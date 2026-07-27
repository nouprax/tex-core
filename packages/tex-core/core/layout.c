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
    node->size = 0;
    node->width = width;
    node->ascent = 0;
    node->descent = 0;
    node->italic = 0;
    node->range = range;
    node->children = NULL;
    node->child_count = 0;
}

static tex_core_status txc_fraction_box(
    txc_arena *arena,
    const txc_fraction *fraction,
    int style,
    tex_core_range range,
    txc_node **out,
    tex_core_error *error
);

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
    if (field->kind == TXC_FIELD_CHAR) {
        const txc_metric *metric = txc_metric_find(field->style, field->codepoint);
        if (metric == NULL) {
            return txc_fail(
                error,
                TEX_CORE_STATUS_UNSUPPORTED,
                &field->range,
                "unsupported character U+%04X",
                (unsigned)field->codepoint
            );
        }
        txc_scaled em = TXC_MATHSIZE_EM[txc_style_size(style)];
        box = txc_arena_alloc(arena, sizeof(txc_node));
        const txc_node **children = txc_arena_alloc(arena, sizeof(*children));
        txc_node *glyph = txc_arena_alloc(arena, sizeof(txc_node));
        if (box == NULL || children == NULL || glyph == NULL) {
            return txc_alloc_fail(error);
        }
        glyph->kind = TEX_CORE_NODE_GLYPH;
        glyph->x = 0;
        glyph->y = 0;
        glyph->codepoint = field->codepoint;
        glyph->style = field->style;
        glyph->size = em;
        glyph->width = txc_em(metric->width, em);
        glyph->ascent = txc_em(metric->height, em);
        glyph->descent = txc_em(metric->depth, em);
        glyph->italic = txc_em(metric->italic, em);
        glyph->range = field->range;
        glyph->children = NULL;
        glyph->child_count = 0;
        children[0] = glyph;
        box->kind = TEX_CORE_NODE_HBOX;
        box->x = 0;
        box->y = 0;
        box->codepoint = 0;
        box->style = TEX_CORE_STYLE_UPRIGHT;
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

    txc_scaled thickness = txc_param(TXC_PARAMETER_RULE_THICKNESS, size);
    txc_scaled axis = txc_param(TXC_PARAMETER_AXIS_HEIGHT, size);
    txc_scaled shift_up;
    txc_scaled shift_down;
    if (fraction_style < TXC_STYLE_TEXT) {
        shift_up = txc_param(TXC_PARAMETER_NUM1, size);
        shift_down = txc_param(TXC_PARAMETER_DENOM1, size);
    } else {
        shift_up = txc_param(TXC_PARAMETER_NUM2, size);
        shift_down = txc_param(TXC_PARAMETER_DENOM2, size);
    }
    txc_scaled clearance = fraction_style < TXC_STYLE_TEXT ? 3 * thickness : thickness;
    txc_scaled delta = txc_half(thickness);
    txc_scaled gap_up = clearance - ((shift_up - num->descent) - (axis + delta));
    if (gap_up > 0) {
        shift_up += gap_up;
    }
    txc_scaled gap_down = clearance - ((axis - delta) - (den->ascent - shift_down));
    if (gap_down > 0) {
        shift_down += gap_down;
    }

    /* The parts share the wider width, the narrower centered — the
     * resolved geometry of TeX's rebox fil glue. */
    txc_scaled width = txc_max(num->width, den->width);
    num->x = TXC_NULL_DELIMITER_SPACE + txc_half(width - num->width);
    num->y = shift_up;
    den->x = TXC_NULL_DELIMITER_SPACE + txc_half(width - den->width);
    den->y = -shift_down;

    txc_node *box = txc_arena_alloc(arena, sizeof(txc_node));
    const txc_node **children = txc_arena_alloc(arena, 5 * sizeof(*children));
    txc_node *left = txc_arena_alloc(arena, sizeof(txc_node));
    txc_node *rule = txc_arena_alloc(arena, sizeof(txc_node));
    txc_node *right = txc_arena_alloc(arena, sizeof(txc_node));
    if (box == NULL || children == NULL || left == NULL || rule == NULL || right == NULL) {
        return txc_alloc_fail(error);
    }
    tex_core_range left_edge = {fraction->command.begin, fraction->command.begin};
    txc_kern_init(left, 0, TXC_NULL_DELIMITER_SPACE, left_edge);
    tex_core_range right_edge = {range.end, range.end};
    txc_kern_init(right, TXC_NULL_DELIMITER_SPACE + width, TXC_NULL_DELIMITER_SPACE, right_edge);

    rule->kind = TEX_CORE_NODE_RULE;
    rule->x = TXC_NULL_DELIMITER_SPACE;
    rule->y = axis + delta - thickness;
    rule->codepoint = 0;
    rule->style = TEX_CORE_STYLE_UPRIGHT;
    rule->size = 0;
    rule->width = width;
    rule->ascent = thickness;
    rule->descent = 0;
    rule->italic = 0;
    rule->range = fraction->command;
    rule->children = NULL;
    rule->child_count = 0;

    /* Child order is source order: the bar's source is the command token,
     * which precedes both arguments. */
    children[0] = left;
    children[1] = rule;
    children[2] = num;
    children[3] = den;
    children[4] = right;

    box->kind = TEX_CORE_NODE_HBOX;
    box->x = 0;
    box->y = 0;
    box->codepoint = 0;
    box->style = TEX_CORE_STYLE_UPRIGHT;
    box->size = 0;
    box->width = width + 2 * TXC_NULL_DELIMITER_SPACE;
    box->ascent = shift_up + num->ascent;
    box->descent = shift_down + den->descent;
    box->italic = 0;
    box->range = range;
    box->children = children;
    box->child_count = 5;

    *out = box;
    return TEX_CORE_STATUS_OK;
}

