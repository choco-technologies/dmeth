# dmeth Port Layer

`dmeth_port` currently supports `stm32f4` and `stm32f7` - both share the
exact same Ethernet MAC/DMA IP block (every register offset/bit position in
`include/port/stm32_common_regs.h` was cross-checked against both families'
real CMSIS headers and is byte-for-byte identical), so almost all of the
implementation lives in one shared file,
`src/port/stm32_common/stm32_common.c`; each family's `src/port/<family>/
port.c` is a thin `dmod_init()`/`dmod_deinit()` + `DMOD_IRQ_HANDLER` wrapper
around it - the same split `dmfmc` uses for its shared FMC SDRAM controller
(`dmfmc/src/port/stm32_common/`).

## Port Contract (`include/dmeth_port.h`)

```c
dmeth_instance_t dmeth_port_get_instance_count(void);

int  dmeth_port_init(dmeth_instance_t instance, const dmeth_config_t* config);
int  dmeth_port_deinit(dmeth_instance_t instance);

int  dmeth_port_set_mac_address(dmeth_instance_t instance, const uint8_t mac[DMETH_MAC_ADDR_LEN]);
int  dmeth_port_get_mac_address(dmeth_instance_t instance, uint8_t mac[DMETH_MAC_ADDR_LEN]);
int  dmeth_port_start(dmeth_instance_t instance);
int  dmeth_port_stop(dmeth_instance_t instance);
bool dmeth_port_get_link_status(dmeth_instance_t instance);
int  dmeth_port_set_promiscuous_mode(dmeth_instance_t instance, bool enable);

int  dmeth_port_set_loopback_mode(dmeth_instance_t instance, dmeth_loopback_mode_t mode);

