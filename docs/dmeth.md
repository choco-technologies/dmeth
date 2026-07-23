# DMETH - DMOD Ethernet MAC Driver Module

## Overview

DMETH is a DMOD module providing a hardware-abstracted Ethernet MAC driver.
Like every other `dmdrvi` driver, it shows up as an ordinary device file
(`/dev/dmeth0`) rather than exposing dedicated `SendFrame`/`ReceiveFrame`
functions - a TCP/IP stack (lwIP, in a separate `network` module/service)
opens it and drives it with plain `fopen`/`fread`/`fwrite`, exactly like any
other application. `dmdevfs` and `dmdrvi` needed no changes to support this;
the generic driver interface was already general enough.

## Architecture

```
┌──────────────────────────────────────┐
│     Application / TCP-IP stack       │
├──────────────────────────────────────┤
│         DMDRVI Interface             │
│  (open/close/read/write/ioctl/stat)  │
├──────────────────────────────────────┤
│           DMETH Core                 │
│ (config parsing, ioctl dispatch)     │
├──────────────────────────────────────┤
│        DMETH Port Layer              │
│ (MAC/DMA/PHY register access,        │
│  descriptor rings, RX/TX ISR)        │
├──────────────────────────────────────┤
│      Hardware (Ethernet MAC)         │
└──────────────────────────────────────┘
```

## Data path

One `dmdrvi_write()` call transmits exactly one Ethernet frame; one
`dmdrvi_read()` call receives exactly one frame. The buffer is the full raw
frame (destination MAC, source MAC, ethertype, payload) - dmeth never
touches the header, it is a byte-copying pipe between the caller's buffer
and the MAC's DMA descriptor memory. See `docs/api-reference.md`.

## Buffering

RX/TX descriptor rings and packet buffers are allocated from the shared
`"dma"` `dmheap` context at `dmeth_port_init()` time - not from ordinary heap
memory - because on STM32F7 that context is backed by DTCM, which is both
reachable by the Ethernet MAC's own DMA engine and outside the CPU's cache
hierarchy (no cache maintenance needed). See `docs/port-implementation.md`
for the full reasoning and references.

## Device Path

The Ethernet MAC is registered as `/dev/dmeth<N>` where `N` is the major
number (`instance` in the `.ini` config) - there is no minor-number concept
for a plain network interface (see `dmdrvi`'s "IMPLEMENTING A NETWORK
DRIVER" documentation).

## Dependencies

- `dmdrvi` - DMOD Driver Interface (device file, read/write/ioctl)
- `dmini` - INI configuration parser
- `dmosi` - semaphore (RX-ready signaling from ISR) and minimum interrupt priority query
- `dmclk_port` - runtime clock query (MDC divisor) and microsecond delays
- `dmheap` - the shared `"dma"` heap context RX/TX buffers are allocated from
