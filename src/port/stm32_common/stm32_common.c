#include "stm32_common.h"
#include "dmod.h"
#include "dmeth_port.h"
#include "dmclk_port.h"
#include "dmosi.h"
#include "dmheap.h"
#include "port/stm32_common_regs.h"
#include <errno.h>
#include <string.h>

/**
 * All register-level logic lives here, shared between STM32F4 and STM32F7
 * (identical Ethernet MAC/DMA IP block on both - see
 * include/port/stm32_common_regs.h for the cross-checked evidence); each
 * family's src/port/<family>/port.c is a thin dmod_init()/dmod_deinit() +
 * DMOD_IRQ_HANDLER wrapper around stm32_eth_irq_handler() below, mirroring
 * dmfmc's stm32_common pattern.
 */

/* Only one ETH peripheral exists per chip on the families supported here -
 * dev_num.major is always 0 (see dmeth_dmdrvi_create() in src/dmeth.c). */
#define DMETH_MAX_INSTANCES     1

/* ETH_HEADER(14) + VLAN_TAG(4) + MAX_ETH_PAYLOAD(1500) + ETH_CRC(4) + 2 spare
 * bytes, matching dnx-rtos's ETH_MAX_PACKET_SIZE - the size of each RX/TX
 * DMA buffer. */
#define DMETH_FRAME_BUF_SIZE     1524U

#define DMETH_DMA_HEAP_NAME      "dma"

#define PHY_MDIO_TIMEOUT_ITERATIONS   200000U

/* How long dmeth_port_receive_frame() waits on the RX semaphore before
 * re-examining the descriptor ring anyway. The semaphore is only a wake-up
 * hint (see dmeth_port_receive_frame()), so this bounds how long a caller
 * can sit blocked on a post that was coalesced or dropped. It is not a
 * receive timeout: the function still blocks until a frame actually
 * arrives. */
#define DMETH_RX_WAIT_TIMEOUT_MS      100

/* IEEE 802.3 clause 22 standard PHY registers/bits (same on every PHY,
 * including the default LAN8742A - vendor-specific registers are handled
 * separately below). */
#define PHY_REG_BCR                   0x00U
#define PHY_REG_BSR                   0x01U
#define PHY_BCR_DUPLEX_FULL           (1U << 8)
#define PHY_BCR_RESTART_AUTONEG       (1U << 9)
#define PHY_BCR_SPEED_100M            (1U << 13)
#define PHY_BCR_LOOPBACK              (1U << 14)
#define PHY_BCR_AUTONEG_ENABLE        (1U << 12)
#define PHY_BCR_RESET                 (1U << 15)
#define PHY_BSR_LINK_STATUS           (1U << 2)
#define PHY_BSR_AUTONEG_COMPLETE      (1U << 5)

/* LAN8742A-specific "Special Control/Status Register" (register 31),
 * bits [4:2] HCDSPEED - verified against a real board (see below), NOT
 * two independent status bits as the dnx-rtos-derived constants this
 * replaced assumed:
 *   001 = 10M half   101 = 10M full
 *   010 = 100M half  110 = 100M full
 * so bit 3 alone distinguishes 100M from 10M regardless of duplex, and
 * bit 4 alone distinguishes full from half regardless of speed - bit 2
 * is NOT part of either check (it only differs between the two 10M rows).
 * Confirmed live on an STM32F746G-DISCO: after a successful 100M/full
 * autoneg, register 31 read back 0x1058 (bits 4,3 set, bit 2 clear) - the
 * previous bit-2-only speed check misread this as 10M, leaving MACCR.FES
 * clear while the link was actually running at 100M, a MAC/PHY speed
 * mismatch that silently discarded every received frame. */
#define PHY_REG_LAN8742A_SPECIAL_STATUS   31U
#define PHY_LAN8742A_SPEED_100M           (1U << 3)
#define PHY_LAN8742A_DUPLEX_FULL          (1U << 4)

typedef struct eth_dma_desc
{
    volatile uint32_t     status;      /* TDES0 / RDES0 */
    uint32_t              ctrl_size;   /* TDES1 / RDES1 */
    uint8_t               *buffer;     /* TDES2 / RDES2 - points into the flat buffer array below */
    struct eth_dma_desc   *next;       /* TDES3 / RDES3 - chained-mode next descriptor */
} eth_dma_desc_t;

typedef struct
{
    bool                initialized;
    bool                running;
    uint8_t             phy_address;
    uint16_t            rx_count;
    uint16_t            tx_count;
    eth_dma_desc_t     *rx_desc;
    eth_dma_desc_t     *tx_desc;
    uint8_t            *rx_buffers;
    uint8_t            *tx_buffers;
    volatile uint32_t   rx_tail;      /* next descriptor the core consumes */
    volatile uint32_t   tx_head;      /* next descriptor a transmit fills */
    uint32_t            io_timeout_ms;/* bound on a blocking receive/transmit; 0 = wait forever */
    dmosi_semaphore_t   rx_sem;       /* posted from the ISR when a frame is ready */
} eth_state_t;

static eth_state_t s_eth[DMETH_MAX_INSTANCES];

