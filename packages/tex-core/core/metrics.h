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

/* The three font sizes of math typesetting. Math styles map onto them:
 * display and text set at the text size, script at the script size,
 * scriptscript at the scriptscript size. */
typedef enum txc_mathsize {
    TXC_MATHSIZE_TEXT = 0,
    TXC_MATHSIZE_SCRIPT = 1,
    TXC_MATHSIZE_SCRIPTSCRIPT = 2
} txc_mathsize;

/* Style parameters (the TeXbook Appendix G sigmas and xis). The enum order
 * is the row order of the generated TXC_PARAMETERS table. */
typedef enum txc_parameter {
    TXC_PARAMETER_QUAD = 0,
    TXC_PARAMETER_X_HEIGHT = 1,
    TXC_PARAMETER_SUP1 = 2,
    TXC_PARAMETER_SUP2 = 3,
    TXC_PARAMETER_SUP3 = 4,
    TXC_PARAMETER_SUB1 = 5,
    TXC_PARAMETER_SUB2 = 6,
    TXC_PARAMETER_SUP_DROP = 7,
    TXC_PARAMETER_SUB_DROP = 8,
    TXC_PARAMETER_RULE_THICKNESS = 9,
    TXC_PARAMETER_NUM1 = 10,
    TXC_PARAMETER_NUM2 = 11,
    TXC_PARAMETER_DENOM1 = 12,
    TXC_PARAMETER_DENOM2 = 13,
    TXC_PARAMETER_AXIS_HEIGHT = 14,
    TXC_PARAMETER_COUNT = 15
} txc_parameter;

/* Returns the metrics for a codepoint in a face style, or NULL when the
 * embedded tables do not cover it. */
const txc_metric *txc_metric_find(tex_core_style style, uint32_t codepoint);

/* Returns a style parameter as a 16.16 em fraction of `size`'s own em. */
int32_t txc_parameter_value(txc_parameter parameter, txc_mathsize size);

#endif
