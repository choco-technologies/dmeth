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
 * @brief Real communication test: transmit a frame with MAC-internal
 *        loopback enabled and verify it comes back byte-for-byte.
 *
 * MAC loopback (MACCR.LM) loops TX straight to RX inside the MAC, before
 * the RMII pins - this exercises the whole data path (DMA descriptors,
 * chained-ring bookkeeping, RX ISR, RX-ready semaphore, the single memcpy
 * on each side) without needing a cable, link partner, or even a working
 * PHY chip. See dmeth_port_set_loopback_mode() / docs/port-implementation.md.
 */
DMOD_TEST_STEP(dmeth_mac_loopback_tx_rx_roundtrip)
{
    dmeth_config_t config;
    memset(&config, 0, sizeof(config));
    config.instance        = 0;
    config.rx_buffer_count = 4;
    config.tx_buffer_count = 4;
    config.phy_address     = 0;

    int init_ret = dmeth_port_init(0, &config);
    DMOD_TEST_EXPECT_EQ(init_ret, 0);
    if (init_ret != 0)
        return;

    DMOD_TEST_EXPECT_EQ(dmeth_port_set_loopback_mode(0, dmeth_loopback_mode_mac), 0);
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