/**
 * @brief Check whether an instance number is within the supported range.
 *
 * @param instance Instance number to check.
 * @return true if @p instance is a valid index into #s_eth.
 */
static bool is_valid_instance(dmeth_instance_t instance)
{
    return instance < DMETH_MAX_INSTANCES;
}

/* ---- MDIO ---- */

/**
 * @brief Read a PHY register over MDIO.
 *
 * Busy-polls MACMIIAR.MB (MII busy) until the hardware clears it or
 * #PHY_MDIO_TIMEOUT_ITERATIONS is exhausted; on timeout, whatever
 * MACMIIDR happens to hold is returned (there is no separate error return -
 * see mdio_write() for a variant that does report timeout).
 *
 * @param phy_addr MDIO address of the PHY chip.
 * @param reg      PHY register number to read (0-31).
 * @return The 16-bit register value read from MACMIIDR.
 */
static uint16_t mdio_read(uint8_t phy_addr, uint8_t reg)
{
    uint32_t tmp = ETH->MACMIIAR & ETH_MACMIIAR_CR_Msk;
    tmp |= ((uint32_t)phy_addr << ETH_MACMIIAR_PA_Pos) & ETH_MACMIIAR_PA_Msk;
    tmp |= ((uint32_t)reg      << ETH_MACMIIAR_MR_Pos) & ETH_MACMIIAR_MR_Msk;
    tmp |= ETH_MACMIIAR_MB;
    ETH->MACMIIAR = tmp;

    uint32_t timeout = PHY_MDIO_TIMEOUT_ITERATIONS;
    while ((ETH->MACMIIAR & ETH_MACMIIAR_MB) != 0)
    {
        if (--timeout == 0)
            break;
    }
    return (uint16_t)ETH->MACMIIDR;
}

/**
 * @brief Write a PHY register over MDIO.
 *
 * Busy-polls MACMIIAR.MB (MII busy) until the hardware clears it or
 * #PHY_MDIO_TIMEOUT_ITERATIONS is exhausted.
 *
 * @param phy_addr MDIO address of the PHY chip.
 * @param reg      PHY register number to write (0-31).
 * @param value    16-bit value to write.
 * @return 0 on success, -ETIMEDOUT if the hardware never cleared MB.
 */
static int mdio_write(uint8_t phy_addr, uint8_t reg, uint16_t value)
{
    uint32_t tmp = ETH->MACMIIAR & ETH_MACMIIAR_CR_Msk;
    tmp |= ((uint32_t)phy_addr << ETH_MACMIIAR_PA_Pos) & ETH_MACMIIAR_PA_Msk;
    tmp |= ((uint32_t)reg      << ETH_MACMIIAR_MR_Pos) & ETH_MACMIIAR_MR_Msk;
    tmp |= ETH_MACMIIAR_MW | ETH_MACMIIAR_MB;
    ETH->MACMIIDR = value;
    ETH->MACMIIAR = tmp;

    uint32_t timeout = PHY_MDIO_TIMEOUT_ITERATIONS;
    while ((ETH->MACMIIAR & ETH_MACMIIAR_MB) != 0)
    {
        if (--timeout == 0)
            return -ETIMEDOUT;
    }
    return 0;
}

/**
 * @brief Select the MACMIIAR MDC clock divisor for the current HCLK.
 *
 * Selects the MDC clock divisor from the actual runtime HCLK, same range
 * table dnx-rtos's ETH_Init() uses (stm32f4x7_eth.c) - identical on
 * STM32F4 and STM32F7 (MACMIIAR_CR encoding is part of the shared IP block,
 * see stm32_common_regs.h).
 *
 * @return One of the `ETH_MACMIIAR_CR_Div*` constants from stm32_common_regs.h.
 */
static uint32_t mdc_clock_range(void)
{
    dmclk_frequency_t hclk = dmclk_port_get_current_frequency();

    if (hclk >= 150000000U) return ETH_MACMIIAR_CR_Div102;
    if (hclk >= 100000000U) return ETH_MACMIIAR_CR_Div62;
    if (hclk >=  60000000U) return ETH_MACMIIAR_CR_Div42;
    if (hclk >=  35000000U) return ETH_MACMIIAR_CR_Div26;
    return ETH_MACMIIAR_CR_Div16;
}

/**
 * @brief Reset the PHY, run autonegotiation, and apply the resolved speed/duplex to MACCR.
 *
 * Resets and autonegotiates the PHY, then (best-effort - see the comment on
 * #PHY_REG_LAN8742A_SPECIAL_STATUS above) applies the resolved speed/duplex
 * to MACCR. dnx-rtos's `ETH_EXTERN_GetSpeedAndDuplex()` hook is a no-op stub
 * that silently discards this instead - this fixes that gap.
 *
 * @param phy_addr MDIO address of the PHY chip to reset/autonegotiate.
 */
