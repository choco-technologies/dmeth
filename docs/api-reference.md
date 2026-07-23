# dmeth API Reference

dmeth exposes no `dmod_dmeth_api(...)` Built-in API of its own - every
external access goes through the generic `dmdrvi` interface
(`include "dmdrvi.h"`), same as `dmuart`/`dmgpio`. This document only covers
what dmeth adds on top of the generic `dmdrvi` contract: the network ioctl
commands, the config struct, and the port layer contract.

## DMDRVI Interface Functions

dmeth implements the standard `dmdrvi` DIF (`_create`/`_free`/`_open`/
`_close`/`_read`/`_write`/`_ioctl`/`_flush`/`_stat`) - see `dmdrvi/docs/
dmdrvi.md` for the full generic contract. Notable dmeth-specific behavior:

- `dmdrvi_create()` assigns `dev_num->flags = DMDRVI_NUM_MAJOR` and
  `dev_num->major = <instance>` -> `/dev/dmeth0`, `/dev/dmeth1`, ... (one
  major number per MAC peripheral, no minor-number concept - see `dmdrvi`'s
  "IMPLEMENTING A NETWORK DRIVER" section).
- `dmdrvi_read()`/`dmdrvi_write()` transfer exactly one frame per call and
  return `0` if the interface hasn't been started yet
  (`DMDRVI_IOCTL_NET_START` not yet applied) - matching the convention
  `dmdrvi`'s own network-driver docs recommend. Both block indefinitely
  while waiting (no `O_NONBLOCK`/timeout - `dmdrvi` has no such concept).
- `dmdrvi_flush()` is a no-op (0) - frames are already fully handed to the
  DMA by the time `dmdrvi_write()` returns, nothing buffered at this layer.
- `dmdrvi_stat()` reports `size=0` (stream-like device), `mode=0666`.

## IOCTL Commands

### Network commands (shared, from `dmdrvi_ioctl.h`)

| Command                            | `arg` direction | `arg` type                     | Description                    |
|-------------------------------------|------------------|----------------------------------|---------------------------------|
| `DMDRVI_IOCTL_NET_SET_MAC_ADDR`     | in               | `const dmdrvi_net_mac_addr_t*`  | Set the device MAC address (also cached and re-applied on the next START) |
| `DMDRVI_IOCTL_NET_GET_MAC_ADDR`     | out              | `dmdrvi_net_mac_addr_t*`        | Read the current MAC address (from hardware registers) |
| `DMDRVI_IOCTL_NET_GET_LINK_STATUS`  | out              | `dmdrvi_net_link_status_t*`     | Read the current link state (polls the PHY over MDIO) |
| `DMDRVI_IOCTL_NET_START`            | -                | `NULL`                          | Apply any cached MAC address, then enable packet reception/transmission |
| `DMDRVI_IOCTL_NET_STOP`             | -                | `NULL`                          | Stop the interface                |

### dmeth-private commands (from `dmeth_ioctl.h`)

Defined relative to `DMDRVI_IOCTL_CUSTOM_BASE` (`dmdrvi_ioctl.h`'s reserved
range for driver-specific commands), not "last standard command + 1" - that
would silently collide the next time a new `DMDRVI_IOCTL_NET_*` command is
added. Both command sets are dispatched through the same `dmdrvi_ioctl()`
switch - see the comment in `dmeth_ioctl.h`.

| Command                                  | `arg` direction | `arg` type    | Description                     |
|--------------------------------------------|------------------|-----------------|------------------------------------|
| `DMETH_IOCTL_SET_PROMISCUOUS_MODE`         | in               | `const bool*`  | Enable/disable the MAC's promiscuous filter |
| `DMETH_IOCTL_GET_PROMISCUOUS_MODE`         | out              | `bool*`        | Read the current promiscuous filter state   |

### Bring-up sequence

Same as `dmdrvi`'s documented network driver sequence:

```c
#include "dmdrvi.h"
#include "dmdrvi_ioctl.h"

void* handle = dmdrvi_open(ctx, DMDRVI_O_RDWR, &dev_num);

dmdrvi_net_mac_addr_t mac = { .addr = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x01 } };
dmdrvi_ioctl(ctx, handle, DMDRVI_IOCTL_NET_SET_MAC_ADDR, &mac);
dmdrvi_ioctl(ctx, handle, DMDRVI_IOCTL_NET_START, NULL);

dmdrvi_net_link_status_t link;
dmdrvi_ioctl(ctx, handle, DMDRVI_IOCTL_NET_GET_LINK_STATUS, &link);

if (link == DMDRVI_NET_LINK_UP) {
    uint8_t frame[64] = { /* ... */ };
    dmdrvi_write(ctx, handle, frame, sizeof(frame), 0);

    uint8_t rx_buffer[1518];
    size_t received = dmdrvi_read(ctx, handle, rx_buffer, sizeof(rx_buffer), 0);
}

dmdrvi_ioctl(ctx, handle, DMDRVI_IOCTL_NET_STOP, NULL);
dmdrvi_close(ctx, handle);
```

## Configuration Structure

```c
typedef struct
{
    dmeth_instance_t instance;
    uint8_t          mac_address[DMETH_MAC_ADDR_LEN];
    bool             mac_address_set;
    uint16_t         rx_buffer_count;
    uint16_t         tx_buffer_count;
    bool             promiscuous;
    uint8_t          phy_address;
} dmeth_config_t;
```

See `docs/configuration.md` for the `.ini` field mapping.

## Port Layer API

See `docs/port-implementation.md` for the full contract and design
rationale. Summary (`include/dmeth_port.h`):

| Function                              | Purpose                                                    |
|-----------------------------------------|--------------------------------------------------------------|
| `dmeth_port_get_instance_count()`      | How many ETH peripherals this chip/family has (capability query, core validates `config->instance` against it) |
| `dmeth_port_init(instance, config)`     | RMII/MAC/DMA/PHY bring-up, allocate descriptor rings          |
| `dmeth_port_deinit(instance)`           | Tear down, free descriptor rings                              |
| `dmeth_port_set_mac_address(instance, mac)` | Program the MAC address filter                            |
| `dmeth_port_get_mac_address(instance, mac)` | Read the MAC address filter                                |
| `dmeth_port_start(instance)`            | Enable MAC TX/RX + DMA TX/RX                                  |
| `dmeth_port_stop(instance)`             | Disable MAC TX/RX + DMA TX/RX                                 |
| `dmeth_port_get_link_status(instance)`  | Poll the PHY's link status bit over MDIO                      |
| `dmeth_port_set_promiscuous_mode(instance, enable)` | Toggle the MAC's promiscuous filter               |
| `dmeth_port_transmit_frame(instance, frame, len)` | Block until a TX descriptor is free, send one frame  |
| `dmeth_port_receive_frame(instance, buffer, size, *received)` | Block until a frame is ready, copy it out |
