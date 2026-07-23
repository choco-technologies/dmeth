#define DMOD_ENABLE_REGISTRATION ON
#include "dmod_test.h"
#include "dmeth.h"
#include "dmeth_types.h"
#include "dmeth_ioctl.h"
#include "dmdrvi.h"
#include "dmdrvi_ioctl.h"

/**
 * dmeth/dmeth_port build as standalone loadable DMOD modules (separate
 * executables), not libraries - dmeth_dmdrvi_create() and friends are
 * resolved dynamically at runtime by the DMOD loader, not through ordinary
 * static linking, so a dmod_add_test() binary cannot call them directly
 * (confirmed: `target_link_libraries` against an executable target fails at
 * configure time). Exercising the real dmdrvi/port flow needs an on-target
 * Application instead, in the style of dmdma's dmdma_test_dev.c (opens
 * /dev/dmeth0 via Dmod_FileOpen against a fully running system with
 * dmeth+dmeth_port loaded) - not implemented here, see
 * docs/port-implementation.md.
 *
 * What's left, genuinely testable from headers alone: the structural
 * invariants the design depends on - most importantly that dmeth's private
 * ioctl commands fall within dmdrvi's reserved custom-command range (so they
 * can never collide with a future DMDRVI_IOCTL_NET_* addition), since both
 * are dispatched through the same switch() in dmeth_dmdrvi_ioctl() (see the
 * comment in dmeth_ioctl.h).
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