static void phy_reset_and_autonegotiate(uint8_t phy_addr)
{
    mdio_write(phy_addr, PHY_REG_BCR, PHY_BCR_RESET);
    dmclk_port_delay_us(500000); /* PHY reset takes up to ~500ms on typical parts */

    mdio_write(phy_addr, PHY_REG_BCR, PHY_BCR_AUTONEG_ENABLE | PHY_BCR_RESTART_AUTONEG);

    uint32_t timeout = PHY_MDIO_TIMEOUT_ITERATIONS;
    while ((mdio_read(phy_addr, PHY_REG_BSR) & PHY_BSR_AUTONEG_COMPLETE) == 0)
    {
        if (--timeout == 0)
            break; /* leave MACCR at its pre-autoneg default rather than hang forever */
        dmclk_port_delay_us(100);
    }

    uint16_t status = mdio_read(phy_addr, PHY_REG_LAN8742A_SPECIAL_STATUS);
    uint32_t maccr = ETH->MACCR;

    maccr = (status & PHY_LAN8742A_SPEED_100M)  ? (maccr | ETH_MACCR_FES) : (maccr & ~ETH_MACCR_FES);
    maccr = (status & PHY_LAN8742A_DUPLEX_FULL) ? (maccr | ETH_MACCR_DM)  : (maccr & ~ETH_MACCR_DM);
    ETH->MACCR = maccr;
}

/* ---- Descriptor ring setup ---- */

/**
 * @brief Free a state's descriptor rings and packet buffers, if allocated.
 *
 * Safe to call on a partially-initialized #eth_state_t (e.g. cleanup after a
 * failed allocate_rings() call) - each pointer is freed only if non-NULL,
 * and all four are reset to NULL afterward.
 *
 * @param st State whose rings/buffers should be released.
 */
static void free_rings(eth_state_t *st)
{
    dmheap_context_t *heap = dmheap_get_context_by_name(DMETH_DMA_HEAP_NAME);
    if (heap != NULL)
    {
        if (st->rx_desc    != NULL) dmheap_free(heap, st->rx_desc, true);
        if (st->tx_desc    != NULL) dmheap_free(heap, st->tx_desc, true);
        if (st->rx_buffers != NULL) dmheap_free(heap, st->rx_buffers, true);
        if (st->tx_buffers != NULL) dmheap_free(heap, st->tx_buffers, true);
    }
    st->rx_desc = NULL;
    st->tx_desc = NULL;
    st->rx_buffers = NULL;
    st->tx_buffers = NULL;
}

/**
 * @brief Allocate and chain-init the RX/TX descriptor rings and packet buffers.
 *
 * Allocates both descriptor arrays and both flat packet-buffer arrays from
 * the shared "dma" dmheap context (DMA-capable, non-cacheable on F7's DTCM -
 * see docs/port-implementation.md), then links each descriptor to its
 * buffer slice and to the next descriptor in chained mode (`RCH`/`TCH`),
 * and programs `ETH->DMARDLAR`/`DMATDLAR` to point at the first descriptor
 * of each ring. On any allocation failure, everything allocated so far is
 * freed via free_rings() before returning.
 *
 * @param st       State to populate (rx_desc/tx_desc/rx_buffers/tx_buffers/
 *                 rx_count/tx_count/rx_tail/tx_head).
 * @param rx_count Number of RX descriptors/buffers to allocate.
 * @param tx_count Number of TX descriptors/buffers to allocate.
 * @return 0 on success, -ENOMEM if the "dma" heap is missing or allocation failed.
 */
static int allocate_rings(eth_state_t *st, uint16_t rx_count, uint16_t tx_count)
{
    dmheap_context_t *heap = dmheap_get_context_by_name(DMETH_DMA_HEAP_NAME);
    if (heap == NULL)
    {
        DMOD_LOG_ERROR("No dmheap context named '%s' - this target has no DMA-capable heap configured\n",
                        DMETH_DMA_HEAP_NAME);
        return -ENOMEM;
    }

    st->rx_desc = dmheap_aligned_alloc(heap, 4, sizeof(eth_dma_desc_t) * rx_count, "dmeth_port");
    st->tx_desc = dmheap_aligned_alloc(heap, 4, sizeof(eth_dma_desc_t) * tx_count, "dmeth_port");
    st->rx_buffers = dmheap_aligned_alloc(heap, 4, (size_t)DMETH_FRAME_BUF_SIZE * rx_count, "dmeth_port");
    st->tx_buffers = dmheap_aligned_alloc(heap, 4, (size_t)DMETH_FRAME_BUF_SIZE * tx_count, "dmeth_port");

    if (st->rx_desc == NULL || st->tx_desc == NULL || st->rx_buffers == NULL || st->tx_buffers == NULL)
    {
        DMOD_LOG_ERROR("Out of memory allocating ETH descriptor rings from the '%s' heap\n", DMETH_DMA_HEAP_NAME);
        free_rings(st);
        return -ENOMEM;
    }

    for (uint16_t i = 0; i < rx_count; i++)
    {
        eth_dma_desc_t *d = &st->rx_desc[i];
        d->buffer    = &st->rx_buffers[(size_t)i * DMETH_FRAME_BUF_SIZE];
        d->ctrl_size = ETH_DMARxDesc_RCH | DMETH_FRAME_BUF_SIZE;
        d->next      = &st->rx_desc[(i + 1) % rx_count];
        d->status    = ETH_DMA_DESC_OWN;
    }
    ETH->DMARDLAR = (uint32_t)&st->rx_desc[0];

    for (uint16_t i = 0; i < tx_count; i++)
    {
        eth_dma_desc_t *d = &st->tx_desc[i];
        d->buffer    = &st->tx_buffers[(size_t)i * DMETH_FRAME_BUF_SIZE];
        d->ctrl_size = 0;
        d->next      = &st->tx_desc[(i + 1) % tx_count];
        d->status    = ETH_DMATxDesc_TCH; /* CPU-owned (OWN clear) until a transmit fills it */
    }
    ETH->DMATDLAR = (uint32_t)&st->tx_desc[0];

    st->rx_count = rx_count;
    st->tx_count = tx_count;
    st->rx_tail  = 0;
    st->tx_head  = 0;
    return 0;
}

