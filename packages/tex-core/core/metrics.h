/* Embedded font metrics (plan decision D5): compiled-in static tables, no
 * file I/O. Regenerate with scripts/generate-metrics.mjs. */

#ifndef TXC_METRICS_H
#define TXC_METRICS_H

#include <stdint.h>

#include "tex_core.h"

/* One glyph row in 16.16 fixed-point em units. */
typedef struct txc_metric {
    uint32_t codepoint;
    int32_t width;
    int32_t height;
    int32_t depth;
    int32_t italic;
} txc_metric;

/* Returns the metrics for a codepoint in a face style, or NULL when the
 * embedded tables do not cover it. */
const txc_metric *txc_metric_find(tex_core_style style, uint32_t codepoint);

#endif
