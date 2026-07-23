#ifndef DMETH_STM32_COMMON_REGS_H
#define DMETH_STM32_COMMON_REGS_H

#include <stdint.h>

/* ======================================================================
 *   Common STM32F4/F7 Ethernet MAC/DMA register definitions
 *
 *   Hand-rolled (no CMSIS dependency, matching dmuart's convention - see
 *   dmuart/include/port/stm32_common_regs.h) but cross-checked field-by-field
 *   against the real vendor CMSIS headers at
 *   /data/projects/dnx-rtos/src/system/cpu/stm32f7/stm32f7xx/stm32f746xx.h and
 *   /data/projects/dnx-rtos/src/system/cpu/stm32f4/stm32f4xx/stm32f407xx.h -
 *   every base address, register offset, and bit position checked below
 *   (ETH_BASE, RCC AHB1ENR/AHB1RSTR ETHMAC* bits, SYSCFG_PMC_MII_RMII_SEL,
 *   ETH_IRQn, MACMIIAR_CR_Div102) is byte-for-byte identical between the two
 *   families - both share the same Ethernet MAC/DMA IP block, exactly like
 *   dmfmc's shared FMC SDRAM controller (see dmfmc/include/port/
 *   stm32_common_regs.h for the same pattern applied to a different
 *   peripheral). Do not reorder or resize any field below without
 *   re-checking both files.
 * ====================================================================== */

/* ---- Base addresses ---- */

#define STM32F7_PERIPH_BASE         0x40000000UL
#define STM32F7_APB2PERIPH_BASE     (STM32F7_PERIPH_BASE + 0x00010000UL)
#define STM32F7_AHB1PERIPH_BASE     (STM32F7_PERIPH_BASE + 0x00020000UL)

#define STM32F7_RCC_BASE            (STM32F7_AHB1PERIPH_BASE + 0x3800UL)
#define STM32F7_SYSCFG_BASE         (STM32F7_APB2PERIPH_BASE + 0x3800UL)
#define STM32F7_ETH_BASE            (STM32F7_AHB1PERIPH_BASE + 0x8000UL)

/* ---- RCC (subset needed by dmeth_port - same field layout as
 *      dmuart/include/port/stm32_common_regs.h's UART_RCC_TypeDef) ---- */

typedef struct
{
    volatile uint32_t CR;
    volatile uint32_t PLLCFGR;
    volatile uint32_t CFGR;
    volatile uint32_t CIR;
    volatile uint32_t AHB1RSTR;
    volatile uint32_t AHB2RSTR;
    volatile uint32_t AHB3RSTR;
    volatile uint32_t RESERVED0;
    volatile uint32_t APB1RSTR;
    volatile uint32_t APB2RSTR;
    volatile uint32_t RESERVED1[2];
    volatile uint32_t AHB1ENR;
    volatile uint32_t AHB2ENR;
    volatile uint32_t AHB3ENR;
    volatile uint32_t RESERVED2;
    volatile uint32_t APB1ENR;
    volatile uint32_t APB2ENR;
} ETH_RCC_TypeDef;

#define ETH_RCC   ((ETH_RCC_TypeDef *)STM32F7_RCC_BASE)

#define RCC_AHB1ENR_ETHMACEN        (1U << 25)
#define RCC_AHB1ENR_ETHMACTXEN      (1U << 26)
#define RCC_AHB1ENR_ETHMACRXEN      (1U << 27)
#define RCC_AHB1RSTR_ETHMACRST      (1U << 25)
#define RCC_APB2ENR_SYSCFGEN        (1U << 14)

/* ---- SYSCFG (RMII/MII peripheral mode selection) ---- */

typedef struct
{
    volatile uint32_t MEMRMP;
    volatile uint32_t PMC;
    volatile uint32_t EXTICR[4];
    volatile uint32_t RESERVED[2];
    volatile uint32_t CMPCR;
} ETH_SYSCFG_TypeDef;

#define ETH_SYSCFG   ((ETH_SYSCFG_TypeDef *)STM32F7_SYSCFG_BASE)

#define SYSCFG_PMC_MII_RMII_SEL     (1U << 23)  /* 0 = MII, 1 = RMII */

/* ---- ETH (MAC + DMA), full register block - field order/reserved gaps
 *      must exactly match the real ETH_TypeDef so offsets line up. ---- */