/* ---- Capability query ---- */

/**
 * @brief Number of ETH peripherals this chip/family has.
 *
 * Every STM32F4/F7 part supported today has exactly one - this is still a
 * real query (not a hardcoded assumption in core) so dmeth_dmdrvi_create()
 * can validate `config->instance` with a clear diagnostic instead of
 * discovering the limit only via dmeth_port_init() rejecting it.
 *
 * @return #DMETH_MAX_INSTANCES.
 */
dmod_dmeth_port_api_declaration(1.0, dmeth_instance_t, _get_instance_count, ( void ))
{
    return DMETH_MAX_INSTANCES;
}

/* ---- Lifecycle ---- */

/**
 * @brief Bring up the Ethernet MAC/DMA/PHY for one instance.
 *
 * Enables the SYSCFG/RCC clocks and selects RMII mode, resets the MAC and
 * waits for the reset to clear, selects the MDC clock divisor from the
 * runtime HCLK, resets and autonegotiates the PHY (see
 * phy_reset_and_autonegotiate()), configures MACCR/MACFFR (auto pad/CRC
 * strip, promiscuous mode from @p config) and DMABMR/DMAOMR (chained-mode
 * descriptors, store-and-forward, operate-on-second-frame), allocates the
 * RX/TX descriptor rings (see allocate_rings()), creates the RX-ready
 * semaphore, and enables the ETH IRQ at the platform's minimum interrupt
 * priority. TE/RE and DMA ST/SR are deliberately left disabled here -
 * dmeth_port_start() enables them.
 *
 * @param instance Instance to initialize (must be 0 - only one ETH
 *                 peripheral is supported per chip today).
 * @param config   Configuration (buffer counts, PHY address, promiscuous
 *                 flag) to apply.
 * @return 0 on success; -EINVAL for a bad instance/NULL config or if already
 *         initialized-and-busy; -ETIMEDOUT if the MAC software reset never
 *         completed; -ENOMEM if ring/semaphore allocation failed.
 */
dmod_dmeth_port_api_declaration(1.0, int, _init, ( dmeth_instance_t instance, const dmeth_config_t* config ))
{
    if (!is_valid_instance(instance) || config == NULL)
        return -EINVAL;

    eth_state_t *st = &s_eth[instance];
    if (st->initialized)
        return -EBUSY;

    memset(st, 0, sizeof(*st));
    st->phy_address = config->phy_address;

    /* RMII pin mux + peripheral clocks. GPIO AF11 pin muxing for the
     * individual RMII signals (REF_CLK, MDIO, MDC, CRS_DV, RXD0/1, TXD0/1,
     * TX_EN) is board config's job (dmgpio + board .ini), same division of
     * responsibility as dnx-rtos's separate AFM driver. */
    ETH_RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    ETH_SYSCFG->PMC  |= SYSCFG_PMC_MII_RMII_SEL;
    ETH_RCC->AHB1ENR |= RCC_AHB1ENR_ETHMACEN | RCC_AHB1ENR_ETHMACTXEN | RCC_AHB1ENR_ETHMACRXEN;

    /* Software reset, then wait for DMABMR.SR to self-clear. */
    ETH->DMABMR |= ETH_DMABMR_SR;
    uint32_t timeout = PHY_MDIO_TIMEOUT_ITERATIONS;
    while ((ETH->DMABMR & ETH_DMABMR_SR) != 0)
    {
        if (--timeout == 0)
        {
            DMOD_LOG_ERROR("ETH%u software reset did not complete\n", instance);
            return -ETIMEDOUT;
        }
    }

    ETH->MACMIIAR = (ETH->MACMIIAR & ~ETH_MACMIIAR_CR_Msk) | mdc_clock_range();

    phy_reset_and_autonegotiate(st->phy_address);

    /* MAC: strip the FCS off every received frame, promiscuous mode from
     * config; speed/duplex bits were already applied by
     * phy_reset_and_autonegotiate(). TE/RE are left clear here -
     * DMDRVI_IOCTL_NET_START enables them.
     *
     * Both stripping bits are needed, because each covers only half the
     * frames: APCS strips pad+FCS from 802.3 frames (length/type field
     * <= 1500) and explicitly leaves Ethernet II frames alone, while CSTF
     * covers exactly those Type frames - which is what all real traffic is
     * (ARP, IPv4, ...). With APCS alone the driver hands its caller the
     * 4-byte FCS the MAC itself appended on transmit: a 60-byte ARP request
     * arrives as 64 bytes of "frame". tests/dmeth_test.c round-trips one
     * frame of each shape for this reason. */
    uint32_t maccr = ETH->MACCR | ETH_MACCR_APCS | ETH_MACCR_CSTF;
    ETH->MACCR = maccr;

    uint32_t macffr = 0;
    if (config->promiscuous)
        macffr |= ETH_MACFFR_PM;
    ETH->MACFFR = macffr;

    /* DMA: chained-mode descriptors, store-and-forward, operate on second
     * frame - matches both reference drivers. SR/ST (start receive/transmit)
     * are left clear here too - enabled by DMDRVI_IOCTL_NET_START. */
    ETH->DMABMR = ETH_DMABMR_AAB | ETH_DMABMR_FB | ETH_DMABMR_USP |
                  ETH_DMABMR_RDP(32) | ETH_DMABMR_PBL(32);
    ETH->DMAOMR = ETH_DMAOMR_RSF | ETH_DMAOMR_TSF | ETH_DMAOMR_OSF;

    int ret = allocate_rings(st, config->rx_buffer_count, config->tx_buffer_count);
    if (ret != 0)
        return ret;

    st->rx_sem = dmosi_semaphore_create(0, config->rx_buffer_count);
    if (st->rx_sem == NULL)
    {
        free_rings(st);
        return -ENOMEM;
    }

    ETH->DMASR  = 0x1FFFFU; /* clear any stale flags before enabling interrupts */
    ETH->DMAIER = ETH_DMAIER_NISE | ETH_DMAIER_RIE;

    uint8_t priority = (uint8_t)dmosi_get_min_interrupt_priority();
    NVIC_IP[ETH_IRQn] = priority;
    NVIC_ISER[ETH_IRQn / 32] = (1U << (ETH_IRQn % 32));

    st->initialized = true;
    return 0;
}

