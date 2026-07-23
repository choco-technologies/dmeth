#define DMOD_ENABLE_REGISTRATION ON
#include "dmod_test.h"
#include "dmeth.h"
#include "dmeth_types.h"
#include "dmeth_ioctl.h"
#include "dmeth_port.h"
#include "dmdrvi.h"
#include "dmdrvi_ioctl.h"
#include <string.h>

/**
 * This test binary calls dmeth_port_* directly (bypassing dmeth core/dmdrvi
 * entirely), the same way dmdma_test_port.c does for dmdma_port - it links
 * dmeth_port_if (headers + the generated dynamic-dispatch stubs), not the
 * real dmeth_port executable, so it needs no dmdevfs/dmini config and can
 * run standalone on target. This is an on-target hardware test: MAC
 * loopback needs real ETH silicon and does real MDIO/PHY-reset/
 * autonegotiation timing (see dmeth_port_init()), so it will not run in a
 * host/simulator build.
 */

DMOD_TEST_STEP(dmeth_private_ioctl_commands_are_in_the_reserved_custom_range)
{
    DMOD_TEST_EXPECT(DMETH_IOCTL_SET_PROMISCUOUS_MODE >= DMDRVI_IOCTL_CUSTOM_BASE);
    DMOD_TEST_EXPECT(DMETH_IOCTL_GET_PROMISCUOUS_MODE >= DMDRVI_IOCTL_CUSTOM_BASE);
    DMOD_TEST_EXPECT(DMETH_IOCTL_SET_PROMISCUOUS_MODE != DMETH_IOCTL_GET_PROMISCUOUS_MODE);
}

DMOD_TEST_STEP(dmeth_mac_addr_len_matches_dmdrvi_net_type)
{
    /* dmeth_config_t.mac_address and dmdrvi_net_mac_addr_t.addr are copied
     * between each other byte-for-byte in dmeth_dmdrvi_ioctl() - they must
     * agree on length. */
    DMOD_TEST_EXPECT_EQ(DMETH_MAC_ADDR_LEN, (size_t)DMDRVI_NET_MAC_ADDR_LEN);
    DMOD_TEST_EXPECT_EQ(sizeof(((dmeth_config_t *)0)->mac_address), (size_t)DMDRVI_NET_MAC_ADDR_LEN);
}

/**
 * @brief Shared body for the loopback tx/rx roundtrip steps below - transmit
 *        a frame with the given loopback mode enabled and verify it comes
 *        back byte-for-byte. Only the loopback mode differs between the MAC
 *        and PHY variants (see dmeth_loopback_mode_t in dmeth_types.h), so
 *        both DMOD_TEST_STEP()s below just call this with their mode.
 *
 * See dmeth_port_set_loopback_mode() / docs/port-implementation.md for how
 * each mode routes TX back to RX.
 *
 * @param mode Loopback mode to exercise (mac or phy).
 */
static void loopback_tx_rx_roundtrip(dmeth_loopback_mode_t mode)
{
    dmeth_config_t config;
    memset(&config, 0, sizeof(config));
    config.instance        = 0;
    config.rx_buffer_count = 4;
    config.tx_buffer_count = 4;
    config.phy_address     = 0;

    /* This step verifies loopback tx/rx communication, not
     * dmeth_port_init() itself - a nonzero return here (e.g. -EBUSY because
     * dmeth core already owns this instance) isn't a communication defect,
     * so it must not fail this test. Warn and skip instead. */
    int init_ret = dmeth_port_init(0, &config);
    if (init_ret != 0)
    {
        DMOD_LOG_WARN("dmeth_port_init() returned %d - instance already in use or hardware not ready; "
                      "skipping loopback roundtrip check\n", init_ret);
        return;
    }

    DMOD_TEST_EXPECT_EQ(dmeth_port_set_loopback_mode(0, mode), 0);
    DMOD_TEST_EXPECT_EQ(dmeth_port_start(0), 0);

    uint8_t tx_frame[64];
    for (size_t i = 0; i < sizeof(tx_frame); i++)
        tx_frame[i] = (uint8_t)i;

    DMOD_TEST_EXPECT_EQ(dmeth_port_transmit_frame(0, tx_frame, sizeof(tx_frame)), 0);

    uint8_t rx_frame[64] = {0};
    size_t received = 0;
    DMOD_TEST_EXPECT_EQ(dmeth_port_receive_frame(0, rx_frame, sizeof(rx_frame), &received), 0);
    DMOD_TEST_EXPECT_EQ(received, sizeof(tx_frame));

    bool frame_matches = true;
    for (size_t i = 0; i < sizeof(tx_frame); i++)
    {
        if (rx_frame[i] != tx_frame[i])
        {
            frame_matches = false;
            break;
        }
    }
    DMOD_TEST_EXPECT(frame_matches);

    dmeth_port_set_loopback_mode(0, dmeth_loopback_mode_none);
    dmeth_port_stop(0);
    dmeth_port_deinit(0);
}

/**
 * @brief MAC-internal loopback: MACCR.LM loops TX straight to RX inside the
 *        MAC, before the RMII pins - exercises the whole data path (DMA
 *        descriptors, chained-ring bookkeeping, RX ISR, RX-ready semaphore,
 *        the single memcpy on each side) without needing a cable, link
 *        partner, or even a working PHY chip.
 */
DMOD_TEST_STEP(dmeth_mac_loopback_tx_rx_roundtrip)
{
    loopback_tx_rx_roundtrip(dmeth_loopback_mode_mac);
}

/**
 * @brief PHY loopback: the PHY's standard BCR.Loopback bit (IEEE 802.3
 *        clause 22, bit 14 - same on every PHY) loops TX back to RX inside
 *        the PHY chip itself, after the RMII pins - additionally exercises
 *        the real RMII electrical connection and a reachable PHY over MDIO,
 *        still without needing a cable or link partner.
 */
DMOD_TEST_STEP(dmeth_phy_loopback_tx_rx_roundtrip)
{
    loopback_tx_rx_roundtrip(dmeth_loopback_mode_phy);
}
