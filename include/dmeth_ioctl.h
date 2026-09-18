#ifndef DMETH_IOCTL_H
#define DMETH_IOCTL_H

#include "dmdrvi_ioctl.h"
#include <stdint.h>

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

/**
 * arg: const uint32_t* / uint32_t*
 *
 * Upper bound, in milliseconds, on how long a read() or write() on this
 * device may block - read() waiting for a frame to arrive, write() waiting
 * for a free TX descriptor. 0 (the default) means "block until it happens",
 * which is what a network interface's own RX thread wants: a frame that has
 * not arrived yet is not an error to it.
 *
 * A caller that has to *report* "nothing came back" rather than wait for it
 * sets a bound here first - tests/dmeth_test.c does, so a loopback that
 * does not round-trip fails the test instead of hanging the shell that
 * started it. On expiry read()/write() return 0 (bytes transferred), the
 * same way a short transfer is reported.
 */
#define DMETH_IOCTL_SET_IO_TIMEOUT          (DMDRVI_IOCTL_CUSTOM_BASE + 4)  /**< arg: const uint32_t* (milliseconds, 0 = block forever) */
#define DMETH_IOCTL_GET_IO_TIMEOUT          (DMDRVI_IOCTL_CUSTOM_BASE + 5)  /**< arg: uint32_t* (milliseconds, 0 = block forever) */

#endif /* DMETH_IOCTL_H */