/**
 * @brief Tear down an instance previously brought up by dmeth_port_init().
 *
 * Disables the ETH IRQ, MAC TE/RE, and DMA ST/SR, destroys the RX-ready
 * semaphore, frees the descriptor rings/buffers (see free_rings()),
 * disables the AHB1 ETH clocks, and zeroes the instance's state. Safe to
 * call on an instance that was never initialized (no-op, returns 0).
 *
 * @param instance Instance to tear down.
 * @return 0 on success (including the not-initialized no-op case), -EINVAL
 *         for a bad instance number.
 */
dmod_dmeth_port_api_declaration(1.0, int, _deinit, ( dmeth_instance_t instance ))
{
    if (!is_valid_instance(instance))
        return -EINVAL;

    eth_state_t *st = &s_eth[instance];
    if (!st->initialized)
        return 0;

    NVIC_ICER[ETH_IRQn / 32] = (1U << (ETH_IRQn % 32));
    ETH->DMAIER = 0;
    ETH->MACCR &= ~(ETH_MACCR_TE | ETH_MACCR_RE);
    ETH->DMAOMR &= ~(ETH_DMAOMR_ST | ETH_DMAOMR_SR);

    if (st->rx_sem != NULL)
    {
        dmosi_semaphore_destroy(st->rx_sem);
        st->rx_sem = NULL;
    }
    free_rings(st);

    ETH_RCC->AHB1ENR &= ~(RCC_AHB1ENR_ETHMACEN | RCC_AHB1ENR_ETHMACTXEN | RCC_AHB1ENR_ETHMACRXEN);

    memset(st, 0, sizeof(*st));
    return 0;
}

/* ---- Control plane ---- */

/**
 * @brief Program the MAC address filter (slot 0) directly from register access.
 *
 * @param instance Target instance.
 * @param mac      6-byte MAC address to program into MACA0HR/MACA0LR.
 * @return 0 on success, -EINVAL for a bad instance or NULL @p mac.
 */
dmod_dmeth_port_api_declaration(1.0, int, _set_mac_address, ( dmeth_instance_t instance, const uint8_t mac[DMETH_MAC_ADDR_LEN] ))
{
    if (!is_valid_instance(instance) || mac == NULL)
        return -EINVAL;

    ETH->MACA0HR = ((uint32_t)mac[5] << 8) | (uint32_t)mac[4];
    ETH->MACA0LR = ((uint32_t)mac[3] << 24) | ((uint32_t)mac[2] << 16) |
                   ((uint32_t)mac[1] << 8)  |  (uint32_t)mac[0];
    return 0;
}

/**
 * @brief Read the MAC address filter (slot 0) directly from register access.
 *
 * @param instance   Target instance.
 * @param[out] mac   Filled with the 6-byte MAC address read from
 *                   MACA0HR/MACA0LR.
 * @return 0 on success, -EINVAL for a bad instance or NULL @p mac.
 */
