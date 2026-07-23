# dmeth Documentation

Welcome to the dmeth module documentation.

## Contents

- **[dmeth.md](dmeth.md)** - Overview and architecture
- **[api-reference.md](api-reference.md)** - DMDRVI contract, ioctl commands, port layer summary
- **[configuration.md](configuration.md)** - `.ini` configuration fields and examples
- **[port-implementation.md](port-implementation.md)** - Port layer contract, buffering design, adding a new MCU family

## Quick Reference

```c
#include "dmdrvi.h"
#include "dmdrvi_ioctl.h"
```

dmeth has no Built-in API of its own (`dmeth.h` is config-struct-only) -
every external access goes through `dmdrvi_open`/`_read`/`_write`/`_ioctl`.

View documentation using `dmf-man`:

```bash
dmf-man dmeth                     # Main documentation
dmf-man dmeth api-reference        # API reference
dmf-man dmeth configuration        # Configuration guide
dmf-man dmeth port-implementation  # Port layer contract
```
