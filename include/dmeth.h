#ifndef DMETH_H
#define DMETH_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "dmod_types.h"
#include "dmeth_defs.h"

/**
 * Public API for the dmeth module.
 *
 * Functions are declared with the dmod_dmeth_api(...) macro - dmod's
 * standard pattern for functions callable from other modules (or from this
 * module's own tests/), resolved dynamically by the loader rather than
 * through normal static linkage. See dm_sw_ring/include/dm_sw_ring.h for a
 * fully worked real-world example of the same shape.
 *
 * Definitions in src/dmeth.c use the matching
 * dmod_dmeth_api_declaration(...) macro - a plain C function
 * definition here will NOT satisfy these declarations at link time.
 *
 * This is an example interface using the usual "opaque handle" pattern -
 * replace the handle, functions, and struct definition in
 * src/dmeth.c with your module's real API.
 */

/* Opaque handle - the real struct is defined in src/dmeth.c */
typedef struct dmeth* dmeth_t;

/**
 * Create a new dmeth instance.
 *
 * @return A valid handle on success, or NULL on allocation failure.
 */
dmod_dmeth_api(1.0, dmeth_t, _create, ( void ));

/**
 * Destroy an instance created by dmeth_create(). Safe to call with
 * NULL.
 */
dmod_dmeth_api(1.0, void, _destroy, ( dmeth_t handle ));

/**
 * Example accessor - replace with your module's real API.
 *
 * @return true if handle is a valid, non-NULL instance.
 */
dmod_dmeth_api(1.0, bool, _is_valid, ( dmeth_t handle ));

#endif // DMETH_H
