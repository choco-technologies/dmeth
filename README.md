# dmeth

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

dmeth DMOD library module.

## Description

TODO: describe what this module does.

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

## Usage

This library module provides functions that can be used by other modules:

```c
#include "dmeth.h"
```

## Documentation

See the `docs/` directory:

- **[api-reference.md](docs/api-reference.md)** - Complete API documentation

View documentation using `dmf-man dmeth`.

## Hardware Port

This module ships two DMOD modules: the architecture-independent
`dmeth` and `dmeth_port`, which contains the
architecture-specific implementation. The active architecture is selected via
`DMOD_CPU_FAMILY` (default: `stm32f7`):

```bash
cmake .. -DDMOD_CPU_FAMILY=stm32f7
```

See [docs/port-implementation.md](docs/port-implementation.md) for how to add
another architecture. Port-specific files:

```
├── include/dmeth_port.h
├── src/port/
│   ├── CMakeLists.txt
│   └── stm32f7/
│       ├── config.cmake
│       └── port.c
└── dmeth_port.dmr
```
## Project Structure

```
dmeth/
├── docs/              # Documentation (markdown format)
├── include/           # Public headers
│   └── dmeth.h
├── src/
│   └── dmeth.c
├── tests/
│   ├── CMakeLists.txt
│   └── dmeth_test.c
├── CMakeLists.txt
├── Makefile
├── dmeth.dmr
└── manifest.dmm
```

## Author

Patryk Kubiak

## License

MIT
