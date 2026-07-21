#include "metrics.h"

#include <stddef.h>

#include "metrics.inc"

const txc_metric *txc_metric_find(tex_core_style style, uint32_t codepoint) {
    const txc_metric *table;
    size_t count;
    if (style == TEX_CORE_STYLE_ITALIC) {
        table = TXC_METRICS_ITALIC;
        count = sizeof(TXC_METRICS_ITALIC) / sizeof(TXC_METRICS_ITALIC[0]);
    } else {
        table = TXC_METRICS_UPRIGHT;
        count = sizeof(TXC_METRICS_UPRIGHT) / sizeof(TXC_METRICS_UPRIGHT[0]);
    }

    size_t low = 0;
    size_t high = count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (table[middle].codepoint < codepoint) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    if (low < count && table[low].codepoint == codepoint) {
        return &table[low];
    }
    return NULL;
}