int  dmeth_port_transmit_frame(dmeth_instance_t instance, const uint8_t* frame, size_t len);
int  dmeth_port_receive_frame(dmeth_instance_t instance, uint8_t* buffer, size_t size, size_t* received);
```

`_init()` does all hardware bring-up in one call (RMII pin mux select,
MAC/DMA/PHY configuration, descriptor ring allocation) since dmeth's config
is small enough to hand over as a whole struct, unlike `dmuart_port` (one
independent register per setting, because UART has many more orthogonal
knobs). `_transmit_frame()`/`_receive_frame()` are the only two functions
core (`src/dmeth.c`) calls on the data path - core never touches
descriptors, DMA registers, or PHY/MDIO at all.

`_get_instance_count()` is a capability query, not a hardcoded assumption -
`dmeth_dmdrvi_create()` calls it and rejects an out-of-range `instance` from
`.ini` with a clear log message before ever calling `_init()`, the same
reasoning `dmdma_port_get_stream_count()` documents in `dmdma_port.h`. Every
STM32F4/F7 part supported today returns 1 here (`DMETH_MAX_INSTANCES` in
`stm32_common.c`) - there is exactly one ETH peripheral per chip - but core
never hardcodes that number itself.

## RX/TX buffering: zero-copy, DTCM-backed, no cache maintenance

The descriptor ring + packet buffers are owned entirely by the port (not
handed in by core, unlike `dmuart`'s `dm_sw_ring` - Ethernet's RX/TX buffers
*are* the DMA descriptor memory, which is inherently hardware-specific).
Exactly one `memcpy()` happens on each path:

- RX: DMA writes directly into a descriptor's buffer; `_receive_frame()`
  waits on a semaphore posted by the ISR, then does the one `memcpy()` from
  that buffer straight into the caller's buffer (truncating to the caller's
  `size`, like `recvfrom()`), then gives the descriptor back to the DMA.
- TX: `_transmit_frame()` waits for the next descriptor's `OWN` bit to clear,
  `memcpy()`'s the caller's frame into it, then sets `OWN` to hand it to the
  DMA.

Both block indefinitely (`dmdrvi` has no `O_NONBLOCK`/timeout to plumb a
bound through) using `dmosi_semaphore_wait(sem, 1, -1)` for RX and a plain
polling loop for TX (no TX-complete interrupt is enabled, mirroring
dnx-rtos's own minimal ISR - see below).

**Buffers are allocated from the shared `"dma"` `dmheap` context**
(`dmheap_get_context_by_name("dma")` + `dmheap_aligned_alloc(...)`), not a
raw address or a new linker section. On STM32F7 that context is backed by
DTCM (`dmod-boot/configs/mcu/stm32f7/*/mcu.ld`'s `dma` region is the same
address as `dtcm`), which is:

- **Reachable by the Ethernet MAC's own DMA engine** - documented in ST's
  reference manual for the F74x/F75x series ("DTCM-RAM ... accessible by all
  AHB masters from AHB bus Matrix"), and corroborated by dnx-rtos's own
  memory manager (`cpuctl.c`), which registers DTCM as `DMA_CAPABLE`.
- **Outside the CPU's cache hierarchy** - Cortex-M7 TCM is never cached
  (dnx-rtos's memory manager marks DTCM `DMA_CAPABLE` but deliberately
  *not* `CACHEABLE`, unlike SRAM1/SRAM2), so **no `SCB_CleanDCache`/
  `SCB_InvalidateDCache` calls are needed anywhere** in `stm32_common.c`.
  This is a real gap in dnx-rtos's own F7 driver (`eth.c` has an
  unimplemented `// TODO cache clear/invalidate`, because it allocates from
  ordinary - cacheable - heap memory instead); dmeth avoids the problem
  entirely by construction.

On STM32F4 (no cache on Cortex-M4 at all, and `"dma"` there resolves to
ordinary SRAM1 rather than the CCM RAM - CCM is *not* DMA-capable on F4,
confirmed by `dmod-boot/configs/mcu/stm32f4/*/mcu.ld` mapping `dma` to the
same address as `ram`, not `ccm`) there is nothing extra to do either - the
same `dmheap_get_context_by_name("dma")` call just works, with zero
family-specific code in `stm32_common.c`.

Default ring depth is 10 RX + 10 TX descriptors x ~1524 B each (~30 KB
total, `rx_buffer_count`/`tx_buffer_count` in `.ini` - see
`docs/configuration.md`), matching dnx-rtos's own STM32F4x7 ETH driver
default.

## On-target testing: loopback

`dmeth_port_set_loopback_mode()` exists so `tests/dmeth_test.c` can verify
real RX/TX communication without a cable or link partner:

- `dmeth_loopback_mode_mac` sets `MACCR.LM`, looping transmitted frames back
  to the receive path *inside the MAC*, before the RMII pins - exercises the
  DMA descriptors, chained-ring bookkeeping, RX ISR, RX-ready semaphore, and
  the single `memcpy()` on each side, without needing a working PHY chip at
  all.
- `dmeth_loopback_mode_phy` instead sets the PHY's standard `BCR.Loopback`
  bit (IEEE 802.3 clause 22, bit 14 - identical on every PHY, not a
  vendor-specific register) over MDIO, looping *inside the PHY chip*, after
  the RMII pins - additionally exercises the real RMII electrical
  connection and the PHY itself, but needs a real, MDIO-reachable PHY.

`tests/dmeth_test.c` is a `dmod_add_executable()` application (not a
`dmod_add_test()`/`DMOD_TEST_STEP` unit test - it needs `argv` and is meant
to be run manually from the shell), the same shape `dmdma_test_dev.c` uses
for `dmdma`: it opens `/dev/dmeth0` (or whichever instance is passed as
`argv[1]`) through the ordinary VFS file interface (`Dmod_FileOpen`/`_Ioctl`/
`_Write`/`_Read`/`_Close`) and drives it purely through `dmdrvi_ioctl()`
commands - `DMETH_IOCTL_SET_LOOPBACK_MODE` (see `dmeth_ioctl.h`) plus the
standard `DMDRVI_IOCTL_NET_START`/`_STOP`. This needs a fully running system
with `dmeth`+`dmeth_port` already loaded and configured from the board's
actual `eth0.ini` (see `configs/board/`), so a pass confirms that specific
board's real configuration - not a synthetic one - produces working MAC and
PHY loopback. It does real MDIO/PHY-reset/autonegotiation timing (in
`dmeth_port_init()`, underneath `dmeth_dmdrvi_create()`), so it won't run in
a host/simulator build.

## Known bugs (from the two real reference drivers this was built against) avoided here

- **chocos** (`chocos/Source/system/portable/src/st/stm32f7/lld/eth/
  oc_eth_lld.c`): RX-buffer-unavailable recovery pokes `ETH_DMATPDR` (the
  *TX* poll-demand register) instead of `ETH_DMARPDR`. `stm32_common.c` uses
  `DMARPDR` for RX resume.
- **dnx-rtos** (`stm32fx/eth.c`): `ETH_EXTERN_GetSpeedAndDuplex()` is a
  no-op stub - the PHY's resolved speed/duplex after autonegotiation is
  silently discarded, leaving MACCR at whatever pre-autoneg values were
  configured. `phy_reset_and_autonegotiate()` in `stm32_common.c` reads the
  PHY's vendor-specific status register and updates `MACCR.FES`/`MACCR.DM`
  - though see the comment there: the exact bit polarity for this is
  inferred from dnx-rtos's own (unused) Configtool metadata for the
  LAN8742A, not independently verified against that chip's datasheet.
- **dnx-rtos**: F7 buffers live in ordinary (cacheable) heap memory with an
  unimplemented cache-maintenance TODO. See "RX/TX buffering" above for how
  dmeth avoids this by construction instead.

## Adding Another Architecture

1. Create `src/port/<family>/config.cmake`, setting `DMOD_TOOLS_NAME` for the
   target architecture (see `src/port/stm32f7/config.cmake` for the pattern -
   it must match a directory under `dmod/configs/arch/...`).
2. If the peripheral IP is identical to an already-supported family (true of
   every STM32 F2/F4/F7 part with an Ethernet MAC), just add a thin
   `src/port/<family>/port.c` with `dmod_init`/`dmod_deinit` +
   `DMOD_IRQ_HANDLER(ETH_IRQn) { stm32_eth_irq_handler(); }` - no new logic
   needed, following `src/port/stm32f4/port.c`/`src/port/stm32f7/port.c`.
3. Otherwise, implement the `dmod_dmeth_port_api_declaration(...)` functions
   declared in `include/dmeth_port.h` directly in `src/port/<family>/port.c`.
4. Build by selecting the new family: `cmake .. -DDMOD_CPU_FAMILY=<family>`.
5. Do not introduce a module-specific variable (e.g. `<MODULE>_MCU_SERIES`) for
   this - `DMOD_CPU_FAMILY` is the ecosystem-wide convention, already wired
   into `dmf-get` package resolution.
