/* Parser: token stream to semantic list. The semantic lists are internal;
 * the public contract is the render tree only (plan section 5.2). The
 * grammar covers classed math atoms (characters and symbol commands) with
 * braced groups, superscripts, subscripts, and fractions, plus explicit
 * spacing; document mode still typesets ordinary text atoms only. */

#ifndef TXC_PARSE_H
#define TXC_PARSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "memory.h"
#include "tex_core.h"

typedef enum txc_item_kind { TXC_ITEM_ATOM = 0, TXC_ITEM_SPACE = 1 } txc_item_kind;

/* TeX math atom classes (TeXbook chapter 17). The parser produces all
 * eight classes: Op comes from big operators and function names, Inner
 * from fractions and \left/\right fences. The spacing table in layout
 * covers all eight. Document-mode atoms always carry TXC_ATOM_ORD; atom
 * classes never affect text layout. */
typedef enum txc_atom_class {
    TXC_ATOM_ORD = 0,
    TXC_ATOM_OP = 1,
    TXC_ATOM_BIN = 2,
    TXC_ATOM_REL = 3,
    TXC_ATOM_OPEN = 4,
    TXC_ATOM_CLOSE = 5,
    TXC_ATOM_PUNCT = 6,
    TXC_ATOM_INNER = 7
} txc_atom_class;

/* When a big operator or limit-taking function name places its scripts
 * as limits above and below: in display style only (the TeX default for
 * sums and \lim), always (\limits), or never (integrals, \sin, and
 * \nolimits). */
typedef enum txc_op_limits { TXC_LIMITS_DISPLAY = 0, TXC_LIMITS_NEVER = 1, TXC_LIMITS_ALWAYS = 2 } txc_op_limits;

/* Explicit spacing amounts. WORD is the interword space; the math spaces
 * are the classic mu-based commands resolved against the style's quad. */
typedef enum txc_space {
    TXC_SPACE_WORD = 0,
    TXC_SPACE_THIN = 1,
    TXC_SPACE_MEDIUM = 2,
    TXC_SPACE_THICK = 3,
    TXC_SPACE_NEGATIVE_THIN = 4,
    TXC_SPACE_QUAD = 5,
    TXC_SPACE_QQUAD = 6
} txc_space;

struct txc_item;

typedef struct txc_list {
    struct txc_item *head;
    struct txc_item *tail;
    size_t count;
} txc_list;

/* How a delimiter grows past its text-size glyph (the Computer Modern
 * successor chains, mirrored from KaTeX's delimiter sequences): NEVER
 * stops at its largest size-face glyph, LARGE tries the size faces and
 * then the piece assembly, ALWAYS goes straight from the small glyph to
 * the assembly. */
typedef enum txc_delimiter_ladder {
    TXC_LADDER_NEVER = 0,
    TXC_LADDER_LARGE = 1,
    TXC_LADDER_ALWAYS = 2
} txc_delimiter_ladder;

/* One resolved delimiter: the main-family text glyph, the growth ladder,
 * and the extensible pieces (0 marks an absent piece; `middle` is only
 * the braces' waist). `small` 0 is the null delimiter `.`. */
typedef struct txc_delimiter {
    uint32_t small;
    txc_delimiter_ladder ladder;
    uint32_t top;
    uint32_t repeat;
    uint32_t bottom;
    uint32_t middle;
    tex_core_family piece_family;
} txc_delimiter;

/* One noad field (TeXbook chapter 17): an atom's nucleus, superscript, or
 * subscript is empty, a single character, a braced sub-list, a fraction
 * construct, a fixed-size delimiter, or a \left/\right fence. */
typedef enum txc_field_kind {
    TXC_FIELD_EMPTY = 0,
    TXC_FIELD_CHAR = 1,
    TXC_FIELD_LIST = 2,
    TXC_FIELD_FRACTION = 3,
    TXC_FIELD_DELIMITER = 4,
    TXC_FIELD_FENCED = 5,
    TXC_FIELD_RADICAL = 6,
    TXC_FIELD_ACCENT = 7,
    TXC_FIELD_TEXT = 8
} txc_field_kind;

/* The face a style switch applies to its argument's letters and digits.
 * TEXT is \text: document-rule content, not a face rewrite. */
typedef enum txc_face {
    TXC_FACE_RM = 0,
    TXC_FACE_BF = 1,
    TXC_FACE_IT = 2,
    TXC_FACE_CAL = 3,
    TXC_FACE_BB = 4,
    TXC_FACE_SF = 5,
    TXC_FACE_TT = 6,
    TXC_FACE_TEXT = 7
} txc_face;

struct txc_fraction;
struct txc_fenced;
struct txc_radical;
struct txc_accent;

/* An explicit-size delimiter (\bigl … \Biggr): the delimiter and the
 * plain TeX size step 1-4 (\big through \Bigg). */
typedef struct txc_sized_delimiter {
    txc_delimiter delimiter;
    int size;
} txc_sized_delimiter;

