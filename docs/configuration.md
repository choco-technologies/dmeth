# DMETH Configuration Guide

## Configuration File Format

DMETH uses INI-format configuration files parsed by the DMINI module. A
board config file typically contains one section with `driver_name=dmeth`
(named anything - `[dmeth]`, `[eth0]`, ...) plus one `dmgpio` section per
RMII signal - see `configs/README.md`.

Every section that declares `driver_name=dmeth` must include it, even if the
section is also named `[dmeth]` - `dmdevfs` finds candidate sections by
scanning for that key (`collect_section_driver_configs()` in
`dmdevfs.c`), not by section name. Once found, `dmdevfs` locks the `.ini`
context to that one section via `dmini_set_active_section()` before calling
into the driver, which is why the driver itself never needs to know or
guess the section's name - see `src/dmeth.c`'s `read_config_parameters()`.

## Configuration Parameters

All parameters are read from the section `dmdevfs` resolves for this driver
(the one with `driver_name=dmeth` - named `[dmeth]`, or a device-named
section like `[eth0]`, see `configs/board/stm32f746g-disco/eth0.ini` for an
example of the latter):

| Parameter          | Type    | Default | Description                                                              |
|---------------------|---------|---------|---------------------------------------------------------------------------|
| `instance`          | integer | 0       | Which ETH peripheral (0-based; only one exists per chip today)            |
| `rx_buffer_count`   | integer | 10      | Number of RX descriptors/buffers (~1524 B each)                           |
| `tx_buffer_count`   | integer | 10      | Number of TX descriptors/buffers (~1524 B each)                           |
| `mac_address`       | string  | (none)  | Optional static `AA:BB:CC:DD:EE:FF` MAC, applied on `DMDRVI_IOCTL_NET_START` |
| `promiscuous`       | string  | "off"   | `"on"`/`"off"` - start with the MAC promiscuous filter enabled            |
| `phy_address`       | integer | 0       | MDIO address of the PHY chip (board-specific, usually 0 or 1)             |

If `mac_address` is not set here, the MAC address must be provided at
runtime via `DMDRVI_IOCTL_NET_SET_MAC_ADDR` before
`DMDRVI_IOCTL_NET_START` - the driver applies no default of its own.

## Examples

### Minimal (examples/config.ini)

```ini
[dmeth]
driver_name=dmeth
instance=0
rx_buffer_count=10
tx_buffer_count=10
mac_address=02:00:00:00:00:01
promiscuous=off
phy_address=0
```

### Board config with RMII pins

See `configs/board/stm32f746g-disco/eth0.ini` for a complete example that
also configures the RMII GPIO pins via `dmgpio` sections.
