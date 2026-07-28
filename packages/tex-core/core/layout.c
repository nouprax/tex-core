#include "layout.h"

#include "error.h"
#include "metrics.h"

/* TeX math styles (TeXbook chapter 17, tex.web section 688): 0 display,
 * 2 text, 4 script, 6 scriptscript; odd values are the cramped variants.
 * Style arithmetic follows tex.web section 702. */
#define TXC_STYLE_DISPLAY 0
#define TXC_STYLE_TEXT 2
#define TXC_STYLE_SCRIPT 4

static txc_mathsize txc_style_size(int style) {
    if (style < TXC_STYLE_SCRIPT) {
        return TXC_MATHSIZE_TEXT;
    }
    return style < 6 ? TXC_MATHSIZE_SCRIPT : TXC_MATHSIZE_SCRIPTSCRIPT;
}

static int txc_sup_style(int style) { return 2 * (style / 4) + TXC_STYLE_SCRIPT + (style % 2); }

static int txc_sub_style(int style) { return 2 * (style / 4) + TXC_STYLE_SCRIPT + 1; }

static int txc_num_style(int style) { return style + 2 - 2 * (style / 6); }

static int txc_cramped_style(int style) { return 2 * (style / 2) + 1; }

static int txc_denom_style(int style) { return 2 * (style / 2) + 1 + 2 - 2 * (style / 6); }

/* TeX's half(): odd scaled values round up (tex.web section 100). */
static txc_scaled txc_half(txc_scaled value) { return value % 2 != 0 ? (value + 1) / 2 : value / 2; }

/* The em of each math size: 10 pt text, 7 pt script, 5 pt scriptscript. */
static const txc_scaled TXC_MATHSIZE_EM[3] = {655360, 458752, 327680};

/* TeX's \scriptspace: 0.5 pt of padding on every script box. */
#define TXC_SCRIPT_SPACE 32768

/* TeX's \nulldelimiterspace: 1.2 pt on each side of a fraction, an
 * absolute dimen parameter that never scales with the script sizes. */
#define TXC_NULL_DELIMITER_SPACE 78643

/* Plain TeX's \delimitershortfall (5 pt) and \delimiterfactor (901),
 * both absolute at every size. */
#define TXC_DELIMITER_SHORTFALL 327680
#define TXC_DELIMITER_FACTOR 901

/* Appendix G rule 19: a delimiter must cover `delta1` — the enclosed
 * material's reach above or below the axis, whichever is larger — to at
 * least 901/1000ths of full coverage, and may fall short of full
 * coverage by at most \delimitershortfall (integer div exactly as
 * tex.web's make_left_right). */
static txc_scaled txc_delimiter_target(txc_scaled delta1) {
    txc_scaled factored = (delta1 / 500) * TXC_DELIMITER_FACTOR;
    txc_scaled shortfall = 2 * delta1 - TXC_DELIMITER_SHORTFALL;
    return factored > shortfall ? factored : shortfall;
}

/* The plain TeX \big family targets: rule 19 over empty boxes 8.5 pt,
 * 11.5 pt, 14.5 pt, and 17.5 pt tall on the 10 pt text axis, whatever
 * the surrounding style. */
static const txc_scaled TXC_BIG_HEIGHTS[4] = {557056, 753664, 950272, 1146880};

static txc_scaled txc_param(txc_parameter parameter, txc_mathsize size) {
    return txc_em(txc_parameter_value(parameter, size), TXC_MATHSIZE_EM[size]);
}

/* Explicit spacing amounts as 16.16 fractions.
 *
 * The interword space is cmr10's fontdimen 2 (3.33333 pt at the 10 pt
 * design size); the engine resolves glue to its natural width, so stretch
 * and shrink do not appear yet. The math spaces are the classic mu amounts
 * (18 mu = 1 quad): \, = 3 mu, \: = 4 mu, \; = 5 mu, \! = -3 mu. Mu-based
 * widths resolve against the current size's quad, exactly as TeX's cur_mu;
 * \quad and \qquad are em-based (1 em and 2 em) and resolve against the
 * text em, exactly as TeX's em unit in math mode, which measures the
 * surrounding text font, not the script size. */
#define TXC_SPACE_EM_WORD 21845
#define TXC_SPACE_EM_THIN 10923
#define TXC_SPACE_EM_MEDIUM 14564
#define TXC_SPACE_EM_THICK 18204
#define TXC_SPACE_EM_QUAD 65536
#define TXC_SPACE_EM_QQUAD 131072

static txc_scaled txc_quad(txc_mathsize size) {
    return txc_em(txc_parameter_value(TXC_PARAMETER_QUAD, size), TXC_MATHSIZE_EM[size]);
}

static txc_scaled txc_space_width(txc_space space, txc_mathsize size) {
    switch (space) {
    case TXC_SPACE_WORD:
        return txc_em(TXC_SPACE_EM_WORD, TXC_EM_SP);
    case TXC_SPACE_THIN:
        return txc_em(TXC_SPACE_EM_THIN, txc_quad(size));
    case TXC_SPACE_MEDIUM:
        return txc_em(TXC_SPACE_EM_MEDIUM, txc_quad(size));
    case TXC_SPACE_THICK:
        return txc_em(TXC_SPACE_EM_THICK, txc_quad(size));
    case TXC_SPACE_NEGATIVE_THIN:
        return -txc_em(TXC_SPACE_EM_THIN, txc_quad(size));
    case TXC_SPACE_QUAD:
        return txc_em(TXC_SPACE_EM_QUAD, TXC_EM_SP);
    case TXC_SPACE_QQUAD:
        return txc_em(TXC_SPACE_EM_QQUAD, TXC_EM_SP);
    }
    return 0;
}

/* Inter-atom spacing (TeXbook chapter 18, tex.web's math_spacing string),
 * indexed [left class][right class] in txc_atom_class order Ord, Op, Bin,
 * Rel, Open, Close, Punct, Inner. The digits carry TeX's conditionality:
 * 0 none; 1 thin only in display and text styles; 2 thin in every style;
 * 3 medium and 4 thick, both only in display and text styles. The pairs
 * the TeXbook marks impossible (a Bin next to anything
 * txc_resolve_bin_atoms demotes it beside) are 0. */
static const uint8_t TXC_MATH_SPACING[8][8] = {
    /* Ord   */ {0, 2, 3, 4, 0, 0, 0, 1},
    /* Op    */ {2, 2, 0, 4, 0, 0, 0, 1},
    /* Bin   */ {3, 3, 0, 0, 3, 0, 0, 3},
    /* Rel   */ {4, 4, 0, 0, 4, 0, 0, 4},
    /* Open  */ {0, 0, 0, 0, 0, 0, 0, 0},
    /* Close */ {0, 2, 3, 4, 0, 0, 0, 1},
    /* Punct */ {1, 1, 0, 1, 1, 1, 1, 1},
    /* Inner */ {1, 2, 3, 4, 1, 0, 1, 1},
};

