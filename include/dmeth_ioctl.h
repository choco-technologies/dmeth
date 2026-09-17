#ifndef DMETH_IOCTL_H
#define DMETH_IOCTL_H

#include "dmdrvi_ioctl.h"

/**
 * @brief dmeth-private IOCTL commands
 *
 * dmeth's primary control-plane ioctl surface is the shared
 * DMDRVI_IOCTL_NET_* command set from dmdrvi_ioctl.h (SET/GET_MAC_ADDR,
 * GET_LINK_STATUS, START, STOP) - see dmdrvi's "IMPLEMENTING A NETWORK
 * DRIVER" documentation. These only cover extras that command set doesn't
 * have. Defined (not enumerated) relative to DMDRVI_IOCTL_CUSTOM_BASE,
 * dmdrvi's own reserved range for driver-specific commands - not relative to
 * "DMDRVI_IOCTL_NET_STOP + 1", which would silently collide the next time a
 * new standard DMDRVI_IOCTL_NET_* command is added to dmdrvi_ioctl.h.
 *
 * Kept in its own header (not dmeth_types.h) so dmeth_port.h - which only
 * needs dmeth_instance_t - never has to depend on dmdrvi_ioctl.h.
 * src/dmeth.c (core, where these are dispatched) and tests/dmeth_test.c
 * (the on-target caller) both include this.
 */
#define DMETH_IOCTL_SET_PROMISCUOUS_MODE    (DMDRVI_IOCTL_CUSTOM_BASE + 0)  /**< arg: const bool* */
#define DMETH_IOCTL_GET_PROMISCUOUS_MODE    (DMDRVI_IOCTL_CUSTOM_BASE + 1)  /**< arg: bool* */

/**
 * arg: const dmeth_loopback_mode_t* / dmeth_loopback_mode_t*
 *
 * Exposes dmeth_port_set_loopback_mode() (see dmeth_port.h - "test-only")
 * through the ordinary dmdrvi/devfs path, so an on-target test can exercise
 * MAC/PHY loopback through the real DIF stack (dmdevfs -> dmdrvi -> dmeth
 * core -> dmeth_port) with the board's actual .ini configuration applied -
 * see tests/dmeth_test.c. Must be set before DMDRVI_IOCTL_NET_START, same
 * restriction as dmeth_port_set_loopback_mode() itself.
 */
#define DMETH_IOCTL_SET_LOOPBACK_MODE       (DMDRVI_IOCTL_CUSTOM_BASE + 2)  /**< arg: const dmeth_loopback_mode_t* */
#define DMETH_IOCTL_GET_LOOPBACK_MODE       (DMDRVI_IOCTL_CUSTOM_BASE + 3)  /**< arg: dmeth_loopback_mode_t* */

#endif /* DMETH_IOCTL_H */