dmod_dmeth_port_api_declaration(1.0, int, _get_mac_address, ( dmeth_instance_t instance, uint8_t mac[DMETH_MAC_ADDR_LEN] ))
{
    if (!is_valid_instance(instance) || mac == NULL)
        return -EINVAL;

    uint32_t hr = ETH->MACA0HR;
    uint32_t lr = ETH->MACA0LR;
    mac[0] = (uint8_t)(lr);
    mac[1] = (uint8_t)(lr >> 8);
    mac[2] = (uint8_t)(lr >> 16);
    mac[3] = (uint8_t)(lr >> 24);
    mac[4] = (uint8_t)(hr);
    mac[5] = (uint8_t)(hr >> 8);
    return 0;
}

/**
 * @brief Enable MAC transmission/reception and start the DMA engine.
 *
 * Sets MACCR.TE/RE, flushes the TX FIFO (DMAOMR.FTF), then sets
 * DMAOMR.ST/SR to start the DMA transmit/receive processes.
 *
 * @param instance Instance to start (must already be initialized via
 *                 dmeth_port_init()).
 * @return 0 on success, -EINVAL for a bad instance or if not initialized.
 */
dmod_dmeth_port_api_declaration(1.0, int, _start, ( dmeth_instance_t instance ))
{
    if (!is_valid_instance(instance))
        return -EINVAL;

    eth_state_t *st = &s_eth[instance];
    if (!st->initialized)
        return -EINVAL;

    ETH->MACCR |= ETH_MACCR_TE | ETH_MACCR_RE;
    ETH->DMAOMR |= ETH_DMAOMR_FTF;
    ETH->DMAOMR |= ETH_DMAOMR_ST | ETH_DMAOMR_SR;

    st->running = true;
    return 0;
}

/**
 * @brief Disable the DMA engine and MAC transmission/reception.
 *
 * @param instance Instance to stop.
 * @return 0 on success, -EINVAL for a bad instance.
 */
dmod_dmeth_port_api_declaration(1.0, int, _stop, ( dmeth_instance_t instance ))
{
    if (!is_valid_instance(instance))
        return -EINVAL;

    eth_state_t *st = &s_eth[instance];

    ETH->DMAOMR &= ~(ETH_DMAOMR_ST | ETH_DMAOMR_SR);
    ETH->MACCR &= ~(ETH_MACCR_TE | ETH_MACCR_RE);

    st->running = false;
    return 0;
}

/**
 * @brief Poll the PHY's link status bit over MDIO.
 *
 * @param instance Target instance.
 * @return true if the PHY reports the link up, false if down or if
 *         @p instance is invalid.
 */
dmod_dmeth_port_api_declaration(1.0, bool, _get_link_status, ( dmeth_instance_t instance ))
{
    if (!is_valid_instance(instance))
        return false;

    uint16_t bsr = mdio_read(s_eth[instance].phy_address, PHY_REG_BSR);
    return (bsr & PHY_BSR_LINK_STATUS) != 0;
}

/**
 * @brief Enable or disable the MAC's promiscuous filter.
 *
 * @param instance Target instance.
 * @param enable   true to set MACFFR.PM, false to clear it.
 * @return 0 on success, -EINVAL for a bad instance.
 */
dmod_dmeth_port_api_declaration(1.0, int, _set_promiscuous_mode, ( dmeth_instance_t instance, bool enable ))
{
    if (!is_valid_instance(instance))
        return -EINVAL;

    if (enable)
        ETH->MACFFR |= ETH_MACFFR_PM;
    else
        ETH->MACFFR &= ~ETH_MACFFR_PM;
    return 0;
}

/**
 * @brief Whether a blocking wait started at @p start_ms has run out of time.
 *
 * @param st       State whose configured io_timeout_ms bounds the wait
 *                 (0 = no bound, so this never reports expiry).
 * @param start_ms dmosi_get_tick_count() sampled when the wait began.
 * @return true once the bound has been exceeded.
 */
static bool io_timeout_expired(const eth_state_t *st, uint32_t start_ms)
{
    if (st->io_timeout_ms == 0)
        return false;

    /* Unsigned wraparound makes this correct across the tick counter's
     * 32-bit rollover without a 64-bit counter. */
    return (uint32_t)(dmosi_get_tick_count() - start_ms) >= st->io_timeout_ms;
}

/**
 * @brief Bound how long a blocking receive/transmit may wait.
 *
 * dmdrvi has no O_NONBLOCK/timeout concept to plumb through per call, so
 * this is a per-instance setting applied to every subsequent
 * dmeth_port_receive_frame()/dmeth_port_transmit_frame() - reachable from a
 * caller through DMETH_IOCTL_SET_IO_TIMEOUT (dmeth_ioctl.h).
 *
 * @param instance   Target instance.
 * @param timeout_ms Bound in milliseconds, or 0 to wait indefinitely (the
 *                   default, and what a network interface's RX thread wants).
 * @return 0 on success, -EINVAL for a bad instance.
 */
dmod_dmeth_port_api_declaration(1.0, int, _set_io_timeout, ( dmeth_instance_t instance, uint32_t timeout_ms ))
{
    if (!is_valid_instance(instance))
        return -EINVAL;

    s_eth[instance].io_timeout_ms = timeout_ms;
    return 0;
}

/* ---- Loopback (test-only) ---- */

