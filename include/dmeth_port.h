#ifndef DMETH_PORT_H
#define DMETH_PORT_H

#include "dmod_types.h"
#include "dmeth_port_defs.h"
#include "dmeth_types.h"

/* --- Capability query ---
 *
 * Let the arch-independent core (dmeth_dmdrvi_create()) discover how many
 * ETH peripherals this chip/family actually has, instead of assuming "just
 * one" - same reasoning as dmdma_port_get_stream_count() in dmdma_port.h.
 * Every STM32F4/F7 part supported today only has one, but core should
 * validate `config->instance` against this rather than relying on
 * dmeth_port_init() to reject an out-of-range instance number with no
 * context on *why*.
 */

dmod_dmeth_port_api(1.0, dmeth_instance_t, _get_instance_count, ( void ) );

/* --- Lifecycle ---
 *
 * dmeth_port_init() does everything hardware-specific in one call: RMII pin
 * mux selection, MAC/DMA/PHY bring-up, and allocating the RX/TX descriptor
 * ring + packet buffers from the shared "dma" dmheap context (DTCM-backed,
 * DMA-capable, non-cacheable - see docs/port-implementation.md). Unlike
 * dmuart_port (one independent register per setting), dmeth's config is
 * small enough that core hands over the whole dmeth_config_t at once.
 */

dmod_dmeth_port_api(1.0, int,  _init,   ( dmeth_instance_t instance, const dmeth_config_t* config ) );
dmod_dmeth_port_api(1.0, int,  _deinit, ( dmeth_instance_t instance ) );

/* --- Device identity / control plane ---
 *
 * Mirrors the DMDRVI_IOCTL_NET_* command set 1:1 - core's _ioctl() forwards
 * each command straight to one of these.
 */

dmod_dmeth_port_api(1.0, int,  _set_mac_address, ( dmeth_instance_t instance, const uint8_t mac[DMETH_MAC_ADDR_LEN] ) );
dmod_dmeth_port_api(1.0, int,  _get_mac_address, ( dmeth_instance_t instance, uint8_t mac[DMETH_MAC_ADDR_LEN] ) );
dmod_dmeth_port_api(1.0, int,  _start, ( dmeth_instance_t instance ) );
dmod_dmeth_port_api(1.0, int,  _stop,  ( dmeth_instance_t instance ) );
dmod_dmeth_port_api(1.0, bool, _get_link_status, ( dmeth_instance_t instance ) );
dmod_dmeth_port_api(1.0, int,  _set_promiscuous_mode, ( dmeth_instance_t instance, bool enable ) );

/* --- Loopback (test-only) ---
 *
 * Reachable from an on-target test (tests/dmeth_test.c) via core's
 * DMETH_IOCTL_SET_LOOPBACK_MODE (dmeth_ioctl.h), to verify RX/TX
 * communication - transmit a known frame, receive it back, compare - without
 * a cable or link partner. Must be set before dmeth_port_start().
 */

dmod_dmeth_port_api(1.0, int, _set_loopback_mode, ( dmeth_instance_t instance, dmeth_loopback_mode_t mode ) );

/* --- Data plane ---
 *
 * Exactly one memcpy each: DMA rx buffer -> caller's buffer, or caller's
 * buffer -> DMA tx buffer. Core (dmeth.c) never touches descriptors
 * directly.
 *
 * Both block by default - _receive_frame() waits on an internal semaphore
 * posted from the RX ISR, _transmit_frame() waits for a free TX descriptor -
 * which is what a network interface's own RX thread wants. Since dmdrvi has
 * no O_NONBLOCK to plumb through, dmeth_port_set_io_timeout() is how a
 * caller that must not wait forever (a diagnostic that has to *report* "no
 * frame came back", like tests/dmeth_test.c) puts a bound on that wait;
 * both then return -ETIMEDOUT once it expires. 0 (the default) restores
 * "wait indefinitely".
 */

dmod_dmeth_port_api(1.0, int, _set_io_timeout, ( dmeth_instance_t instance, uint32_t timeout_ms ) );
dmod_dmeth_port_api(1.0, int, _transmit_frame, ( dmeth_instance_t instance, const uint8_t* frame, size_t len ) );
dmod_dmeth_port_api(1.0, int, _receive_frame,  ( dmeth_instance_t instance, uint8_t* buffer, size_t size, size_t* received ) );

#endif // DMETH_PORT_H