static txc_scaled txc_inter_atom_width(uint8_t digit, int style) {
    bool scripted = style >= TXC_STYLE_SCRIPT;
    txc_mathsize size = txc_style_size(style);
    switch (digit) {
    case 1:
        return scripted ? 0 : txc_em(TXC_SPACE_EM_THIN, txc_quad(size));
    case 2:
        return txc_em(TXC_SPACE_EM_THIN, txc_quad(size));
    case 3:
        return scripted ? 0 : txc_em(TXC_SPACE_EM_MEDIUM, txc_quad(size));
    case 4:
        return scripted ? 0 : txc_em(TXC_SPACE_EM_THICK, txc_quad(size));
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
 * becomes Ord. Explicit spacing between atoms never affects demotion.
 * Every sub-list resolves independently when its own layout runs. */
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

static tex_core_status txc_mlist(
    txc_arena *arena,
    const txc_list *list,
    int style,
    bool math,
    tex_core_range range,
    txc_node **out,
    tex_core_error *error
);

static tex_core_status txc_alloc_fail(tex_core_error *error) {
    return txc_fail(error, TEX_CORE_STATUS_ALLOCATION_FAILED, NULL, "allocation failed");
}

static void txc_kern_init(txc_node *node, txc_scaled x, txc_scaled width, tex_core_range range) {
    node->kind = TEX_CORE_NODE_KERN;
    node->x = x;
    node->y = 0;
    node->codepoint = 0;
    node->style = TEX_CORE_STYLE_UPRIGHT;
    node->family = TEX_CORE_FAMILY_MAIN;
    node->size = 0;
    node->width = width;
    node->ascent = 0;
    node->descent = 0;
    node->italic = 0;
    node->range = range;
    node->children = NULL;
    node->child_count = 0;
}

static void txc_glyph_init(
    txc_node *node,
    uint32_t codepoint,
    tex_core_family family,
    const txc_metric *metric,
    txc_scaled em,
    tex_core_range range
);

/* Initializes a horizontal box node over `children`; the caller fills
 * width, ascent, and descent. */
static void txc_box_init(txc_node *box, tex_core_range range, const txc_node **children, size_t child_count) {
    box->kind = TEX_CORE_NODE_HBOX;
    box->x = 0;
    box->y = 0;
    box->codepoint = 0;
    box->style = TEX_CORE_STYLE_UPRIGHT;
    box->family = TEX_CORE_FAMILY_MAIN;
    box->size = 0;
    box->width = 0;
    box->ascent = 0;
    box->descent = 0;
    box->italic = 0;
    box->range = range;
    box->children = children;
    box->child_count = child_count;
}

static tex_core_status txc_fraction_box(
    txc_arena *arena,
    const txc_fraction *fraction,
    int style,
    tex_core_range range,
    txc_node **out,
    tex_core_error *error
);

static tex_core_status txc_delimiter_boxed(
    txc_arena *arena,
    const txc_delimiter *delimiter,
    int style,
    txc_scaled target,
    tex_core_range range,
    txc_node **out,
    tex_core_error *error
);

static tex_core_status txc_fenced_box(
    txc_arena *arena,
    const txc_fenced *fenced,
    int style,
    tex_core_range range,
    txc_node **out,
    tex_core_error *error
);

static tex_core_status txc_radical_box(
    txc_arena *arena,
    const txc_radical *radical,
    int style,
    tex_core_range range,
    txc_node **out,
    tex_core_error *error
);

static tex_core_status txc_accent_box(
    txc_arena *arena,
    const txc_accent *accent,
    int style,
    tex_core_range range,
    txc_node **out,
    tex_core_error *error
);

static tex_core_status txc_array_box(
    txc_arena *arena,
    const txc_array *array,
    int style,
    tex_core_range range,
    txc_node **out,
    tex_core_error *error
);

/* The rule-19 target of one \big size step. */
static txc_scaled txc_big_target(int size) {
    return txc_delimiter_target(TXC_BIG_HEIGHTS[size - 1] - txc_param(TXC_PARAMETER_AXIS_HEIGHT, TXC_MATHSIZE_TEXT));
}

/* Builds the box for a noad field, TeX's clean_box (tex.web sections
 * 720-721): a character field boxes one glyph and — like clean_box's
 * trivial-box simplification, which strips a lone italic-correction kern —
 * takes the glyph's width without its italic correction; a list field
 * lays out the sub-list at `style` and then applies the same
 * simplification when it produced exactly one glyph; a fraction field
 * builds the fraction's own box. */
static tex_core_status
txc_clean_box(txc_arena *arena, const txc_field *field, int style, txc_node **out, tex_core_error *error) {
    txc_node *box;
    if (field->kind == TXC_FIELD_FRACTION) {
        return txc_fraction_box(arena, field->fraction, style, field->range, out, error);
    }
    if (field->kind == TXC_FIELD_FENCED) {
        return txc_fenced_box(arena, field->fenced, style, field->range, out, error);
    }
    if (field->kind == TXC_FIELD_RADICAL) {
        return txc_radical_box(arena, field->radical, style, field->range, out, error);
    }
    if (field->kind == TXC_FIELD_ACCENT) {
        return txc_accent_box(arena, field->accent, style, field->range, out, error);
    }
    if (field->kind == TXC_FIELD_ARRAY) {
        return txc_array_box(arena, field->array, style, field->range, out, error);
    }
    if (field->kind == TXC_FIELD_DELIMITER) {
        return txc_delimiter_boxed(
            arena,
            &field->sized.delimiter,
            style,
            txc_big_target(field->sized.size),
            field->range,
            out,
            error
        );
    }
    if (field->kind == TXC_FIELD_TEXT) {
        return txc_mlist(arena, &field->list, style, false, field->range, out, error);
    }
    if (field->kind == TXC_FIELD_CHAR) {
        const txc_metric *metric =
            txc_metric_find(field->character.family, field->character.style, field->character.codepoint);
        if (metric == NULL) {
            return txc_fail(
                error,
                TEX_CORE_STATUS_UNSUPPORTED,
                &field->range,
                "unsupported character U+%04X",
                (unsigned)field->character.codepoint
            );
        }
        txc_scaled em = TXC_MATHSIZE_EM[txc_style_size(style)];
        box = txc_arena_alloc(arena, sizeof(txc_node));
        const txc_node **children = txc_arena_alloc(arena, sizeof(*children));
        txc_node *glyph = txc_arena_alloc(arena, sizeof(txc_node));
        if (box == NULL || children == NULL || glyph == NULL) {
            return txc_alloc_fail(error);
        }
        txc_glyph_init(glyph, field->character.codepoint, field->character.family, metric, em, field->range);
        glyph->style = field->character.style;
        children[0] = glyph;
        box->kind = TEX_CORE_NODE_HBOX;
        box->x = 0;
        box->y = 0;
        box->codepoint = 0;
        box->style = TEX_CORE_STYLE_UPRIGHT;
        box->family = TEX_CORE_FAMILY_MAIN;
        box->size = 0;
        box->width = glyph->width;
        box->ascent = glyph->ascent > 0 ? glyph->ascent : 0;
        box->descent = glyph->descent > 0 ? glyph->descent : 0;
        box->italic = 0;
        box->range = field->range;
        box->children = children;
        box->child_count = 1;
    } else {
        tex_core_status status = txc_mlist(arena, &field->list, style, true, field->range, &box, error);
        if (status != TEX_CORE_STATUS_OK) {
            return status;
        }
        if (box->child_count == 1 && box->children[0]->kind == TEX_CORE_NODE_GLYPH) {
            box->width = box->children[0]->width;
        }
    }
    *out = box;
    return TEX_CORE_STATUS_OK;
}

/* Builds the box for a script field: TeX's clean_box plus \scriptspace of
 * width. */
static tex_core_status
txc_script_box(txc_arena *arena, const txc_field *field, int style, txc_node **out, tex_core_error *error) {
    txc_node *box;
    tex_core_status status = txc_clean_box(arena, field, style, &box, error);
    if (status != TEX_CORE_STATUS_OK) {
        return status;
    }
    box->width += TXC_SCRIPT_SPACE;
    *out = box;
    return TEX_CORE_STATUS_OK;
}

static txc_scaled txc_max(txc_scaled left, txc_scaled right) { return left > right ? left : right; }

static void txc_glyph_init(
    txc_node *node,
    uint32_t codepoint,
    tex_core_family family,
    const txc_metric *metric,
    txc_scaled em,
    tex_core_range range
) {
    node->kind = TEX_CORE_NODE_GLYPH;
    node->x = 0;
    node->y = 0;
    node->codepoint = codepoint;
    node->style = TEX_CORE_STYLE_UPRIGHT;
    node->family = family;
    node->size = em;
    node->width = txc_em(metric->width, em);
    node->ascent = txc_em(metric->height, em);
    node->descent = txc_em(metric->depth, em);
    node->italic = txc_em(metric->italic, em);
    node->range = range;
    node->children = NULL;
    node->child_count = 0;
}

/* Builds one piece assembly at least `target` tall: top piece, enough
 * repeaters, a middle piece for braces (repeaters then balance above and
 * below), bottom piece — stacked with no overlap into an hbox whose
 * baseline sits at its bottom. */
static tex_core_status txc_delimiter_assembly(
    txc_arena *arena,
    const txc_delimiter *delimiter,
    txc_scaled target,
    tex_core_range range,
    txc_node **out,
    tex_core_error *error
) {
    txc_scaled em = TXC_MATHSIZE_EM[TXC_MATHSIZE_TEXT];
    const txc_metric *top = txc_metric_find(delimiter->piece_family, TEX_CORE_STYLE_UPRIGHT, delimiter->top);
    const txc_metric *repeat = txc_metric_find(delimiter->piece_family, TEX_CORE_STYLE_UPRIGHT, delimiter->repeat);
    const txc_metric *bottom = txc_metric_find(delimiter->piece_family, TEX_CORE_STYLE_UPRIGHT, delimiter->bottom);
    const txc_metric *middle = delimiter->middle != 0
                                   ? txc_metric_find(delimiter->piece_family, TEX_CORE_STYLE_UPRIGHT, delimiter->middle)
                                   : NULL;
    if (top == NULL || repeat == NULL || bottom == NULL || (delimiter->middle != 0 && middle == NULL)) {
        return txc_fail(error, TEX_CORE_STATUS_UNSUPPORTED, &range, "unsupported delimiter");
    }

    txc_scaled top_size = txc_em(top->height, em) + txc_em(top->depth, em);
    txc_scaled repeat_size = txc_em(repeat->height, em) + txc_em(repeat->depth, em);
    txc_scaled bottom_size = txc_em(bottom->height, em) + txc_em(bottom->depth, em);
    txc_scaled middle_size = middle != NULL ? txc_em(middle->height, em) + txc_em(middle->depth, em) : 0;
    txc_scaled fixed = top_size + bottom_size + middle_size;
    txc_scaled step = middle != NULL ? 2 * repeat_size : repeat_size;
    size_t count = 0;
    if (fixed < target && step > 0) {
        count = (size_t)((target - fixed + step - 1) / step);
    }
    txc_scaled total = fixed + (txc_scaled)count * step;
    size_t pieces = 2 + (middle != NULL ? 1 : 0) + (middle != NULL ? 2 * count : count);

    txc_node *box = txc_arena_alloc(arena, sizeof(txc_node));
    const txc_node **children = txc_arena_alloc(arena, pieces * sizeof(*children));
    if (box == NULL || children == NULL) {
        return txc_alloc_fail(error);
    }

    txc_scaled width = txc_max(txc_em(top->width, em), txc_max(txc_em(repeat->width, em), txc_em(bottom->width, em)));
    if (middle != NULL) {
        width = txc_max(width, txc_em(middle->width, em));
    }

    /* Emit top-down: each piece's ink abuts the previous one. */
    txc_scaled cursor = total;
    size_t index = 0;
    const txc_metric *plan[3] = {top, middle, bottom};
    uint32_t plan_cp[3] = {delimiter->top, delimiter->middle, delimiter->bottom};
    for (size_t part = 0; part < 3; part++) {
        if (plan[part] == NULL || (part == 1 && middle == NULL)) {
            continue;
        }
        if (part > 0) {
            for (size_t copy = 0; copy < count; copy++) {
                txc_node *piece = txc_arena_alloc(arena, sizeof(txc_node));
                if (piece == NULL) {
                    return txc_alloc_fail(error);
                }
                txc_glyph_init(piece, delimiter->repeat, delimiter->piece_family, repeat, em, range);
                piece->y = cursor - piece->ascent;
                cursor -= piece->ascent + piece->descent;
                children[index++] = piece;
            }
            if (part == 1 && middle == NULL) {
                continue;
            }
        }
        txc_node *piece = txc_arena_alloc(arena, sizeof(txc_node));
        if (piece == NULL) {
            return txc_alloc_fail(error);
        }
        txc_glyph_init(piece, plan_cp[part], delimiter->piece_family, plan[part], em, range);
        piece->y = cursor - piece->ascent;
        cursor -= piece->ascent + piece->descent;
        children[index++] = piece;
    }

    txc_box_init(box, range, children, index);
    box->width = width;
    box->ascent = total;
    box->descent = 0;
    *out = box;
    return TEX_CORE_STATUS_OK;
}

/* TeX's var_delimiter (tex.web sections 706-714): the first ladder
 * candidate at least `target` tall wins — the main-family glyph at the
 * current and every larger script size, the size faces at the text em,
 * then the piece assembly. Ladders without pieces fall back to their
 * largest size-face glyph; the null delimiter is a \nulldelimiterspace
 * kern. The chosen node is centered on the math axis of `style`'s size. */
static tex_core_status txc_delimiter_node(
    txc_arena *arena,
    const txc_delimiter *delimiter,
    int style,
    txc_scaled target,
    tex_core_range range,
    txc_node **out,
    tex_core_error *error
) {
    if (delimiter->small == 0) {
        txc_node *kern = txc_arena_alloc(arena, sizeof(txc_node));
        if (kern == NULL) {
            return txc_alloc_fail(error);
        }
        txc_kern_init(kern, 0, TXC_NULL_DELIMITER_SPACE, range);
        *out = kern;
        return TEX_CORE_STATUS_OK;
    }

    txc_scaled axis = txc_param(TXC_PARAMETER_AXIS_HEIGHT, txc_style_size(style));
    const txc_metric *chosen = NULL;
    tex_core_family chosen_family = TEX_CORE_FAMILY_MAIN;
    txc_scaled chosen_em = 0;

    /* Small candidates: the current size's glyph, then each larger one. */
    for (int size = (int)txc_style_size(style); size >= (int)TXC_MATHSIZE_TEXT; size--) {
        const txc_metric *metric = txc_metric_find(TEX_CORE_FAMILY_MAIN, TEX_CORE_STYLE_UPRIGHT, delimiter->small);
        if (metric == NULL) {
            break;
        }
        txc_scaled em = TXC_MATHSIZE_EM[size];
        if (txc_em(metric->height, em) + txc_em(metric->depth, em) >= target) {
            chosen = metric;
            chosen_family = TEX_CORE_FAMILY_MAIN;
            chosen_em = em;
            break;
        }
    }

    /* Size-face candidates at the text em; NEVER ladders keep the last
     * one that exists as their fallback. */
    if (chosen == NULL && delimiter->ladder != TXC_LADDER_ALWAYS) {
        const txc_metric *largest = NULL;
        tex_core_family largest_family = TEX_CORE_FAMILY_MAIN;
        for (tex_core_family family = TEX_CORE_FAMILY_SIZE1; family <= TEX_CORE_FAMILY_SIZE4; family++) {
            const txc_metric *metric = txc_metric_find(family, TEX_CORE_STYLE_UPRIGHT, delimiter->small);
            if (metric == NULL) {
                continue;
            }
            largest = metric;
            largest_family = family;
            txc_scaled em = TXC_MATHSIZE_EM[TXC_MATHSIZE_TEXT];
            if (txc_em(metric->height, em) + txc_em(metric->depth, em) >= target) {
                chosen = metric;
                chosen_family = family;
                chosen_em = TXC_MATHSIZE_EM[TXC_MATHSIZE_TEXT];
                break;
            }
        }
        if (chosen == NULL && delimiter->ladder == TXC_LADDER_NEVER && largest != NULL) {
            chosen = largest;
            chosen_family = largest_family;
            chosen_em = TXC_MATHSIZE_EM[TXC_MATHSIZE_TEXT];
        }
    }

    txc_node *node = NULL;
    if (chosen != NULL) {
        node = txc_arena_alloc(arena, sizeof(txc_node));
        if (node == NULL) {
            return txc_alloc_fail(error);
        }
        txc_glyph_init(node, delimiter->small, chosen_family, chosen, chosen_em, range);
    } else {
        tex_core_status status = txc_delimiter_assembly(arena, delimiter, target, range, &node, error);
        if (status != TEX_CORE_STATUS_OK) {
            return status;
        }
    }
    node->y = axis - txc_half(node->ascent - node->descent);
    *out = node;
    return TEX_CORE_STATUS_OK;
}

/* Wraps a delimiter node in a plain box on the surrounding baseline, so
 * a sized delimiter can be an atom nucleus or a clean-box field. */
static tex_core_status txc_delimiter_boxed(
    txc_arena *arena,
    const txc_delimiter *delimiter,
    int style,
    txc_scaled target,
    tex_core_range range,
    txc_node **out,
    tex_core_error *error
) {
    txc_node *node;
    tex_core_status status = txc_delimiter_node(arena, delimiter, style, target, range, &node, error);
    if (status != TEX_CORE_STATUS_OK) {
        return status;
    }
    txc_node *box = txc_arena_alloc(arena, sizeof(txc_node));
    const txc_node **children = txc_arena_alloc(arena, sizeof(*children));
    if (box == NULL || children == NULL) {
        return txc_alloc_fail(error);
    }
    children[0] = node;
    txc_box_init(box, range, children, 1);
    box->width = node->width;
    box->ascent = txc_max(node->y + node->ascent, 0);
    box->descent = txc_max(node->descent - node->y, 0);
    *out = box;
    return TEX_CORE_STATUS_OK;
}

/* Builds a generalized fraction's box, TeXbook Appendix G rule 15
 * (tex.web's make_fraction, sections 743-748): numerator and denominator
 * are clean boxes one style step down (the denominator cramped), shifted
 * apart until each gap to the bar reaches three rule thicknesses in
 * display style, one otherwise; the bar is a rule of the current size's
 * default thickness centered on the math axis; null delimiters flank the
 * pair as \nulldelimiterspace kerns. Every parameter resolves at the
 * fraction's own style size — \dfrac and \tfrac force display and text
 * style exactly as \displaystyle/\textstyle, uncramped. */
static tex_core_status txc_fraction_box(
    txc_arena *arena,
    const txc_fraction *fraction,
    int style,
    tex_core_range range,
    txc_node **out,
    tex_core_error *error
) {
    int fraction_style = style;
    if (fraction->style == TXC_FRACTION_STYLE_DISPLAY) {
        fraction_style = TXC_STYLE_DISPLAY;
    } else if (fraction->style == TXC_FRACTION_STYLE_TEXT) {
        fraction_style = TXC_STYLE_TEXT;
    }
    txc_mathsize size = txc_style_size(fraction_style);

    txc_node *num;
    tex_core_status status = txc_clean_box(arena, &fraction->num, txc_num_style(fraction_style), &num, error);
    if (status != TEX_CORE_STATUS_OK) {
        return status;
    }
    txc_node *den;
    status = txc_clean_box(arena, &fraction->den, txc_denom_style(fraction_style), &den, error);
    if (status != TEX_CORE_STATUS_OK) {
        return status;
    }

    txc_scaled default_thickness = txc_param(TXC_PARAMETER_RULE_THICKNESS, size);
    txc_scaled thickness = fraction->binom ? 0 : default_thickness;
    txc_scaled axis = txc_param(TXC_PARAMETER_AXIS_HEIGHT, size);
    txc_scaled shift_up;
    txc_scaled shift_down;
    if (fraction_style < TXC_STYLE_TEXT) {
        shift_up = txc_param(TXC_PARAMETER_NUM1, size);
        shift_down = txc_param(TXC_PARAMETER_DENOM1, size);
    } else {
        shift_up = txc_param(thickness != 0 ? TXC_PARAMETER_NUM2 : TXC_PARAMETER_NUM3, size);
        shift_down = txc_param(TXC_PARAMETER_DENOM2, size);
    }
    txc_scaled delta = txc_half(thickness);
    if (thickness == 0) {
        /* No bar (tex.web section 745): keep seven default thicknesses of
         * clearance in display style, three otherwise, pushing both parts
         * apart by half any shortfall. */
        txc_scaled clearance = fraction_style < TXC_STYLE_TEXT ? 7 * default_thickness : 3 * default_thickness;
        txc_scaled gap = (shift_up - num->descent) - (den->ascent - shift_down);
        txc_scaled push = txc_half(clearance - gap);
        if (push > 0) {
            shift_up += push;
            shift_down += push;
        }
    } else {
        txc_scaled clearance = fraction_style < TXC_STYLE_TEXT ? 3 * thickness : thickness;
        txc_scaled gap_up = clearance - ((shift_up - num->descent) - (axis + delta));
        if (gap_up > 0) {
            shift_up += gap_up;
        }
        txc_scaled gap_down = clearance - ((axis - delta) - (den->ascent - shift_down));
        if (gap_down > 0) {
            shift_down += gap_down;
        }
    }

    txc_node *left;
    txc_node *right;
    txc_node *rule = NULL;
    if (fraction->binom) {
        /* Parentheses sized to delim1/delim2 replace the null delimiters
         * (tex.web section 748). */
        static const txc_delimiter paren_left =
            {0x0028, TXC_LADDER_LARGE, 0x239B, 0x239C, 0x239D, 0, TEX_CORE_FAMILY_SIZE4};
        static const txc_delimiter paren_right =
            {0x0029, TXC_LADDER_LARGE, 0x239E, 0x239F, 0x23A0, 0, TEX_CORE_FAMILY_SIZE4};
        txc_scaled target =
            txc_param(fraction_style < TXC_STYLE_TEXT ? TXC_PARAMETER_DELIM1 : TXC_PARAMETER_DELIM2, size);
        status = txc_delimiter_node(arena, &paren_left, fraction_style, target, fraction->command, &left, error);
        if (status != TEX_CORE_STATUS_OK) {
            return status;
        }
        status = txc_delimiter_node(arena, &paren_right, fraction_style, target, fraction->command, &right, error);
        if (status != TEX_CORE_STATUS_OK) {
            return status;
        }
    } else {
        left = txc_arena_alloc(arena, sizeof(txc_node));
        right = txc_arena_alloc(arena, sizeof(txc_node));
        rule = txc_arena_alloc(arena, sizeof(txc_node));
        if (left == NULL || right == NULL || rule == NULL) {
            return txc_alloc_fail(error);
        }
        tex_core_range left_edge = {fraction->command.begin, fraction->command.begin};
        txc_kern_init(left, 0, TXC_NULL_DELIMITER_SPACE, left_edge);
        tex_core_range right_edge = {range.end, range.end};
        txc_kern_init(right, 0, TXC_NULL_DELIMITER_SPACE, right_edge);

        rule->kind = TEX_CORE_NODE_RULE;
        rule->y = axis + delta - thickness;
        rule->codepoint = 0;
        rule->style = TEX_CORE_STYLE_UPRIGHT;
        rule->family = TEX_CORE_FAMILY_MAIN;
        rule->size = 0;
        rule->ascent = thickness;
        rule->descent = 0;
        rule->italic = 0;
        rule->range = fraction->command;
        rule->children = NULL;
        rule->child_count = 0;
    }

    /* The parts share the wider width, the narrower centered — the
     * resolved geometry of TeX's rebox fil glue. */
    txc_scaled width = txc_max(num->width, den->width);
    num->x = left->width + txc_half(width - num->width);
    num->y = shift_up;
    den->x = left->width + txc_half(width - den->width);
    den->y = -shift_down;
    right->x = left->width + width;
    if (rule != NULL) {
        rule->x = left->width;
        rule->width = width;
    }

    size_t child_count = rule != NULL ? 5 : 4;
    txc_node *box = txc_arena_alloc(arena, sizeof(txc_node));
    const txc_node **children = txc_arena_alloc(arena, child_count * sizeof(*children));
    if (box == NULL || children == NULL) {
        return txc_alloc_fail(error);
    }

    /* Child order is source order: the bar and both binomial
     * delimiters come from the command token, which precedes both
     * arguments — so a binomial's right parenthesis is emitted before
     * the parts and placed by `x` alone. The barred fraction's right
     * kern carries the construct-end range and stays last. */
    size_t index = 0;
    children[index++] = left;
    if (rule != NULL) {
        children[index++] = rule;
    } else {
        children[index++] = right;
    }
    children[index++] = num;
    children[index++] = den;
    if (rule != NULL) {
        children[index++] = right;
    }

    txc_scaled ascent = shift_up + num->ascent;
    txc_scaled descent = shift_down + den->descent;
    if (fraction->binom) {
        ascent = txc_max(ascent, txc_max(left->y + left->ascent, right->y + right->ascent));
        descent = txc_max(descent, txc_max(left->descent - left->y, right->descent - right->y));
    }

    txc_box_init(box, range, children, child_count);
    box->width = left->width + width + right->width;
    box->ascent = ascent;
    box->descent = descent;

    *out = box;
    return TEX_CORE_STATUS_OK;
}

/* Builds a \left/\right box (tex.web sections 762 and 1191-1192): the
 * enclosed list is laid out in the surrounding style, both delimiters
 * grow to the rule-19 target of its reach around the axis, and the box's
 * children are the left delimiter, the enclosed nodes spliced directly,
 * and the right delimiter. Inside the box the delimiters space as Open
 * and Close atoms; the box itself is one Inner atom outside. */
static tex_core_status txc_fenced_box(
    txc_arena *arena,
    const txc_fenced *fenced,
    int style,
    tex_core_range range,
    txc_node **out,
    tex_core_error *error
) {
    tex_core_range inner_range = {fenced->left_range.end, fenced->right_range.begin};
    txc_node *inner;
    tex_core_status status = txc_mlist(arena, &fenced->list, style, true, inner_range, &inner, error);
    if (status != TEX_CORE_STATUS_OK) {
        return status;
    }

    txc_scaled axis = txc_param(TXC_PARAMETER_AXIS_HEIGHT, txc_style_size(style));
    txc_scaled delta1 = txc_max(inner->ascent - axis, inner->descent + axis);
    txc_scaled target = txc_delimiter_target(delta1);

    txc_node *left;
    status = txc_delimiter_node(arena, &fenced->left, style, target, fenced->left_range, &left, error);
    if (status != TEX_CORE_STATUS_OK) {
        return status;
    }
    txc_node *right;
    status = txc_delimiter_node(arena, &fenced->right, style, target, fenced->right_range, &right, error);
    if (status != TEX_CORE_STATUS_OK) {
        return status;
    }

    /* The Close-side pair space: the pair table row of the last enclosed
     * atom against Close (the Open row is all zeros, so the left side
     * never spaces). */
    const txc_item *last_atom = NULL;
    for (const txc_item *item = fenced->list.head; item != NULL; item = item->next) {
        if (item->kind == TXC_ITEM_ATOM) {
            last_atom = item;
        }
    }
    txc_scaled close_space = 0;
    if (last_atom != NULL) {
        close_space = txc_inter_atom_width(TXC_MATH_SPACING[last_atom->atom_class][TXC_ATOM_CLOSE], style);
    }

    size_t child_count = 2 + inner->child_count + (close_space != 0 ? 1 : 0);
    txc_node *box = txc_arena_alloc(arena, sizeof(txc_node));
    const txc_node **children = txc_arena_alloc(arena, child_count * sizeof(*children));
    if (box == NULL || children == NULL) {
        return txc_alloc_fail(error);
    }

    size_t index = 0;
    txc_scaled cursor = 0;
    children[index++] = left;
    cursor += left->width;
    for (size_t child = 0; child < inner->child_count; child++) {
        txc_node *node = (txc_node *)inner->children[child];
        node->x += cursor;
        children[index++] = node;
    }
    cursor += inner->width;
    if (close_space != 0) {
        txc_node *space = txc_arena_alloc(arena, sizeof(txc_node));
        if (space == NULL) {
            return txc_alloc_fail(error);
        }
        tex_core_range gap = {last_atom->range.end, fenced->right_range.begin};
        txc_kern_init(space, cursor, close_space, gap);
        cursor += close_space;
        children[index++] = space;
    }
    right->x = cursor;
    children[index++] = right;
    cursor += right->width;

    txc_scaled ascent = 0;
    txc_scaled descent = 0;
    for (size_t child = 0; child < index; child++) {
        const txc_node *node = children[child];
        if (node->kind == TEX_CORE_NODE_KERN) {
            continue;
        }
        ascent = txc_max(ascent, node->ascent + node->y);
        descent = txc_max(descent, node->descent - node->y);
    }

    txc_box_init(box, range, children, index);
    box->width = cursor;
    box->ascent = ascent;
    box->descent = descent;

    *out = box;
    return TEX_CORE_STATUS_OK;
}

/* LaTeX's array geometry at the 10 pt text size. \arraycolsep (5 pt)
 * pads every column on both sides; the matrix family's outer
 * -\arraycolsep skips cancel the edge padding exactly, so columns sit
 * 2\arraycolsep apart with none outside and no kerns are published.
 * Rows pitch their baselines \baselineskip (12 pt) apart, falling back
 * to \lineskip (1 pt) of clearance when a row pair reaches
 * \lineskiplimit (0 pt), exactly as TeX's vpack. LaTeX's \@arstrut
 * floors every row at .7 and .3 of \baselineskip — by TeX's decimal
 * scan those coefficients are 45875/65536 and 19661/65536, so the strut
 * is exactly 550500 sp over 235932 sp. */
#define TXC_ARRAY_COLSEP 327680
#define TXC_ARRAY_BASELINESKIP 786432
#define TXC_ARRAY_LINESKIP 65536
#define TXC_ARRAY_STRUT_ASCENT 550500
#define TXC_ARRAY_STRUT_DESCENT 235932

/* Builds a math alignment environment's box (the matrix family): every
 * cell is its own inline math list set in text style whatever the
 * surrounding style — array cells open fresh inline math, so a matrix
 * inside a script keeps full-size entries, exactly as LaTeX. Each
 * column takes its widest cell's width with cells centered by half the
 * excess; each row is an hbox of the full alignment width whose
 * extents are floored by the array strut (the one deliberate excess
 * over its children besides the operator-limits assembly); the stacked
 * rows are centered on the math axis exactly as \vcenter (tex.web
 * section 736), and the delimited variants grow their fences to the
 * rule-19 target of the centered box's reach, exactly as
 * \left/\right. */
static tex_core_status txc_array_box(
    txc_arena *arena,
    const txc_array *array,
    int style,
    tex_core_range range,
    txc_node **out,
    tex_core_error *error
) {
    size_t column_count = 0;
    for (const txc_array_row *row = array->rows; row != NULL; row = row->next) {
        if (row->cell_count > column_count) {
            column_count = row->cell_count;
        }
    }

    txc_scaled *column_widths = NULL;
    txc_node **row_boxes = NULL;
    txc_scaled *row_ascents = NULL;
    txc_scaled *row_descents = NULL;
    if (column_count > 0) {
        column_widths = txc_arena_alloc(arena, column_count * sizeof(*column_widths));
        row_boxes = txc_arena_alloc(arena, array->row_count * sizeof(*row_boxes));
        row_ascents = txc_arena_alloc(arena, array->row_count * sizeof(*row_ascents));
        row_descents = txc_arena_alloc(arena, array->row_count * sizeof(*row_descents));
        if (column_widths == NULL || row_boxes == NULL || row_ascents == NULL || row_descents == NULL) {
            return txc_alloc_fail(error);
        }
        for (size_t column = 0; column < column_count; column++) {
            column_widths[column] = 0;
        }
    }

    /* First pass: lay out every cell, collecting the column width and
     * strutted row extent maxima. */
    size_t row_index = 0;
    for (const txc_array_row *row = array->rows; row != NULL; row = row->next, row_index++) {
        txc_node *row_box = txc_arena_alloc(arena, sizeof(txc_node));
        const txc_node **cells = txc_arena_alloc(arena, row->cell_count * sizeof(*cells));
        if (row_box == NULL || cells == NULL) {
            return txc_alloc_fail(error);
        }
        txc_scaled ascent = TXC_ARRAY_STRUT_ASCENT;
        txc_scaled descent = TXC_ARRAY_STRUT_DESCENT;
        size_t column = 0;
        for (const txc_array_cell *cell = row->cells; cell != NULL; cell = cell->next, column++) {
            txc_node *box;
            tex_core_status status = txc_mlist(arena, &cell->list, TXC_STYLE_TEXT, true, cell->range, &box, error);
            if (status != TEX_CORE_STATUS_OK) {
                return status;
            }
            cells[column] = box;
            if (box->width > column_widths[column]) {
                column_widths[column] = box->width;
            }
            ascent = txc_max(ascent, box->ascent);
            descent = txc_max(descent, box->descent);
        }
        tex_core_range row_range = {row->cells->range.begin, row->cells->range.begin};
        for (const txc_array_cell *cell = row->cells; cell != NULL; cell = cell->next) {
            row_range.end = cell->range.end;
        }
        txc_box_init(row_box, row_range, cells, row->cell_count);
        row_box->ascent = ascent;
        row_box->descent = descent;
        row_boxes[row_index] = row_box;
        row_ascents[row_index] = ascent;
        row_descents[row_index] = descent;
    }

    /* Column positions and the alignment width. */
    txc_scaled width = 0;
    for (size_t column = 0; column < column_count; column++) {
        if (column > 0) {
            width += 2 * TXC_ARRAY_COLSEP;
        }
        width += column_widths[column];
    }

    /* Row baselines by TeX's interline glue, then the \vcenter split
     * around the axis of the surrounding style's size. */
    txc_scaled axis = txc_param(TXC_PARAMETER_AXIS_HEIGHT, txc_style_size(style));
    txc_scaled depth_below_first = 0;
    for (size_t index = 0; index < array->row_count; index++) {
        if (index > 0) {
            txc_scaled gap = TXC_ARRAY_BASELINESKIP - row_descents[index - 1] - row_ascents[index];
            txc_scaled distance =
                gap >= 0 ? TXC_ARRAY_BASELINESKIP : row_descents[index - 1] + row_ascents[index] + TXC_ARRAY_LINESKIP;
            depth_below_first += distance;
        }
    }
    txc_scaled stack_height = array->row_count > 0 ? row_ascents[0] : 0;
    txc_scaled stack_depth = array->row_count > 0 ? depth_below_first + row_descents[array->row_count - 1] : 0;
    txc_scaled delta = stack_height + stack_depth;
    txc_scaled ascent = axis + txc_half(delta);
    txc_scaled descent = delta - ascent;

    /* Second pass: place the cells in their columns and the rows on
     * their baselines. */
    txc_scaled baseline = ascent - stack_height;
    for (size_t index = 0; index < array->row_count; index++) {
        txc_node *row_box = row_boxes[index];
        if (index > 0) {
            txc_scaled gap = TXC_ARRAY_BASELINESKIP - row_descents[index - 1] - row_ascents[index];
            txc_scaled distance =
                gap >= 0 ? TXC_ARRAY_BASELINESKIP : row_descents[index - 1] + row_ascents[index] + TXC_ARRAY_LINESKIP;
            baseline -= distance;
        }
        row_box->y = baseline;
        row_box->width = width;
        txc_scaled cursor = 0;
        size_t column = 0;
        for (size_t cell = 0; cell < row_box->child_count; cell++, column++) {
            txc_node *box = (txc_node *)row_box->children[cell];
            box->x = cursor + txc_half(column_widths[column] - box->width);
            cursor += column_widths[column] + 2 * TXC_ARRAY_COLSEP;
        }
    }

    if (!array->delimited) {
        txc_node *box = txc_arena_alloc(arena, sizeof(txc_node));
        const txc_node **children = NULL;
        if (array->row_count > 0 && box != NULL) {
            children = txc_arena_alloc(arena, array->row_count * sizeof(*children));
        }
        if (box == NULL || (array->row_count > 0 && children == NULL)) {
            return txc_alloc_fail(error);
        }
        for (size_t index = 0; index < array->row_count; index++) {
            children[index] = row_boxes[index];
        }
        txc_box_init(box, range, children, array->row_count);
        box->width = width;
        box->ascent = ascent;
        box->descent = descent;
        *out = box;
        return TEX_CORE_STATUS_OK;
    }

    /* The delimited variants are their amsmath definitions —
     * \left<delim> matrix \right<delim> — so the fences grow against
     * the centered stack's reach around the axis. */
    txc_scaled delta1 = txc_max(ascent - axis, descent + axis);
    txc_scaled target = txc_delimiter_target(delta1);
    txc_node *left;
    tex_core_status status = txc_delimiter_node(arena, &array->left, style, target, array->begin, &left, error);
    if (status != TEX_CORE_STATUS_OK) {
        return status;
    }
    txc_node *right;
    status = txc_delimiter_node(arena, &array->right, style, target, array->end, &right, error);
    if (status != TEX_CORE_STATUS_OK) {
        return status;
    }

    size_t child_count = 2 + array->row_count;
    txc_node *box = txc_arena_alloc(arena, sizeof(txc_node));
    const txc_node **children = txc_arena_alloc(arena, child_count * sizeof(*children));
    if (box == NULL || children == NULL) {
        return txc_alloc_fail(error);
    }
    size_t index = 0;
    children[index++] = left;
    for (size_t row = 0; row < array->row_count; row++) {
        txc_node *row_box = row_boxes[row];
        row_box->x = left->width;
        children[index++] = row_box;
    }
    right->x = left->width + width;
    children[index++] = right;

    txc_scaled box_ascent = ascent;
    txc_scaled box_descent = descent;
    box_ascent = txc_max(box_ascent, txc_max(left->y + left->ascent, right->y + right->ascent));
    box_descent = txc_max(box_descent, txc_max(left->descent - left->y, right->descent - right->y));

    txc_box_init(box, range, children, child_count);
    box->width = left->width + width + right->width;
    box->ascent = box_ascent;
    box->descent = box_descent;
    *out = box;
    return TEX_CORE_STATUS_OK;
}

/* LaTeX's index raise (\r@@t): 0.6 of the radical box's ascent minus
 * descent, as the 16.16 fraction 39322; and the \mkern amounts flanking
 * the index, 5 mu and -10 mu of the current size's quad. */
#define TXC_RADICAL_RAISE 39322
#define TXC_RADICAL_KERN_BEFORE 18204
#define TXC_RADICAL_KERN_AFTER 36409

/* Builds a radical's box, TeXbook Appendix G rule 11 (tex.web's
 * make_radical, section 737): the radicand is a clean box in the cramped
 * style, the clearance is one rule thickness plus a quarter of the
 * x-height in display style (a quarter rule thickness otherwise), the
 * sign grows through the delimiter ladder — stopping at the size4 glyph,
 * as the vendored faces publish no radical assembly pieces — half of any
 * excess joins the clearance, and the bar sits over the radicand flush
 * with the sign's top. The optional LaTeX index is set in uncramped
 * scriptscript style, raised 0.6 of the sign box's ascent minus descent,
 * between 5 mu and -10 mu kerns. */
static tex_core_status txc_radical_box(
    txc_arena *arena,
    const txc_radical *radical,
    int style,
    tex_core_range range,
    txc_node **out,
    tex_core_error *error
) {
    static const txc_delimiter sign_delimiter = {0x221A, TXC_LADDER_NEVER, 0, 0, 0, 0, TEX_CORE_FAMILY_MAIN};

    txc_node *radicand;
    tex_core_status status = txc_clean_box(arena, &radical->argument, txc_cramped_style(style), &radicand, error);
    if (status != TEX_CORE_STATUS_OK) {
        return status;
    }

    txc_mathsize size = txc_style_size(style);
    txc_scaled thickness = txc_param(TXC_PARAMETER_RULE_THICKNESS, size);
    txc_scaled clearance =
        style < TXC_STYLE_TEXT ? thickness + txc_param(TXC_PARAMETER_X_HEIGHT, size) / 4 : thickness + thickness / 4;
    txc_scaled target = radicand->ascent + radicand->descent + clearance + thickness;

    txc_node *sign;
    status = txc_delimiter_node(arena, &sign_delimiter, style, target, radical->command, &sign, error);
    if (status != TEX_CORE_STATUS_OK) {
        return status;
    }
    txc_scaled excess = (sign->ascent + sign->descent) - target;
    if (excess > 0) {
        clearance += txc_half(excess);
    }

    txc_scaled bar_bottom = radicand->ascent + clearance;
    txc_scaled bar_top = bar_bottom + thickness;
    sign->y = bar_top - sign->ascent;

    /* The index is laid out against the sign-and-bar box's extents. */
    txc_scaled base_ascent = bar_top;
    txc_scaled base_descent = txc_max(radicand->descent, sign->descent - sign->y);

    bool has_index = radical->index.kind != TXC_FIELD_EMPTY;
    txc_node *index_box = NULL;
    txc_scaled kern_before = 0;
    txc_scaled kern_after = 0;
    txc_scaled raise = 0;
    if (has_index) {
        status = txc_mlist(arena, &radical->index.list, 6, true, radical->index.range, &index_box, error);
        if (status != TEX_CORE_STATUS_OK) {
            return status;
        }
        kern_before = txc_em(TXC_RADICAL_KERN_BEFORE, txc_quad(size));
        kern_after = txc_em(TXC_RADICAL_KERN_AFTER, txc_quad(size));
        raise = txc_em(TXC_RADICAL_RAISE, base_ascent - base_descent);
        index_box->x = kern_before;
        index_box->y = raise;
    }

    /* LaTeX's -10 mu deliberately tucks the sign under the raised
     * index, so the lead may be negative and the sign may start left of
     * the previous advance. */
    txc_scaled lead = has_index ? kern_before + index_box->width - kern_after : 0;
    sign->x = lead;
    txc_scaled content_x = lead + sign->width;

    txc_node *rule = txc_arena_alloc(arena, sizeof(txc_node));
    txc_node *box = txc_arena_alloc(arena, sizeof(txc_node));
    size_t child_count = has_index ? 6 : 3;
    const txc_node **children = txc_arena_alloc(arena, child_count * sizeof(*children));
    txc_node *before = NULL;
    txc_node *after = NULL;
    if (has_index) {
        before = txc_arena_alloc(arena, sizeof(txc_node));
        after = txc_arena_alloc(arena, sizeof(txc_node));
    }
    if (rule == NULL || box == NULL || children == NULL || (has_index && (before == NULL || after == NULL))) {
        return txc_alloc_fail(error);
    }

    rule->kind = TEX_CORE_NODE_RULE;
    rule->x = content_x;
    rule->y = bar_bottom;
    rule->codepoint = 0;
    rule->style = TEX_CORE_STYLE_UPRIGHT;
    rule->family = TEX_CORE_FAMILY_MAIN;
    rule->size = 0;
    rule->width = radicand->width;
    rule->ascent = thickness;
    rule->descent = 0;
    rule->italic = 0;
    rule->range = radical->command;
    rule->children = NULL;
    rule->child_count = 0;

    radicand->x = content_x;
    radicand->y = 0;

    /* Child order is source order: the sign and the bar carry the
     * command token, the index kerns anchor at the bracket edges. */
    size_t index = 0;
    children[index++] = sign;
    children[index++] = rule;
    if (has_index) {
        tex_core_range before_edge = {radical->index.range.begin, radical->index.range.begin};
        txc_kern_init(before, 0, kern_before, before_edge);
        tex_core_range after_edge = {radical->index.range.end, radical->index.range.end};
        txc_kern_init(after, kern_before + index_box->width, -kern_after, after_edge);
        children[index++] = before;
        children[index++] = index_box;
        children[index++] = after;
    }
    children[index++] = radicand;

    txc_scaled ascent = base_ascent;
    txc_scaled descent = base_descent;
    if (has_index) {
        ascent = txc_max(ascent, index_box->y + index_box->ascent);
        descent = txc_max(descent, index_box->descent - index_box->y);
    }

    txc_box_init(box, range, children, index);
    box->width = content_x + radicand->width;
    box->ascent = ascent;
    box->descent = descent;

    *out = box;
    return TEX_CORE_STATUS_OK;
}

/* Builds a math accent's box, TeXbook Appendix G rule 12 (tex.web's
 * make_math_accent, section 738): the nucleus is a clean box in the
 * cramped style; the accent glyph — main family at the current size, or
 * for the wide pair the first width step at least as wide as the
 * nucleus, capped at size4 — rises with any nucleus taller than the
 * x-height and shifts right by the nucleus character's skew. The box
 * keeps the nucleus width. */
static tex_core_status txc_accent_box(
    txc_arena *arena,
    const txc_accent *accent,
    int style,
    tex_core_range range,
    txc_node **out,
    tex_core_error *error
) {
    txc_node *nucleus;
    tex_core_status status = txc_clean_box(arena, &accent->argument, txc_cramped_style(style), &nucleus, error);
    if (status != TEX_CORE_STATUS_OK) {
        return status;
    }

    txc_mathsize size = txc_style_size(style);
    txc_scaled em = TXC_MATHSIZE_EM[size];
    txc_scaled skew = 0;
    if (accent->argument.kind == TXC_FIELD_CHAR) {
        const txc_metric *metric = txc_metric_find(
            accent->argument.character.family,
            accent->argument.character.style,
            accent->argument.character.codepoint
        );
        if (metric != NULL) {
            skew = txc_em(metric->skew, em);
        }
    }

    const txc_metric *chosen = txc_metric_find(TEX_CORE_FAMILY_MAIN, TEX_CORE_STYLE_UPRIGHT, accent->codepoint);
    tex_core_family family = TEX_CORE_FAMILY_MAIN;
    txc_scaled glyph_em = em;
    if (chosen == NULL) {
        return txc_fail(error, TEX_CORE_STATUS_UNSUPPORTED, &accent->command, "unsupported accent");
    }
    if (accent->wide) {
        /* The width ladder: the text-size glyph, then the size faces,
         * first fit at least as wide as the nucleus, else the widest. */
        glyph_em = TXC_MATHSIZE_EM[TXC_MATHSIZE_TEXT];
        for (tex_core_family step = TEX_CORE_FAMILY_SIZE1; step <= TEX_CORE_FAMILY_SIZE4; step++) {
            if (txc_em(chosen->width, glyph_em) >= nucleus->width) {
                break;
            }
            const txc_metric *metric = txc_metric_find(step, TEX_CORE_STYLE_UPRIGHT, accent->codepoint);
            if (metric == NULL) {
                break;
            }
            chosen = metric;
            family = step;
        }
    }

    txc_node *glyph = txc_arena_alloc(arena, sizeof(txc_node));
    txc_node *box = txc_arena_alloc(arena, sizeof(txc_node));
    const txc_node **children = txc_arena_alloc(arena, 2 * sizeof(*children));
    if (glyph == NULL || box == NULL || children == NULL) {
        return txc_alloc_fail(error);
    }
    txc_glyph_init(glyph, accent->codepoint, family, chosen, glyph_em, accent->command);
    txc_scaled drop = nucleus->ascent < txc_param(TXC_PARAMETER_X_HEIGHT, size)
                          ? nucleus->ascent
                          : txc_param(TXC_PARAMETER_X_HEIGHT, size);
    glyph->y = nucleus->ascent - drop;
    glyph->x = skew + txc_half(nucleus->width - glyph->width);
    nucleus->x = 0;
    nucleus->y = 0;

    children[0] = glyph;
    children[1] = nucleus;

    txc_box_init(box, range, children, 2);
    box->width = nucleus->width;
    box->ascent = txc_max(nucleus->ascent, glyph->y + glyph->ascent);
    box->descent = nucleus->descent;
    *out = box;
    return TEX_CORE_STATUS_OK;
}

/* Nodes one atom contributes to its list: the nucleus rendering plus one
 * box per script. Shared by the counting and building passes. */
/* Whether an Op atom's scripts place as limits in `style` (TeXbook
 * Appendix G rule 13: display style unless overridden). */
static bool txc_limits_active(const txc_item *item, int style) {
    if (item->atom_class != TXC_ATOM_OP) {
        return false;
    }
    return item->op_limits == TXC_LIMITS_ALWAYS || (item->op_limits == TXC_LIMITS_DISPLAY && style < TXC_STYLE_TEXT);
}

static size_t txc_atom_nodes(const txc_item *item, int style) {
    if (txc_limits_active(item, style) && (item->sup.kind != TXC_FIELD_EMPTY || item->sub.kind != TXC_FIELD_EMPTY)) {
        /* The whole operator-with-limits assembly is one box. */
        return 1;
    }
    size_t count = item->nucleus.kind != TXC_FIELD_EMPTY ? 1 : 0;
    count += item->sup.kind != TXC_FIELD_EMPTY ? 1 : 0;
    count += item->sub.kind != TXC_FIELD_EMPTY ? 1 : 0;
    return count;
}

/* Lays out one atom at `*cursor`, appending its nodes: the nucleus glyph
 * or box, then the script boxes shifted per TeXbook Appendix G rule 18
 * (tex.web's make_scripts, sections 756-759). The italic correction is in
 * the advance of a char nucleus — and offsets the superscript — unless a
 * subscript is present, which attaches flush and withholds it. */
static tex_core_status txc_atom(
    txc_arena *arena,
    const txc_item *item,
    int style,
    txc_scaled *cursor,
    const txc_node **children,
    size_t *index,
    tex_core_error *error
) {
    txc_mathsize size = txc_style_size(style);
    txc_scaled em = TXC_MATHSIZE_EM[size];
    txc_mathsize drop = style < TXC_STYLE_SCRIPT ? TXC_MATHSIZE_SCRIPT : TXC_MATHSIZE_SCRIPTSCRIPT;
    bool has_sup = item->sup.kind != TXC_FIELD_EMPTY;
    bool has_sub = item->sub.kind != TXC_FIELD_EMPTY;

    txc_scaled nucleus_width = 0;
    txc_scaled delta = 0;
    txc_scaled shift_up = 0;
    txc_scaled shift_down = 0;

    if (item->atom_class == TXC_ATOM_OP && item->nucleus.kind == TXC_FIELD_CHAR) {
        /* make_op (tex.web section 749): the operator glyph comes from
         * the Size1 face — Size2 in display style — always at the text
         * em, vertically centered on the axis. Its italic correction is
         * the script delta, exactly as for a character nucleus. */
        tex_core_family family = style < TXC_STYLE_TEXT ? TEX_CORE_FAMILY_SIZE2 : TEX_CORE_FAMILY_SIZE1;
        const txc_metric *metric = txc_metric_find(family, TEX_CORE_STYLE_UPRIGHT, item->nucleus.character.codepoint);
        if (metric == NULL) {
            return txc_fail(
                error,
                TEX_CORE_STATUS_UNSUPPORTED,
                &item->nucleus.range,
                "unsupported character U+%04X",
                (unsigned)item->nucleus.character.codepoint
            );
        }
        txc_node *glyph = txc_arena_alloc(arena, sizeof(txc_node));
        if (glyph == NULL) {
            return txc_alloc_fail(error);
        }
        txc_glyph_init(
            glyph,
            item->nucleus.character.codepoint,
            family,
            metric,
            TXC_MATHSIZE_EM[TXC_MATHSIZE_TEXT],
            item->nucleus.range
        );
        txc_scaled axis = txc_param(TXC_PARAMETER_AXIS_HEIGHT, size);
        glyph->x = *cursor;
        glyph->y = axis - txc_half(glyph->ascent - glyph->descent);
        children[(*index)++] = glyph;
        nucleus_width = glyph->width;
        delta = glyph->italic;
        /* The shifted glyph behaves as a box for script shifts. */
        shift_up = (glyph->ascent + glyph->y) - txc_param(TXC_PARAMETER_SUP_DROP, drop);
        shift_down = (glyph->descent - glyph->y) + txc_param(TXC_PARAMETER_SUB_DROP, drop);
    } else if (item->nucleus.kind == TXC_FIELD_CHAR) {
        const txc_metric *metric = txc_metric_find(
            item->nucleus.character.family,
            item->nucleus.character.style,
            item->nucleus.character.codepoint
        );
        if (metric == NULL) {
            return txc_fail(
                error,
                TEX_CORE_STATUS_UNSUPPORTED,
                &item->nucleus.range,
                "unsupported character U+%04X",
                (unsigned)item->nucleus.character.codepoint
            );
        }
        txc_node *glyph = txc_arena_alloc(arena, sizeof(txc_node));
        if (glyph == NULL) {
            return txc_alloc_fail(error);
        }
        txc_glyph_init(
            glyph,
            item->nucleus.character.codepoint,
            item->nucleus.character.family,
            metric,
            em,
            item->nucleus.range
        );
        glyph->style = item->nucleus.character.style;
        glyph->x = *cursor;
        children[(*index)++] = glyph;
        nucleus_width = glyph->width;
        delta = glyph->italic;
    } else if (item->nucleus.kind != TXC_FIELD_EMPTY) {
        txc_node *box;
        tex_core_status status;
        switch (item->nucleus.kind) {
        case TXC_FIELD_FRACTION:
            status = txc_fraction_box(arena, item->nucleus.fraction, style, item->nucleus.range, &box, error);
            break;
        case TXC_FIELD_FENCED:
            status = txc_fenced_box(arena, item->nucleus.fenced, style, item->nucleus.range, &box, error);
            break;
        case TXC_FIELD_RADICAL:
            status = txc_radical_box(arena, item->nucleus.radical, style, item->nucleus.range, &box, error);
            break;
        case TXC_FIELD_ACCENT:
            status = txc_accent_box(arena, item->nucleus.accent, style, item->nucleus.range, &box, error);
            break;
        case TXC_FIELD_ARRAY:
            status = txc_array_box(arena, item->nucleus.array, style, item->nucleus.range, &box, error);
            break;
        case TXC_FIELD_TEXT:
            status = txc_mlist(arena, &item->nucleus.list, style, false, item->nucleus.range, &box, error);
            break;
        case TXC_FIELD_DELIMITER:
            status = txc_delimiter_boxed(
                arena,
                &item->nucleus.sized.delimiter,
                style,
                txc_big_target(item->nucleus.sized.size),
                item->nucleus.range,
                &box,
                error
            );
            break;
        case TXC_FIELD_LIST:
        default:
            status = txc_mlist(arena, &item->nucleus.list, style, true, item->nucleus.range, &box, error);
            break;
        }
        if (status != TEX_CORE_STATUS_OK) {
            return status;
        }
        box->x = *cursor;
        box->y = 0;
        children[(*index)++] = box;
        nucleus_width = box->width;
        shift_up = box->ascent - txc_param(TXC_PARAMETER_SUP_DROP, drop);
        shift_down = box->descent + txc_param(TXC_PARAMETER_SUB_DROP, drop);
    } else {
        shift_up = -txc_param(TXC_PARAMETER_SUP_DROP, drop);
        shift_down = txc_param(TXC_PARAMETER_SUB_DROP, drop);
    }

    if (!has_sup && !has_sub) {
        *cursor += nucleus_width + delta;
        return TEX_CORE_STATUS_OK;
    }

    if (txc_limits_active(item, style)) {
        /* make_limits (tex.web section 750, Appendix G rule 13a): the
         * scripts become boxes centered above and below the operator in
         * the common width, the superscript nudged half the italic
         * correction right and the subscript half left, separated by the
         * bigOpSpacing clearances with the spacing5 pads joining the
         * assembly's extent. The whole assembly is one box. */
        txc_node *nucleus_node = (txc_node *)children[--(*index)];
        txc_scaled op_top = nucleus_node->ascent + nucleus_node->y;
        txc_scaled op_bottom = nucleus_node->descent - nucleus_node->y;
        txc_node *sup = NULL;
        txc_node *sub = NULL;
        if (has_sup) {
            tex_core_status status = txc_clean_box(arena, &item->sup, txc_sup_style(style), &sup, error);
            if (status != TEX_CORE_STATUS_OK) {
                return status;
            }
        }
        if (has_sub) {
            tex_core_status status = txc_clean_box(arena, &item->sub, txc_sub_style(style), &sub, error);
            if (status != TEX_CORE_STATUS_OK) {
                return status;
            }
        }
        txc_scaled width = nucleus_width;
        if (sup != NULL) {
            width = txc_max(width, sup->width);
        }
        if (sub != NULL) {
            width = txc_max(width, sub->width);
        }
        nucleus_node->x = txc_half(width - nucleus_width);
        txc_scaled ascent = op_top;
        txc_scaled descent = op_bottom;
        txc_scaled pad = txc_param(TXC_PARAMETER_BIG_OP_SPACING5, size);
        if (sup != NULL) {
            txc_scaled clearance = txc_max(
                txc_param(TXC_PARAMETER_BIG_OP_SPACING1, size),
                txc_param(TXC_PARAMETER_BIG_OP_SPACING3, size) - sup->descent
            );
            sup->x = txc_half(width - sup->width) + txc_half(delta);
            sup->y = op_top + clearance + sup->descent;
            ascent = sup->y + sup->ascent + pad;
        }
        if (sub != NULL) {
            txc_scaled clearance = txc_max(
                txc_param(TXC_PARAMETER_BIG_OP_SPACING2, size),
                txc_param(TXC_PARAMETER_BIG_OP_SPACING4, size) - sub->ascent
            );
            sub->x = txc_half(width - sub->width) - txc_half(delta);
            sub->y = -(op_bottom + clearance + sub->ascent);
            descent = -sub->y + sub->descent + pad;
        }

        txc_node *assembly = txc_arena_alloc(arena, sizeof(txc_node));
        const txc_node **parts = txc_arena_alloc(arena, 3 * sizeof(*parts));
        if (assembly == NULL || parts == NULL) {
            return txc_alloc_fail(error);
        }
        size_t part = 0;
        parts[part++] = nucleus_node;
        if (item->sub_first) {
            if (sub != NULL) {
                parts[part++] = sub;
            }
            if (sup != NULL) {
                parts[part++] = sup;
            }
        } else {
            if (sup != NULL) {
                parts[part++] = sup;
            }
            if (sub != NULL) {
                parts[part++] = sub;
            }
        }
        assembly->kind = TEX_CORE_NODE_HBOX;
        assembly->x = *cursor;
        assembly->y = 0;
        assembly->codepoint = 0;
        assembly->style = TEX_CORE_STYLE_UPRIGHT;
        assembly->family = TEX_CORE_FAMILY_MAIN;
        assembly->size = 0;
        assembly->width = width;
        assembly->ascent = ascent;
        assembly->descent = descent;
        assembly->italic = 0;
        assembly->range = item->range;
        assembly->children = parts;
        assembly->child_count = part;
        children[(*index)++] = assembly;
        *cursor += width;
        return TEX_CORE_STATUS_OK;
    }

    txc_node *sup = NULL;
    txc_node *sub = NULL;
    if (has_sup) {
        tex_core_status status = txc_script_box(arena, &item->sup, txc_sup_style(style), &sup, error);
        if (status != TEX_CORE_STATUS_OK) {
            return status;
        }
    }
    if (has_sub) {
        tex_core_status status = txc_script_box(arena, &item->sub, txc_sub_style(style), &sub, error);
        if (status != TEX_CORE_STATUS_OK) {
            return status;
        }
    }

    txc_scaled x_height = txc_param(TXC_PARAMETER_X_HEIGHT, size);
    if (has_sup) {
        txc_parameter minimum = TXC_PARAMETER_SUP2;
        if (style % 2 == 1) {
            minimum = TXC_PARAMETER_SUP3;
        } else if (style < TXC_STYLE_TEXT) {
            minimum = TXC_PARAMETER_SUP1;
        }
        shift_up = txc_max(shift_up, txc_param(minimum, size));
        shift_up = txc_max(shift_up, sup->descent + x_height / 4);
    }
    if (has_sub && !has_sup) {
        shift_down = txc_max(shift_down, txc_param(TXC_PARAMETER_SUB1, size));
        shift_down = txc_max(shift_down, sub->ascent - (x_height * 4) / 5);
    } else if (has_sub) {
        /* Both scripts: keep 4 rule thicknesses between them, then raise
         * the pair so the superscript's bottom clears 4/5 of x-height. */
        shift_down = txc_max(shift_down, txc_param(TXC_PARAMETER_SUB2, size));
        txc_scaled thickness = txc_param(TXC_PARAMETER_RULE_THICKNESS, size);
        txc_scaled clearance = 4 * thickness - ((shift_up - sup->descent) - (sub->ascent - shift_down));
        if (clearance > 0) {
            shift_down += clearance;
            txc_scaled raise = (x_height * 4) / 5 - (shift_up - sup->descent);
            if (raise > 0) {
                shift_up += raise;
                shift_down -= raise;
            }
        }
    }

    /* A char nucleus keeps its italic kern before a lone superscript
     * (tex.web section 755: the kern is appended when the subscript is
     * empty); with a subscript present the correction instead offsets the
     * superscript to the right of the subscript. */
    txc_scaled base = *cursor + nucleus_width;
    txc_scaled advance;
    if (has_sup && !has_sub) {
        sup->x = base + delta;
        sup->y = shift_up;
        advance = nucleus_width + delta + sup->width;
    } else if (has_sub && !has_sup) {
        sub->x = base;
        sub->y = -shift_down;
        advance = nucleus_width + sub->width;
    } else {
        sup->x = base + delta;
        sup->y = shift_up;
        sub->x = base;
        sub->y = -shift_down;
        advance = nucleus_width + txc_max(delta + sup->width, sub->width);
    }
    if (item->sub_first) {
        if (sub != NULL) {
            children[(*index)++] = sub;
        }
        if (sup != NULL) {
            children[(*index)++] = sup;
        }
    } else {
        if (sup != NULL) {
            children[(*index)++] = sup;
        }
        if (sub != NULL) {
            children[(*index)++] = sub;
        }
    }
    *cursor += advance;
    return TEX_CORE_STATUS_OK;
}

/* Lays out one list into a box node. Math lists resolve Bin demotion and
 * insert the inter-atom spacing kerns of their own style; sub-lists set
 * script styles recursively. */
static tex_core_status txc_mlist(
    txc_arena *arena,
    const txc_list *list,
    int style,
    bool math,
    tex_core_range range,
    txc_node **out,
    tex_core_error *error
) {
    if (math) {
        txc_resolve_bin_atoms(list);
    }

    size_t child_capacity = 0;
    const txc_item *previous_atom = NULL;
    for (const txc_item *item = list->head; item != NULL; item = item->next) {
        if (item->kind == TXC_ITEM_ATOM) {
            child_capacity += txc_atom_nodes(item, style);
            if (math && previous_atom != NULL &&
                txc_inter_atom_width(TXC_MATH_SPACING[previous_atom->atom_class][item->atom_class], style) != 0) {
                child_capacity += 1;
            }
            previous_atom = item;
        } else {
            child_capacity += 1;
        }
    }

    txc_node *hbox = txc_arena_alloc(arena, sizeof(txc_node));
    const txc_node **children = NULL;
    if (child_capacity > 0 && hbox != NULL) {
        children = txc_arena_alloc(arena, child_capacity * sizeof(*children));
    }
    if (hbox == NULL || (child_capacity > 0 && children == NULL)) {
        return txc_alloc_fail(error);
    }

    txc_scaled cursor = 0;
    size_t index = 0;
    previous_atom = NULL;
    for (const txc_item *item = list->head; item != NULL; item = item->next) {
        if (math && item->kind == TXC_ITEM_ATOM && previous_atom != NULL) {
            /* TeX inserts the pair's space directly before the right atom,
             * after (never instead of) any explicit spacing between the
             * two (tex.web section 766). The kern's source range is the
             * gap between the atoms. */
            txc_scaled width =
                txc_inter_atom_width(TXC_MATH_SPACING[previous_atom->atom_class][item->atom_class], style);
            if (width != 0) {
                txc_node *space = txc_arena_alloc(arena, sizeof(txc_node));
                if (space == NULL) {
                    return txc_alloc_fail(error);
                }
                tex_core_range gap = {previous_atom->range.end, item->range.begin};
                txc_kern_init(space, cursor, width, gap);
                cursor += width;
                children[index++] = space;
            }
        }

        if (item->kind == TXC_ITEM_ATOM) {
            tex_core_status status = txc_atom(arena, item, style, &cursor, children, &index, error);
            if (status != TEX_CORE_STATUS_OK) {
                return status;
            }
            previous_atom = item;
        } else {
            txc_node *node = txc_arena_alloc(arena, sizeof(txc_node));
            if (node == NULL) {
                return txc_alloc_fail(error);
            }
            txc_kern_init(node, cursor, txc_space_width(item->space, txc_style_size(style)), item->range);
            cursor += node->width;
            children[index++] = node;
        }
    }

    txc_scaled ascent = 0;
    txc_scaled descent = 0;
    for (size_t child = 0; child < index; child++) {
        const txc_node *node = children[child];
        if (node->kind == TEX_CORE_NODE_KERN) {
            continue;
        }
        ascent = txc_max(ascent, node->ascent + node->y);
        descent = txc_max(descent, node->descent - node->y);
    }

    txc_box_init(hbox, range, children, index);
    hbox->width = cursor;
    hbox->ascent = ascent;
    hbox->descent = descent;

    *out = hbox;
    return TEX_CORE_STATUS_OK;
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
     * plain horizontal text list. Display math opens in display style,
     * inline math in text style. */
    bool math = mode != TEX_CORE_MODE_DOCUMENT;
    int style = mode == TEX_CORE_MODE_MATH_DISPLAY ? TXC_STYLE_DISPLAY : TXC_STYLE_TEXT;
    tex_core_range range = {0, source_length};
    txc_node *hbox;
    tex_core_status status = txc_mlist(arena, list, style, math, range, &hbox, error);
    if (status != TEX_CORE_STATUS_OK) {
        return status;
    }
    *root = hbox;
    return TEX_CORE_STATUS_OK;
}
