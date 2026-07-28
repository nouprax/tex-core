#include "metrics.h"

#include <stddef.h>

#include "metrics.inc"

const txc_metric *txc_metric_find(tex_core_family family, tex_core_style style, uint32_t codepoint) {
    const txc_metric *table;
    size_t count;
    switch (family) {
    case TEX_CORE_FAMILY_SIZE1:
        table = TXC_METRICS_SIZE1;
        count = sizeof(TXC_METRICS_SIZE1) / sizeof(TXC_METRICS_SIZE1[0]);
        break;
    case TEX_CORE_FAMILY_SIZE2:
        table = TXC_METRICS_SIZE2;
        count = sizeof(TXC_METRICS_SIZE2) / sizeof(TXC_METRICS_SIZE2[0]);
        break;
    case TEX_CORE_FAMILY_SIZE3:
        table = TXC_METRICS_SIZE3;
        count = sizeof(TXC_METRICS_SIZE3) / sizeof(TXC_METRICS_SIZE3[0]);
        break;
    case TEX_CORE_FAMILY_SIZE4:
        table = TXC_METRICS_SIZE4;
        count = sizeof(TXC_METRICS_SIZE4) / sizeof(TXC_METRICS_SIZE4[0]);
        break;
    case TEX_CORE_FAMILY_MAIN:
    default:
        if (style == TEX_CORE_STYLE_ITALIC) {
            table = TXC_METRICS_ITALIC;
            count = sizeof(TXC_METRICS_ITALIC) / sizeof(TXC_METRICS_ITALIC[0]);
        } else {
            table = TXC_METRICS_UPRIGHT;
            count = sizeof(TXC_METRICS_UPRIGHT) / sizeof(TXC_METRICS_UPRIGHT[0]);
        }
        break;
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

int32_t txc_parameter_value(txc_parameter parameter, txc_mathsize size) {
    _Static_assert(
        sizeof(TXC_PARAMETERS) / sizeof(TXC_PARAMETERS[0]) == TXC_PARAMETER_COUNT,
        "generated parameter table must match the txc_parameter enum"
    );
    return TXC_PARAMETERS[parameter][size];
}