/**
 * @brief Enable/disable MAC- or PHY-level loopback for on-target testing.
 *
 * #dmeth_loopback_mode_mac sets MACCR.LM, looping transmitted frames back to
 * the receive path inside the MAC itself, before the RMII pins - no PHY
 * chip or cable needed. #dmeth_loopback_mode_phy instead sets the PHY's
 * standard BCR.Loopback bit (IEEE 802.3 clause 22, bit 14 - same on every
 * PHY) over MDIO, looping inside the PHY chip, after the RMII pins - this
 * additionally exercises the real RMII electrical connection, but needs a
 * real PHY chip to be present and MDIO-reachable. #dmeth_loopback_mode_none
 * clears both. Must be called before dmeth_port_start().
 *
 * @param instance Target instance.
 * @param mode     Loopback mode to apply.
 * @return 0 on success, -EINVAL for a bad instance or unknown @p mode.
 */
dmod_dmeth_port_api_declaration(1.0, int, _set_loopback_mode, ( dmeth_instance_t instance, dmeth_loopback_mode_t mode ))
{
    if (!is_valid_instance(instance))
        return -EINVAL;

    eth_state_t *st = &s_eth[instance];

    switch (mode)
    {
        case dmeth_loopback_mode_none:
            ETH->MACCR &= ~ETH_MACCR_LM;
            /* Hand the link back to autonegotiation rather than just
             * clearing BCR.Loopback: the PHY path below forces a fixed
             * speed/duplex with autonegotiation *off*, so clearing one bit
             * would leave the PHY forced at 100M/full afterwards. */
            return mdio_write(st->phy_address, PHY_REG_BCR,
                              PHY_BCR_AUTONEG_ENABLE | PHY_BCR_RESTART_AUTONEG);

        case dmeth_loopback_mode_mac:
            ETH->MACCR |= ETH_MACCR_LM;
            return 0;

        case dmeth_loopback_mode_phy:
        {
            /* Force 100M/full-duplex with autonegotiation disabled, rather
             * than OR-ing BCR.Loopback into whatever autonegotiation left
             * behind. Clause 22 leaves BCR.Loopback's behaviour unspecified
             * while BCR.AutonegEnable is set, and the LAN8742A on the
             * reference board is one of the parts that needs autonegotiation
             * off for near-end loopback to pass traffic at all. */
            int ret = mdio_write(st->phy_address, PHY_REG_BCR,
                                 PHY_BCR_LOOPBACK | PHY_BCR_SPEED_100M | PHY_BCR_DUPLEX_FULL);
            if (ret != 0)
                return ret;

            /* MACCR has to agree with the speed/duplex just forced on the
             * PHY - a mismatch here is exactly the silent all-frames-dropped
             * failure described on PHY_REG_LAN8742A_SPECIAL_STATUS above. */
            ETH->MACCR |= ETH_MACCR_FES | ETH_MACCR_DM;
            return 0;
        }

        default:
            return -EINVAL;
    }
}

/* ---- Data plane ---- */

/**
 * @brief Send one Ethernet frame, blocking until a TX descriptor is free.
 *
 * Waits (indefinitely - dmdrvi has no O_NONBLOCK/timeout to plumb a bound
 * through, see docs/port-implementation.md) for the next TX descriptor's
 * `OWN` bit to clear, `memcpy()`'s @p frame into that descriptor's buffer
 * (the only copy on this path), marks it first+last segment, sets `OWN` to
 * hand it to the DMA, and pokes `DMATPDR` if the DMA had stalled on a
 * buffer-unavailable condition.
 *
 * @param instance Target instance (must be started via dmeth_port_start()).
 * @param frame    Frame bytes to transmit.
 * @param len      Length of @p frame in bytes (must be 1..#DMETH_FRAME_BUF_SIZE).
 * @return 0 on success, -EINVAL for bad arguments, -EIO if the instance
 *         isn't initialized/running.
 */
dmod_dmeth_port_api_declaration(1.0, int, _transmit_frame, ( dmeth_instance_t instance, const uint8_t* frame, size_t len ))
{
    if (!is_valid_instance(instance) || frame == NULL || len == 0 || len > DMETH_FRAME_BUF_SIZE)
        return -EINVAL;

    eth_state_t *st = &s_eth[instance];
    if (!st->initialized || !st->running)
        return -EIO;

    eth_dma_desc_t *desc = &st->tx_desc[st->tx_head];

    /* Block until this descriptor is free. dmdrvi has no per-call
     * O_NONBLOCK/timeout to plumb a bound through, so the bound - if the
     * caller wants one at all - comes from dmeth_port_set_io_timeout(). */
    uint32_t start_ms = dmosi_get_tick_count();
    while ((desc->status & ETH_DMA_DESC_OWN) != 0)
    {
        if (io_timeout_expired(st, start_ms))
            return -ETIMEDOUT;
        dmclk_port_delay_us(100);
    }

    memcpy(desc->buffer, frame, len);
    desc->ctrl_size = (uint32_t)len & ETH_DMATxDesc_TBS1_Msk;
    desc->status |= ETH_DMATxDesc_FS | ETH_DMATxDesc_LS;
    desc->status |= ETH_DMA_DESC_OWN;

    if (ETH->DMASR & ETH_DMASR_TBUS)
    {
        ETH->DMASR = ETH_DMASR_TBUS;
        ETH->DMATPDR = 0;
    }

    st->tx_head = (st->tx_head + 1U) % st->tx_count;
    return 0;
}

