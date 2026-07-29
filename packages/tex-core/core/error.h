/* Internal constructor for the public caller-allocated error struct. */

#ifndef TXC_ERROR_H
#define TXC_ERROR_H

#include "tex_core.h"

/* Formats `error` (when non-NULL) and returns `status`. `range` may be NULL
 * for errors with no source location. The format arguments must be
 * deterministic: no pointers, no floating point, no locale dependence. */
tex_core_status txc_fail(
    tex_core_error *error,
    tex_core_status status,
    const tex_core_range *range,
    const char *format,
    ...
);

#endif
