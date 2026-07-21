#include "layout.h"

#include "error.h"
#include "metrics.h"

/* Explicit spacing amounts as 16.16 em fractions.
 *
 * The interword space is cmr10's fontdimen 2 (3.33333 pt at the 10 pt
 * design size); the skeleton resolves glue to its natural width, so stretch
 * and shrink do not appear yet. The math spaces are the classic mu amounts
 * (18 mu = 1 em): \, = 3 mu, \: = 4 mu, \; = 5 mu, \! = -3 mu, \quad = 1 em,
 * \qquad = 2 em. */
#define TXC_SPACE_EM_WORD 21845
#define TXC_SPACE_EM_THIN 10923
#define TXC_SPACE_EM_MEDIUM 14564
#define TXC_SPACE_EM_THICK 18204
#define TXC_SPACE_EM_QUAD 65536
#define TXC_SPACE_EM_QQUAD 131072

static txc_scaled txc_space_width(txc_space space) {
    switch (space) {
    case TXC_SPACE_WORD:
        return txc_em(TXC_SPACE_EM_WORD);
    case TXC_SPACE_THIN:
        return txc_em(TXC_SPACE_EM_THIN);
    case TXC_SPACE_MEDIUM:
        return txc_em(TXC_SPACE_EM_MEDIUM);
    case TXC_SPACE_THICK:
        return txc_em(TXC_SPACE_EM_THICK);
    case TXC_SPACE_NEGATIVE_THIN:
        return -txc_em(TXC_SPACE_EM_THIN);
    case TXC_SPACE_QUAD:
        return txc_em(TXC_SPACE_EM_QUAD);
    case TXC_SPACE_QQUAD:
        return txc_em(TXC_SPACE_EM_QQUAD);
    }
    return 0;
}

tex_core_status
txc_layout(txc_arena *arena, const txc_list *list, size_t source_length, const txc_node **root, tex_core_error *error) {
    txc_node *hbox = txc_arena_alloc(arena, sizeof(txc_node));
    const txc_node **children = NULL;
    if (list->count > 0 && hbox != NULL) {
        children = txc_arena_alloc(arena, list->count * sizeof(*children));
    }
    if (hbox == NULL || (list->count > 0 && children == NULL)) {
        return txc_fail(error, TEX_CORE_STATUS_ALLOCATION_FAILED, NULL, "allocation failed");
    }

    txc_scaled cursor = 0;
    txc_scaled ascent = 0;
    txc_scaled descent = 0;
    size_t index = 0;
    for (const txc_item *item = list->head; item != NULL; item = item->next, index++) {
        txc_node *node = txc_arena_alloc(arena, sizeof(txc_node));
        if (node == NULL) {
            return txc_fail(error, TEX_CORE_STATUS_ALLOCATION_FAILED, NULL, "allocation failed");
        }
        node->x = cursor;
        node->y = 0;
        node->range = item->range;

        if (item->kind == TXC_ITEM_ATOM) {
            const txc_metric *metric = txc_metric_find(item->style, item->codepoint);
            if (metric == NULL) {
                return txc_fail(
                    error,
                    TEX_CORE_STATUS_UNSUPPORTED,
                    &item->range,
                    "unsupported character U+%04X",
                    (unsigned)item->codepoint
                );
            }
            node->kind = TEX_CORE_NODE_GLYPH;
            node->codepoint = item->codepoint;
            node->style = item->style;
            node->width = txc_em(metric->width);
            node->ascent = txc_em(metric->height);
            node->descent = txc_em(metric->depth);
            node->italic = txc_em(metric->italic);
            /* TeX appends the italic correction to a math Ord glyph's
             * advance (Appendix G); with no scripts in the skeleton this is
             * unconditional. */
            cursor += node->width + node->italic;
            if (node->ascent > ascent) {
                ascent = node->ascent;
            }
            if (node->descent > descent) {
                descent = node->descent;
            }
        } else {
            node->kind = TEX_CORE_NODE_KERN;
            node->width = txc_space_width(item->space);
            cursor += node->width;
        }
        children[index] = node;
    }

    hbox->kind = TEX_CORE_NODE_HBOX;
    hbox->width = cursor;
    hbox->ascent = ascent;
    hbox->descent = descent;
    hbox->range.begin = 0;
    hbox->range.end = source_length;
    hbox->children = children;
    hbox->child_count = list->count;

    *root = hbox;
    return TEX_CORE_STATUS_OK;
}