/* TXC_FIELD_CHAR payload. `family` is main except inside a style-switch
 * argument, whose face the parser stamps as the character is classified
 * (the innermost math alphabet wins); `text_face` marks a bare \text
 * character, which an outer style switch leaves alone like all \text
 * content. */
typedef struct txc_char_field {
    uint32_t codepoint;
    tex_core_style style;
    tex_core_family family;
    bool text_face;
} txc_char_field;

/* The anonymous union is standard C11 (6.7.2.1p13); MSVC still flags it
 * as its historical pre-C11 extension under /W4 (C4201), so the spurious
 * warning is silenced for exactly this declaration. */
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4201)
#endif
typedef struct txc_field {
    txc_field_kind kind;
    /* Exactly one member is active, selected by `kind`; constructors and
     * txc_field_reset always initialize the tag and the whole payload
     * together, so no inactive state survives a variant switch. */
    union {
        /* TXC_FIELD_CHAR. */
        txc_char_field character;
        /* TXC_FIELD_LIST and TXC_FIELD_TEXT. */
        txc_list list;
        /* TXC_FIELD_FRACTION. */
        struct txc_fraction *fraction;
        /* TXC_FIELD_DELIMITER. */
        txc_sized_delimiter sized;
        /* TXC_FIELD_FENCED. */
        struct txc_fenced *fenced;
        /* TXC_FIELD_RADICAL. */
        struct txc_radical *radical;
        /* TXC_FIELD_ACCENT. */
        struct txc_accent *accent;
    };
    /* The field's own source construct: the character or symbol command,
     * the braced group including its braces, the fraction command through
     * its last argument, the size command through its delimiter, \left
     * through the \right delimiter, or — for a script field — the script
     * mark through the end of its argument. Empty fields have an empty
     * range at the attachment point. */
    tex_core_range range;
} txc_field;
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

/* A radical (\sqrt): the mandatory radicand and the optional index of
 * the LaTeX bracket form. `command` is the \sqrt token — the source the
 * sign and the bar are attributed to. */
typedef struct txc_radical {
    txc_field index;
    txc_field argument;
    tex_core_range command;
} txc_radical;

/* A math accent (\hat through \vec): the accent glyph and the
 * accented nucleus. Wide accents (\widehat, \widetilde) grow through
 * the size-face width steps. `command` is the accent command's token. */
typedef struct txc_accent {
    uint32_t codepoint;
    bool wide;
    txc_field argument;
    tex_core_range command;
} txc_accent;

/* A \left/\right fence: both delimiters with their own token ranges and
 * the enclosed sub-list. */
typedef struct txc_fenced {
    txc_delimiter left;
    tex_core_range left_range;
    txc_delimiter right;
    tex_core_range right_range;
    txc_list list;
} txc_fenced;

/* The style a fraction command forces on its own layout: \frac follows
 * the surrounding style, \dfrac and \tfrac force display and text style
 * exactly as \displaystyle/\textstyle (uncramped). */
typedef enum txc_fraction_style {
    TXC_FRACTION_STYLE_AUTO = 0,
    TXC_FRACTION_STYLE_DISPLAY = 1,
    TXC_FRACTION_STYLE_TEXT = 2
} txc_fraction_style;

/* A generalized fraction (TeXbook chapter 17 fraction noads): numerator
 * over denominator, both mandatory. `command` is the fraction command's
 * own token — the source the bar is attributed to. A binomial (`binom`)
 * has no bar and wraps the pair in delim1/delim2-sized parentheses. */
typedef struct txc_fraction {
    txc_field num;
    txc_field den;
    txc_fraction_style style;
    bool binom;
    tex_core_range command;
} txc_fraction;

typedef struct txc_item {
    txc_item_kind kind;
    /* TXC_ITEM_ATOM only. Layout resolves contextual Bin demotion in
     * place, so atom_class is final only once layout runs. Scripts never
     * change an atom's class. `sub_first` records which script appeared
     * first in source, because child order is source order. */
    txc_atom_class atom_class;
    txc_field nucleus;
    txc_field sup;
    txc_field sub;
    bool sub_first;
    /* TXC_ATOM_OP only: how the scripts place as limits. */
    txc_op_limits op_limits;
    /* TXC_ITEM_SPACE only. */
    txc_space space;
    /* The whole construct: nucleus through the last script. */
    tex_core_range range;
    struct txc_item *next;
} txc_item;

/* Verifies the command dispatch table (a test seam, like
 * txc_allocation_limit: the table is maintained by hand). Checks that the
 * table is strictly sorted — the binary search is only correct over a
 * sorted table — and that every row's class index is in bounds and lands
 * on the payload row carrying the same command name, so an insertion into
 * one of the typed tables cannot silently redirect dispatch. */
bool txc_command_table_sorted(void);

/* Parses the whole source into `list`, allocating items from `arena`. */
tex_core_status txc_parse(
    txc_arena *arena,
    const uint8_t *source,
    size_t length,
    tex_core_mode mode,
    txc_list *list,
    tex_core_error *error
);

#endif
