# dmeth

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

`dmeth` is a hardware-abstracted Ethernet MAC driver module for the
[DMOD](https://github.com/choco-technologies/dmod) ecosystem.

## Description

`dmeth` follows the standard `dmdrvi` driver pattern used throughout DMOD
(same as `dmuart`, `dmgpio`, ...): instead of exposing its own bespoke API,
it registers a plain device file (`/dev/dmeth0`, `/dev/dmeth1`, ...) that any
application drives with ordinary `open`/`read`/`write`/`ioctl` calls. In
practice that application is a TCP/IP stack (e.g. lwIP) running as a
separate `network` module - `dmdevfs` and `dmdrvi` needed no changes to
support Ethernet, the generic driver interface was already general enough.

- One `write()` call transmits exactly one raw Ethernet frame; one `read()`
  call receives exactly one. `dmeth` never inspects frame contents
  (destination/source MAC, ethertype, payload) - it is a byte-copying pipe
  between the caller's buffer and the MAC's DMA descriptor memory.
- Control plane (link status, MAC address, promiscuous mode, start/stop)
  goes through the shared `DMDRVI_IOCTL_NET_*` command set, plus two
  `dmeth`-private ioctls for promiscuous mode.
- Configuration (which ETH instance, RX/TX buffer counts, static MAC
  address, PHY MDIO address, ...) is read from an `.ini` section with
  `driver_name=dmeth`, resolved automatically by `dmdevfs`.
- `dmeth` registers itself with [`dmnetif`](https://github.com/choco-technologies/dmnetif)
  as soon as its device path is known - no separate setup step needed. A
  TCP/IP stack or CLI tool (`ifconfig`, ...) can then address it by name
  (`"eth0"`, `"eth1"`, ...) without ever knowing the underlying `/dev/dmethN`
  path or `dmdrvi` ioctl commands exist. See "Self-registration with
  dmnetif" below.
- Two on-target loopback modes (MAC-internal and PHY-internal) let a test
  exercise the full TX/RX path without a cable or link partner.

See [docs/dmeth.md](docs/dmeth.md) for the full architecture/data-path
writeup, and [docs/api-reference.md](docs/api-reference.md) for the
complete ioctl/API surface.

### Architecture

```
┌──────────────────────────────────────┐
│  TCP/IP stack (networkd) / ifconfig  │
├──────────────────────────────────────┤
│               DMNETIF                │
│   named interface ("eth0"), up/down, │
│   link status, send/receive a frame  │
├──────────────────────────────────────┤
│         DMDRVI Interface             │
│  (open/close/read/write/ioctl/stat)  │
├──────────────────────────────────────┤
│           DMETH Core                 │
│ (config parsing, ioctl dispatch,     │
│  registers itself with dmnetif)      │
├──────────────────────────────────────┤
│        DMETH Port Layer              │
│ (MAC/DMA/PHY register access,        │
│  descriptor rings, RX/TX ISR)        │
├──────────────────────────────────────┤
│      Hardware (Ethernet MAC)         │
└──────────────────────────────────────┘
```

### Self-registration with dmnetif

`dmeth` does not just expose `/dev/dmethN` and stop there - its
`dmdrvi_path_ready()` implementation (`src/dmeth.c`) calls
`dmnetif_register("eth<instance>", path)` as soon as `dmdevfs` hands it the
device's final path, and `dmnetif_unregister()` on teardown
(`dmdrvi_free()`). This is the earliest point a `dmdrvi` driver *can*
register - `dmdevfs` has not necessarily created the device node yet inside
`dmod_init()`/`dmdrvi_create()`.

Practical effect: once a `dmeth` instance configured in `.ini` comes up,
`"eth0"` (or `"eth1"`, ...) exists as a named network interface with no
extra wiring - `networkd`, `ifconfig`, or any other `dmnetif` consumer can
address it immediately without knowing it is backed by `/dev/dmeth0` or that
`dmeth` exists at all. `dmnetif` itself opens that device path and drives it
through the same `dmdrvi` `read`/`write`/`ioctl` contract described above -
`dmeth` never opens its own device file.

RX/TX descriptor rings and packet buffers are allocated from the shared
`"dma"` `dmheap` context (not ordinary heap memory) at `dmeth_port_init()`
time, so on STM32F7 they land in DTCM: reachable by the Ethernet MAC's own
DMA engine and outside the CPU's cache hierarchy, so no cache-maintenance
calls are needed anywhere in the port layer. See
[docs/port-implementation.md](docs/port-implementation.md) for the full
reasoning, the known bugs from reference drivers this design avoids, and how
to add another MCU family.

## Building

### Using CMake

```bash
mkdir -p build
cd build
cmake ..
cmake --build .
```

Pass `-DDMOD_DIR=/path/to/local/dmod` to build against a local dmod checkout
instead of fetching `develop` from GitHub.

### Using Make

```bash
make DMOD_MODE=DMOD_MODULE DMOD_DIR=/path/to/dmod
```

## Configuration

`dmeth` is configured from an `.ini` section carrying `driver_name=dmeth`
(named anything - `[dmeth]`, `[eth0]`, ...), plus one `dmgpio` section per
RMII signal so `dmdevfs` can bring up both the MAC and its pins together:

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

| Parameter          | Type    | Default | Description                                                              |
|---------------------|---------|---------|---------------------------------------------------------------------------|
| `instance`          | integer | 0       | Which ETH peripheral (0-based; only one exists per chip today)            |
| `rx_buffer_count`   | integer | 10      | Number of RX descriptors/buffers (~1524 B each)                           |
| `tx_buffer_count`   | integer | 10      | Number of TX descriptors/buffers (~1524 B each)                           |
| `mac_address`       | string  | (none)  | Optional static `AA:BB:CC:DD:EE:FF` MAC, applied on `DMDRVI_IOCTL_NET_START` |
| `promiscuous`       | string  | "off"   | `"on"`/`"off"` - start with the MAC promiscuous filter enabled            |
| `phy_address`       | integer | 0       | MDIO address of the PHY chip (board-specific, usually 0 or 1)             |

If `mac_address` is omitted, it must be set at runtime via
`DMDRVI_IOCTL_NET_SET_MAC_ADDR` before `DMDRVI_IOCTL_NET_START`.

Ready-made configs live under `configs/`:

- `configs/mcu/*.ini` - generic per-MCU defaults, no GPIO pin setup.
- `configs/board/*/eth0.ini` - full board configs including the RMII
  `dmgpio` pin sections (32F746G-DISCOVERY, STM32F769I-DISCOVERY,
  NUCLEO-F767ZI, NUCLEO-F429ZI - all cross-checked against a real reference
  board-support file, see `configs/README.md`).

See [docs/configuration.md](docs/configuration.md) and
[configs/README.md](configs/README.md) for details.

## Usage

`dmeth` has no Built-in API of its own - `dmeth.h` only re-exports the
config struct. All access goes through `dmdrvi`:

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

See [docs/api-reference.md](docs/api-reference.md) for the full list of
`DMDRVI_IOCTL_NET_*` and `DMETH_IOCTL_*` commands.

## Hardware Port

This repository ships two DMOD modules: the architecture-independent
`dmeth` (config parsing, ioctl dispatch, `dmdrvi`/`dmnetif` registration) and
`dmeth_port` (MAC/DMA/PHY register access, RMII pin setup, descriptor rings,
RX/TX interrupt handling). The active architecture is selected via
`DMOD_CPU_FAMILY` (default: `stm32f7`):

```bash
cmake .. -DDMOD_CPU_FAMILY=stm32f7
```

Currently supported families - `stm32f4` and `stm32f7` - share the exact
same Ethernet MAC/DMA IP block, so almost all of the port implementation
lives in one shared file (`src/port/stm32_common/`); each family's
`src/port/<family>/port.c` is a thin lifecycle + IRQ wrapper around it. See
[docs/port-implementation.md](docs/port-implementation.md) for how to add
another architecture. Port-specific files:

```
├── include/dmeth_port.h
├── include/port/
│   ├── stm32_common_regs.h
│   ├── stm32f4_regs.h
│   └── stm32f7_regs.h
├── src/port/
│   ├── CMakeLists.txt
│   ├── stm32_common/
│   │   ├── stm32_common.h
│   │   └── stm32_common.c
│   ├── stm32f4/
│   │   ├── config.cmake
│   │   └── port.c
│   └── stm32f7/
│       ├── config.cmake
│       └── port.c
└── dmeth_port.dmr
```

## Testing

`tests/dmeth_test.c` is a `dmod_add_executable()` application
(`dmeth_test /dev/dmeth0`) that opens the real device node through the
ordinary VFS interface and drives it via `dmdrvi_ioctl()` (including
`DMETH_IOCTL_SET_LOOPBACK_MODE`), exercising the full
`dmdevfs`/`dmdrvi`/`dmeth` stack with the board's actual `.ini`
configuration applied: transmit a known frame under MAC-internal loopback,
receive it back, compare byte-for-byte, then repeat under PHY-internal
loopback. A pass confirms that specific board's real configuration (not a
synthetic one) works. It does real MDIO/PHY-reset/autonegotiation timing,
so it only runs on real hardware, not in a host/simulator build.

The frame it sends is a well-formed Ethernet frame addressed to the
interface's own MAC, because a loopback frame still goes through the MAC's
address filter on the way back in; and it bounds `read()`/`write()` with
`DMETH_IOCTL_SET_IO_TIMEOUT` first, so a loopback that never round-trips
fails the test rather than hanging the shell that started it. Each loopback
mode round-trips two frame shapes - an 802.3 length frame and an Ethernet II
type frame - because the MAC's FCS stripping treats them differently, and
only the second is what real traffic looks like.

## Dependencies

- `dmdrvi` - DMOD Driver Interface (device file, read/write/ioctl)
- `dmini` - INI configuration parser
- `dmosi` - semaphore (RX-ready signaling from ISR) and minimum interrupt priority query
- `dmclk_port` - runtime clock query (MDC divisor) and microsecond delays
- `dmheap` - the shared `"dma"` heap context RX/TX buffers are allocated from
- `dmnetif` - registers the interface (`eth0`, `eth1`, ...) once its device path is known

## Documentation

See the `docs/` directory:

- **[dmeth.md](docs/dmeth.md)** - Overview and architecture
- **[api-reference.md](docs/api-reference.md)** - DMDRVI contract, ioctl commands, port layer summary
- **[configuration.md](docs/configuration.md)** - `.ini` configuration fields and examples
- **[port-implementation.md](docs/port-implementation.md)** - Port layer contract, buffering design, adding a new MCU family

View documentation using `dmf-man`:

```bash
dmf-man dmeth                     # Main documentation
dmf-man dmeth api-reference        # API reference
dmf-man dmeth configuration        # Configuration guide
dmf-man dmeth port-implementation  # Port layer contract
```

## Project Structure

```
dmeth/
├── configs/                  # Ready-made per-MCU / per-board .ini configs
│   ├── board/
│   └── mcu/
├── docs/                      # Documentation (markdown format)
├── examples/
│   └── config.ini
├── include/
│   ├── dmeth.h                # Public header (re-exports dmeth_config_t)
│   ├── dmeth_types.h           # dmeth_config_t, dmeth_loopback_mode_t
│   ├── dmeth_ioctl.h           # dmeth-private ioctl commands
│   ├── dmeth_port.h            # Port layer contract
│   └── port/                   # Shared/per-family register definitions
├── src/
│   ├── dmeth.c                 # Core: config parsing, dmdrvi DIF, ioctl dispatch
│   └── port/
│       ├── stm32_common/       # Shared MAC/DMA/PHY implementation
│       ├── stm32f4/
│       └── stm32f7/
├── tests/
│   ├── CMakeLists.txt
│   └── dmeth_test.c            # On-target loopback test (device-node, real .ini config)
├── CMakeLists.txt
├── Makefile
├── dmeth.dmr
├── dmeth_port.dmr
├── test_dmeth.dmr
└── manifest.dmm
```

## Author

Patryk Kubiak

## License

MIT