/**
 * @brief Receive one Ethernet frame, blocking until one is ready.
 *
 * Blocks (indefinitely, same rationale as dmeth_port_transmit_frame()) until
 * the next descriptor in the ring is CPU-owned, using the ISR's RX semaphore
 * only as a wake-up hint - see the comment in the body for why the descriptor
 * OWN bit, not the semaphore count, is the authority here. Then `memcpy()`'s
 * `min(frame_len, size)` bytes from that descriptor's buffer into @p buffer
 * (the only copy on this path, truncating like `recvfrom()` if the caller's
 * buffer is smaller than the frame), gives the descriptor back to the DMA,
 * and pokes `DMARPDR` if the DMA had stalled on a buffer-unavailable
 * condition.
 *
 * @param instance      Target instance (must be started via dmeth_port_start()).
 * @param buffer        Destination buffer for the received frame.
 * @param size          Capacity of @p buffer in bytes.
 * @param[out] received Set to the number of bytes actually copied into
 *                      @p buffer.
 * @return 0 on success, -EINVAL for bad arguments, -EIO if the instance
 *         isn't initialized/running.
 */
dmod_dmeth_port_api_declaration(1.0, int, _receive_frame, ( dmeth_instance_t instance, uint8_t* buffer, size_t size, size_t* received ))
{
    if (!is_valid_instance(instance) || buffer == NULL || size == 0 || received == NULL)
        return -EINVAL;

    eth_state_t *st = &s_eth[instance];
    if (!st->initialized || !st->running)
        return -EIO;

    eth_dma_desc_t *desc = &st->rx_desc[st->rx_tail];

    /* Blocks until this descriptor is ours - same policy as _transmit_frame()
     * above, for the same reason.
     *
     * The descriptor's OWN bit, not the semaphore count, decides when a frame
     * is here. The two do not correspond one-to-one: the ISR posts once per
     * *interrupt*, but the DMA can fill several descriptors before that
     * interrupt is serviced, so one post can cover several frames - and,
     * conversely, a post is silently dropped once the semaphore saturates at
     * rx_buffer_count. Treating the count as an exact tally of ready frames
     * therefore both under- and over-counts: descriptors would be left
     * unreclaimed until the ring filled and the DMA stalled on RBUS.
     *
     * Waiting with a timeout and re-checking OWN makes the semaphore a pure
     * wake-up hint, which is all it can reliably be: a coalesced or dropped
     * post costs one extra wait, never a permanent stall.
     *
     * How long the whole loop may run is a separate question from that
     * per-wait hint interval, and is the caller's to answer through
     * dmeth_port_set_io_timeout(): unbounded by default (a frame that has
     * not arrived is not an error to a network interface), bounded by a
     * caller that has to report "nothing arrived" instead of waiting for
     * it. */
    uint32_t start_ms = dmosi_get_tick_count();
    while ((desc->status & ETH_DMA_DESC_OWN) != 0)
    {
        if (io_timeout_expired(st, start_ms))
            return -ETIMEDOUT;
        dmosi_semaphore_wait(st->rx_sem, 1, DMETH_RX_WAIT_TIMEOUT_MS);
    }

    uint32_t frame_len = (desc->status & ETH_DMARxDesc_FL_Msk) >> ETH_DMARxDesc_FL_Pos;
    size_t copy_len = (frame_len < size) ? frame_len : size;
    memcpy(buffer, desc->buffer, copy_len);

    desc->status = ETH_DMA_DESC_OWN;

    if (ETH->DMASR & ETH_DMASR_RBUS)
    {
        ETH->DMASR = ETH_DMASR_RBUS;
        ETH->DMARPDR = 0; /* NOT DMATPDR - see docs/port-implementation.md, "known bugs to avoid" */
    }

    st->rx_tail = (st->rx_tail + 1U) % st->rx_count;
    *received = copy_len;
    return 0;
}

/**
 * @brief Shared ETH IRQ handler body (called from each family's
 *        `DMOD_IRQ_HANDLER(ETH_IRQn)` wrapper - see src/port/stm32f4/port.c,
 *        src/port/stm32f7/port.c).
 *
 * Minimal on purpose, mirroring dnx-rtos's own `ETH_IRQHandler`: on a
 * receive-status interrupt, clears the status bits and posts the RX-ready
 * semaphore for instance 0 (the only supported instance) - all descriptor
 * walking happens in dmeth_port_receive_frame() above, in task context, not
 * here. Any other pending status is just cleared so the IRQ doesn't get
 * stuck.
 */
void stm32_eth_irq_handler(void)
{
    uint32_t sr = ETH->DMASR;

    if (sr & ETH_DMASR_RS)
    {
        ETH->DMASR = ETH_DMASR_RS | ETH_DMASR_NIS;
        dmosi_semaphore_post(s_eth[0].rx_sem, 1);
    }
    else
    {
        ETH->DMASR = sr; /* clear whatever else fired so the IRQ doesn't get stuck */
    }
}
