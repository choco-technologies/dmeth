# DMETH Configuration Files

This directory contains pre-configured Ethernet settings for various development boards and MCUs.

## Directory Structure

```
configs/
├── board/                          # Board-specific configurations
│   ├── stm32f746g-disco/           # 32F746G-DISCOVERY
│   │   └── eth0.ini
│   ├── stm32f769i-discovery/       # STM32F769I-DISCOVERY
│   │   └── eth0.ini
│   ├── nucleo-f767zi/              # NUCLEO-F767ZI
│   │   └── eth0.ini
│   └── nucleo-f429zi/              # NUCLEO-F429ZI
│       └── eth0.ini
└── mcu/                            # MCU-specific configurations (generic defaults, no GPIO)
    ├── stm32f407vg.ini              # STM32F4 family
    ├── stm32f429zi.ini              # STM32F4 family
    ├── stm32f746zg.ini              # STM32F7 family
    ├── stm32f767zi.ini              # STM32F7 family
    └── stm32f769ni.ini              # STM32F7 family
```

## Configuration Format

Each board Ethernet configuration file contains:
- One GPIO section per RMII signal (`driver_name=dmgpio`, alternate function 11)
- One `[eth0]` section with `driver_name=dmeth` and the Ethernet parameters

This allows `dmdevfs` to automatically configure both the MAC and its RMII pins.

### Fields

| Key                | Meaning                                                              |
|--------------------|-----------------------------------------------------------------------|
| `instance`         | Which ETH peripheral (0-based; only one exists per chip today)        |
| `rx_buffer_count`  | Number of RX descriptors/buffers (default 10, ~1524 B each)            |
| `tx_buffer_count`  | Number of TX descriptors/buffers (default 10, ~1524 B each)            |
| `mac_address`      | Optional static `AA:BB:CC:DD:EE:FF` MAC, applied on `DMDRVI_IOCTL_NET_START` |
| `promiscuous`      | `on`/`off` - start with the MAC promiscuous filter enabled             |
| `phy_address`      | MDIO address of the PHY chip (board-specific, usually 0 or 1)          |

### Example (board/stm32f746g-disco/eth0.ini)

```ini
[eth_ref_clk]
driver_name=dmgpio
pin=PA1
mode=alternate
alternate_function=11
speed=maximum
output_circuit=push_pull
pull=none

; ... one such section per RMII signal (MDIO, CRS_DV, MDC, RXD0/1, TX_EN,
;     TXD0/1, RXER) ...

[eth0]
driver_name=dmeth
instance=0
rx_buffer_count=10
tx_buffer_count=10
mac_address=02:00:00:00:00:01
promiscuous=off
phy_address=0
```

## Board Configurations

| Board                   | Folder                          | PHY      | TXD1 pin | Notes                                   |
|--------------------------|----------------------------------|----------|----------|--------------------------------------------|
| 32F746G-DISCOVERY        | `board/stm32f746g-disco/`        | LAN8742A | PG14     | Confirmed against ChocoOS's own board config for this exact board (`oc_pinsmap.h`) |
| STM32F769I-DISCOVERY     | `board/stm32f769i-discovery/`    | LAN8742A | PG14     | Confirmed against Zephyr's board support file (`boards/st/stm32f769i_disco/stm32f769i_disco.dts`, `&mac`/`&mdio` `pinctrl-0`) |
| NUCLEO-F767ZI            | `board/nucleo-f767zi/`           | LAN8742A | **PB13** | Confirmed against Zephyr's board support file (`boards/st/nucleo_f767zi/nucleo_f767zi.dts`) |
| NUCLEO-F429ZI            | `board/nucleo-f429zi/`           | LAN8742A | **PB13** | Confirmed against Zephyr's board support file (`boards/st/nucleo_f429zi/nucleo_f429zi.dts`) - same MB1137 Nucleo-144 layout as NUCLEO-F767ZI |

**TXD1 genuinely differs between board families** - Discovery-style boards use
`PG14`, Nucleo-144 boards use `PB13` - confirmed by direct comparison across
the four boards above. This is exactly why boards aren't added here by
copying a similar-looking one: an initial pass assumed one universal ST RMII
pinout across all these boards (matching the other 8 pins, which *are*
identical everywhere), and that assumption was wrong for this one pin.

Only pinouts that could be cross-checked against a real, known-good reference
(a board support file actually used to run code on that exact hardware, or a
board-specific pin table pulled from another real driver targeting it) are
included here - adding another board means confirming its RMII pin
assignment against its own schematic/board-support-file/reference manual
first, not guessing from a similar-looking board.

## MCU Configurations

| MCU         | File                   | Family  |
|-------------|------------------------|---------|
| STM32F407VG | `mcu/stm32f407vg.ini`  | stm32f4 |
| STM32F429ZI | `mcu/stm32f429zi.ini`  | stm32f4 |
| STM32F746ZG | `mcu/stm32f746zg.ini`  | stm32f7 |
| STM32F767ZI | `mcu/stm32f767zi.ini`  | stm32f7 |
| STM32F769NI | `mcu/stm32f769ni.ini`  | stm32f7 |