/* Nodes one atom contributes to its list: the nucleus rendering plus one
 * box per script. Shared by the counting and building passes. */
static size_t txc_atom_nodes(const txc_item *item) {
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

    if (item->nucleus.kind == TXC_FIELD_CHAR) {
        const txc_metric *metric = txc_metric_find(item->nucleus.style, item->nucleus.codepoint);
        if (metric == NULL) {
            return txc_fail(
                error,
                TEX_CORE_STATUS_UNSUPPORTED,
                &item->nucleus.range,
                "unsupported character U+%04X",
                (unsigned)item->nucleus.codepoint
            );
        }
        txc_node *glyph = txc_arena_alloc(arena, sizeof(txc_node));
        if (glyph == NULL) {
            return txc_alloc_fail(error);
        }
        glyph->kind = TEX_CORE_NODE_GLYPH;
        glyph->x = *cursor;
        glyph->y = 0;
        glyph->codepoint = item->nucleus.codepoint;
        glyph->style = item->nucleus.style;
        glyph->size = em;
        glyph->width = txc_em(metric->width, em);
        glyph->ascent = txc_em(metric->height, em);
        glyph->descent = txc_em(metric->depth, em);
        glyph->italic = txc_em(metric->italic, em);
        glyph->range = item->nucleus.range;
        glyph->children = NULL;
        glyph->child_count = 0;
        children[(*index)++] = glyph;
        nucleus_width = glyph->width;
        delta = glyph->italic;
    } else if (item->nucleus.kind == TXC_FIELD_LIST || item->nucleus.kind == TXC_FIELD_FRACTION) {
        txc_node *box;
        tex_core_status status;
        if (item->nucleus.kind == TXC_FIELD_LIST) {
            status = txc_mlist(arena, &item->nucleus.list, style, true, item->nucleus.range, &box, error);
        } else {
            status = txc_fraction_box(arena, item->nucleus.fraction, style, item->nucleus.range, &box, error);
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
            child_capacity += txc_atom_nodes(item);
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

    hbox->kind = TEX_CORE_NODE_HBOX;
    hbox->x = 0;
    hbox->y = 0;
    hbox->codepoint = 0;
    hbox->style = TEX_CORE_STYLE_UPRIGHT;
    hbox->size = 0;
    hbox->width = cursor;
    hbox->ascent = ascent;
    hbox->descent = descent;
    hbox->italic = 0;
    hbox->range = range;
    hbox->children = children;
    hbox->child_count = index;

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
