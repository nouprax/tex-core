#include "layout.h"

#include "error.h"
#include "metrics.h"

/* Explicit spacing amounts as 16.16 em fractions.
 *
 * The interword space is cmr10's fontdimen 2 (3.33333 pt at the 10 pt
 * design size); the engine resolves glue to its natural width, so stretch
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

/* Inter-atom spacing (TeXbook chapter 18): 0 none, 1 thin, 2 medium,
 * 3 thick, indexed [left class][right class] in txc_atom_class order
 * Ord, Op, Bin, Rel, Open, Close, Punct, Inner. These are the display- and
 * text-style amounts; the pairs the TeXbook marks impossible (a Bin next
 * to anything txc_resolve_bin_atoms demotes it beside) are 0. Script
 * styles, which drop the medium and thick pairs and arrive with scripts,
 * are a later M1 increment. */
static const uint8_t TXC_MATH_SPACING[8][8] = {
    /* Ord   */ {0, 1, 2, 3, 0, 0, 0, 1},
    /* Op    */ {1, 1, 0, 3, 0, 0, 0, 1},
    /* Bin   */ {2, 2, 0, 0, 2, 0, 0, 2},
    /* Rel   */ {3, 3, 0, 0, 3, 0, 0, 3},
    /* Open  */ {0, 0, 0, 0, 0, 0, 0, 0},
    /* Close */ {0, 1, 2, 3, 0, 0, 0, 1},
    /* Punct */ {1, 1, 0, 1, 1, 1, 1, 1},
    /* Inner */ {1, 1, 2, 3, 1, 0, 1, 1},
};

static txc_scaled txc_inter_atom_width(uint8_t amount) {
    switch (amount) {
    case 1:
        return txc_em(TXC_SPACE_EM_THIN);
    case 2:
        return txc_em(TXC_SPACE_EM_MEDIUM);
    case 3:
        return txc_em(TXC_SPACE_EM_THICK);
    default:
        return 0;
    }
}

static bool txc_bin_demoting(txc_atom_class atom_class) {
    return atom_class == TXC_ATOM_BIN || atom_class == TXC_ATOM_OP || atom_class == TXC_ATOM_REL ||
           atom_class == TXC_ATOM_OPEN || atom_class == TXC_ATOM_PUNCT;
}

/* Resolves contextual Bin atoms to Ord in place, exactly as TeX's first
 * mlist pass (TeXbook appendix G, and tex.web section 727): a Bin becomes
 * Ord when it opens the list or follows a Bin, Op, Rel, Open, or Punct
 * (using the previous atom's already-resolved class); a Bin directly
 * before a Rel, Close, or Punct becomes Ord retroactively; a trailing Bin
 * becomes Ord. Explicit spacing between atoms never affects demotion. */
static void txc_resolve_bin_atoms(const txc_list *list) {
    txc_item *previous = NULL;
    for (txc_item *item = list->head; item != NULL; item = item->next) {
        if (item->kind != TXC_ITEM_ATOM) {
            continue;
        }
        if (item->atom_class == TXC_ATOM_BIN) {
            if (previous == NULL || txc_bin_demoting(previous->atom_class)) {
                item->atom_class = TXC_ATOM_ORD;
            }
        } else if (
            previous != NULL && previous->atom_class == TXC_ATOM_BIN &&
            (item->atom_class == TXC_ATOM_REL || item->atom_class == TXC_ATOM_CLOSE ||
             item->atom_class == TXC_ATOM_PUNCT)
        ) {
            previous->atom_class = TXC_ATOM_ORD;
        }
        previous = item;
    }
    if (previous != NULL && previous->atom_class == TXC_ATOM_BIN) {
        previous->atom_class = TXC_ATOM_ORD;
    }
}

/* Counts the inter-atom spaces the resolved list needs, so the child array
 * can be allocated exactly. */
static size_t txc_count_inter_atom_spaces(const txc_list *list) {
    size_t count = 0;
    const txc_item *previous_atom = NULL;
    for (const txc_item *item = list->head; item != NULL; item = item->next) {
        if (item->kind != TXC_ITEM_ATOM) {
            continue;
        }
        if (previous_atom != NULL && TXC_MATH_SPACING[previous_atom->atom_class][item->atom_class] != 0) {
            count += 1;
        }
        previous_atom = item;
    }
    return count;
}

tex_core_status txc_layout(
    txc_arena *arena,
    const txc_list *list,
    tex_core_mode mode,
    size_t source_length,
    const txc_node **root,
    tex_core_error *error
) {
    /* Atom classes drive spacing in math lists only; document mode is a
     * plain horizontal text list. */
    bool math = mode != TEX_CORE_MODE_DOCUMENT;
    if (math) {
        txc_resolve_bin_atoms(list);
    }
    size_t child_capacity = list->count + (math ? txc_count_inter_atom_spaces(list) : 0);

    txc_node *hbox = txc_arena_alloc(arena, sizeof(txc_node));
    const txc_node **children = NULL;
    if (child_capacity > 0 && hbox != NULL) {
        children = txc_arena_alloc(arena, child_capacity * sizeof(*children));
    }
    if (hbox == NULL || (child_capacity > 0 && children == NULL)) {
        return txc_fail(error, TEX_CORE_STATUS_ALLOCATION_FAILED, NULL, "allocation failed");
    }

    txc_scaled cursor = 0;
    txc_scaled ascent = 0;
    txc_scaled descent = 0;
    size_t index = 0;
    const txc_item *previous_atom = NULL;
    for (const txc_item *item = list->head; item != NULL; item = item->next) {
        if (math && item->kind == TXC_ITEM_ATOM && previous_atom != NULL) {
            /* TeX inserts the pair's space directly before the right atom,
             * after any explicit spacing between the two (tex.web section
             * 766: intervening glue does not suppress inter-atom glue).
             * The kern's source range is the gap between the atoms. */
            uint8_t amount = TXC_MATH_SPACING[previous_atom->atom_class][item->atom_class];
            if (amount != 0) {
                txc_node *space = txc_arena_alloc(arena, sizeof(txc_node));
                if (space == NULL) {
                    return txc_fail(error, TEX_CORE_STATUS_ALLOCATION_FAILED, NULL, "allocation failed");
                }
                space->kind = TEX_CORE_NODE_KERN;
                space->x = cursor;
                space->y = 0;
                space->width = txc_inter_atom_width(amount);
                space->range.begin = previous_atom->range.end;
                space->range.end = item->range.begin;
                cursor += space->width;
                children[index++] = space;
            }
        }

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
            /* TeX appends the italic correction to a math glyph's advance
             * (Appendix G); with no scripts yet this is unconditional. */
            cursor += node->width + node->italic;
            if (node->ascent > ascent) {
                ascent = node->ascent;
            }
            if (node->descent > descent) {
                descent = node->descent;
            }
            previous_atom = item;
        } else {
            node->kind = TEX_CORE_NODE_KERN;
            node->width = txc_space_width(item->space);
            cursor += node->width;
        }
        children[index++] = node;
    }

    hbox->kind = TEX_CORE_NODE_HBOX;
    hbox->width = cursor;
    hbox->ascent = ascent;
    hbox->descent = descent;
    hbox->range.begin = 0;
    hbox->range.end = source_length;
    hbox->children = children;
    hbox->child_count = index;

    *root = hbox;
    return TEX_CORE_STATUS_OK;
}
