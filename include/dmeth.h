#ifndef DMETH_H
#define DMETH_H

#include "dmeth_defs.h"
#include "dmeth_types.h"

/**
 * There is no dmod_dmeth_api(...) Built-in API here - unlike a module with
 * its own bespoke API, every external access to dmeth goes through the
 * generic dmdrvi interface (dmdrvi_read/_write/_ioctl/...), same as dmuart.
 * dmeth_config_t itself lives in dmeth_types.h (see the comment there) -
 * this header just re-exports it for `#include "dmeth.h"` consumers.
 */

#endif // DMETH_H