typedef struct
{
    volatile uint32_t MACCR;
    volatile uint32_t MACFFR;
    volatile uint32_t MACHTHR;
    volatile uint32_t MACHTLR;
    volatile uint32_t MACMIIAR;
    volatile uint32_t MACMIIDR;
    volatile uint32_t MACFCR;
    volatile uint32_t MACVLANTR;
    volatile uint32_t RESERVED0[2];
    volatile uint32_t MACRWUFFR;
    volatile uint32_t MACPMTCSR;
    volatile uint32_t RESERVED1;
    volatile uint32_t MACDBGR;
    volatile uint32_t MACSR;
    volatile uint32_t MACIMR;
    volatile uint32_t MACA0HR;
    volatile uint32_t MACA0LR;
    volatile uint32_t MACA1HR;
    volatile uint32_t MACA1LR;
    volatile uint32_t MACA2HR;
    volatile uint32_t MACA2LR;
    volatile uint32_t MACA3HR;
    volatile uint32_t MACA3LR;
    volatile uint32_t RESERVED2[40];
    volatile uint32_t MMCCR;
    volatile uint32_t MMCRIR;
    volatile uint32_t MMCTIR;
    volatile uint32_t MMCRIMR;
    volatile uint32_t MMCTIMR;
    volatile uint32_t RESERVED3[14];
    volatile uint32_t MMCTGFSCCR;
    volatile uint32_t MMCTGFMSCCR;
    volatile uint32_t RESERVED4[5];
    volatile uint32_t MMCTGFCR;
    volatile uint32_t RESERVED5[10];
    volatile uint32_t MMCRFCECR;
    volatile uint32_t MMCRFAECR;
    volatile uint32_t RESERVED6[10];
    volatile uint32_t MMCRGUFCR;
    volatile uint32_t RESERVED7[334];
    volatile uint32_t PTPTSCR;
    volatile uint32_t PTPSSIR;
    volatile uint32_t PTPTSHR;
    volatile uint32_t PTPTSLR;
    volatile uint32_t PTPTSHUR;
    volatile uint32_t PTPTSLUR;
    volatile uint32_t PTPTSAR;
    volatile uint32_t PTPTTHR;
    volatile uint32_t PTPTTLR;
    volatile uint32_t RESERVED8;
    volatile uint32_t PTPTSSR;
    volatile uint32_t RESERVED9[565];
    volatile uint32_t DMABMR;
    volatile uint32_t DMATPDR;
    volatile uint32_t DMARPDR;
    volatile uint32_t DMARDLAR;
    volatile uint32_t DMATDLAR;
    volatile uint32_t DMASR;
    volatile uint32_t DMAOMR;
    volatile uint32_t DMAIER;
    volatile uint32_t DMAMFBOCR;
    volatile uint32_t DMARSWTR;
    volatile uint32_t RESERVED10[8];
    volatile uint32_t DMACHTDR;
    volatile uint32_t DMACHRDR;
    volatile uint32_t DMACHTBAR;
    volatile uint32_t DMACHRBAR;
} ETH_TypeDef;

#define ETH   ((ETH_TypeDef *)STM32F7_ETH_BASE)

/* ---- MACCR bits ---- */
#define ETH_MACCR_RE                (1U << 2)   /* Receiver enable */
#define ETH_MACCR_TE                (1U << 3)   /* Transmitter enable */
#define ETH_MACCR_APCS              (1U << 7)   /* Automatic pad/CRC stripping */
#define ETH_MACCR_IPCO              (1U << 10)  /* IP checksum offload */
#define ETH_MACCR_DM                (1U << 11)  /* Duplex mode (1 = full) */
#define ETH_MACCR_LM                (1U << 12)  /* Loopback mode */
#define ETH_MACCR_FES               (1U << 14)  /* Fast ethernet speed (1 = 100M) */

/* ---- MACFFR bits ---- */
#define ETH_MACFFR_PM               (1U << 0)   /* Promiscuous mode */
#define ETH_MACFFR_PAM              (1U << 4)   /* Pass all multicast */
#define ETH_MACFFR_BFD              (1U << 5)   /* Broadcast frame disable */
#define ETH_MACFFR_PCF              (1U << 6)   /* Pass control frames */
#define ETH_MACFFR_RA               (1U << 31)  /* Receive all */

/* ---- MACMIIAR (MDIO) bits ---- */
#define ETH_MACMIIAR_MB             (1U << 0)   /* MII busy */
#define ETH_MACMIIAR_MW             (1U << 1)   /* MII write */
#define ETH_MACMIIAR_CR_Pos         2U
#define ETH_MACMIIAR_CR_Msk         (0x7U << ETH_MACMIIAR_CR_Pos)
#define ETH_MACMIIAR_CR_Div42       (0U << ETH_MACMIIAR_CR_Pos)  /* HCLK 60-100 MHz  */
#define ETH_MACMIIAR_CR_Div62       (1U << ETH_MACMIIAR_CR_Pos)  /* HCLK 100-150 MHz */
#define ETH_MACMIIAR_CR_Div16       (2U << ETH_MACMIIAR_CR_Pos)  /* HCLK 20-35 MHz   */
#define ETH_MACMIIAR_CR_Div26       (3U << ETH_MACMIIAR_CR_Pos)  /* HCLK 35-60 MHz   */
#define ETH_MACMIIAR_CR_Div102      (4U << ETH_MACMIIAR_CR_Pos)  /* HCLK 150-216 MHz */
#define ETH_MACMIIAR_MR_Pos         6U
#define ETH_MACMIIAR_MR_Msk         (0x1FU << ETH_MACMIIAR_MR_Pos)
#define ETH_MACMIIAR_PA_Pos         11U
#define ETH_MACMIIAR_PA_Msk         (0x1FU << ETH_MACMIIAR_PA_Pos)

/* ---- DMABMR bits ---- */
#define ETH_DMABMR_SR               (1U << 0)   /* Software reset */
#define ETH_DMABMR_DA               (1U << 1)   /* DMA arbitration scheme (0 = round robin) */
#define ETH_DMABMR_PBL_Pos          8U
#define ETH_DMABMR_PBL(n)           (((uint32_t)(n)) << ETH_DMABMR_PBL_Pos)
#define ETH_DMABMR_RDP_Pos          17U
#define ETH_DMABMR_RDP(n)           (((uint32_t)(n)) << ETH_DMABMR_RDP_Pos)
#define ETH_DMABMR_FB                (1U << 16)  /* Fixed burst */
#define ETH_DMABMR_USP               (1U << 23)  /* Use separate PBL (RDP vs PBL) */
#define ETH_DMABMR_AAB                (1U << 25)  /* Address-aligned beats */

/* ---- DMAOMR bits ---- */
#define ETH_DMAOMR_SR                (1U << 1)   /* Start/stop receive */
#define ETH_DMAOMR_OSF               (1U << 2)   /* Operate on second frame */
#define ETH_DMAOMR_ST                (1U << 13)  /* Start/stop transmission */
#define ETH_DMAOMR_FTF               (1U << 20)  /* Flush transmit FIFO */
#define ETH_DMAOMR_TSF               (1U << 21)  /* Transmit store and forward */
#define ETH_DMAOMR_RSF               (1U << 25)  /* Receive store and forward */

/* ---- DMASR / DMAIER bits (same bit position in both registers) ---- */
#define ETH_DMASR_TS                 (1U << 0)   /* Transmit status */
#define ETH_DMASR_TBUS               (1U << 2)   /* Transmit buffer unavailable status */
#define ETH_DMASR_RS                 (1U << 6)   /* Receive status */
#define ETH_DMASR_RBUS                (1U << 7)   /* Receive buffer unavailable status */
#define ETH_DMASR_NIS                 (1U << 16)  /* Normal interrupt summary */

#define ETH_DMAIER_TIE                (1U << 0)   /* Transmit interrupt enable */
#define ETH_DMAIER_RIE                (1U << 6)   /* Receive interrupt enable */
#define ETH_DMAIER_NISE               (1U << 16)  /* Normal interrupt summary enable */

/* ---- DMA descriptor status bits (TDES0/RDES0) and length fields
 *      (RDES1) - part of the Synopsys DesignWare MAC/DMA IP descriptor
 *      format, identical across STM32F4/F7 (confirmed against
 *      dnx-rtos/src/system/drivers/eth/stm32fx/stm32f4x7_eth.h). Not CMSIS
 *      register bits - these describe words the driver itself writes into
 *      descriptor memory, not MMIO registers. ---- */

#define ETH_DMA_DESC_OWN              0x80000000U  /* Owned by DMA (both TDES0/RDES0) */
#define ETH_DMATxDesc_LS              0x20000000U  /* Last segment */
#define ETH_DMATxDesc_FS              0x10000000U  /* First segment */
#define ETH_DMATxDesc_TCH             0x00100000U  /* Second address chained */
#define ETH_DMATxDesc_TBS1_Msk        0x00001FFFU  /* Buffer1 size (TDES1) */

#define ETH_DMARxDesc_FL_Pos          16U
#define ETH_DMARxDesc_FL_Msk          0x3FFF0000U  /* Frame length (RDES0) */
#define ETH_DMARxDesc_ES              0x00008000U  /* Error summary (RDES0) */
#define ETH_DMARxDesc_LS              0x00000100U  /* Last descriptor of the frame (RDES0) */
#define ETH_DMARxDesc_RCH             0x00004000U  /* Second address chained (RDES1) */
#define ETH_DMARxDesc_RBS1_Msk        0x00001FFFU  /* Buffer1 size (RDES1) */

/* ---- NVIC (ARMv7-M, common to Cortex-M4/M7 - same as dmuart's convention) ---- */

#define NVIC_ISER                ((volatile uint32_t *)0xE000E100UL)
#define NVIC_ICER                ((volatile uint32_t *)0xE000E180UL)
#define NVIC_IP                  ((volatile uint8_t *)0xE000E400UL)

#define ETH_IRQn                  61

#endif /* DMETH_STM32_COMMON_REGS_H */
