/* Parser: token stream to semantic list. The semantic lists are internal;
 * the public contract is the render tree only (plan section 5.2). The
 * grammar covers classed math atoms (characters and symbol commands) plus
 * explicit spacing; document mode still typesets ordinary text atoms only. */

#ifndef TXC_PARSE_H
#define TXC_PARSE_H

#include <stddef.h>
#include <stdint.h>

#include "memory.h"
#include "tex_core.h"

typedef enum txc_item_kind { TXC_ITEM_ATOM = 0, TXC_ITEM_SPACE = 1 } txc_item_kind;

/* TeX math atom classes (TeXbook chapter 17). The parser produces Ord, Bin,
 * Rel, Open, Close, and Punct atoms today; Op arrives with big operators
 * and function names, Inner with fractions and \ldots. The spacing table in
 * layout already covers all eight classes. Document-mode atoms always carry
 * TXC_ATOM_ORD; atom classes never affect text layout. */
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

/* Explicit spacing amounts. WORD is the interword space; the math spaces
 * are the classic mu-based commands resolved against the fixed em. */
typedef enum txc_space {
    TXC_SPACE_WORD = 0,
    TXC_SPACE_THIN = 1,
    TXC_SPACE_MEDIUM = 2,
    TXC_SPACE_THICK = 3,
    TXC_SPACE_NEGATIVE_THIN = 4,
    TXC_SPACE_QUAD = 5,
    TXC_SPACE_QQUAD = 6
} txc_space;

typedef struct txc_item {
    txc_item_kind kind;
    /* TXC_ITEM_ATOM only. Layout resolves contextual Bin demotion in
     * place, so atom_class is final only once layout runs. */
    uint32_t codepoint;
    tex_core_style style;
    txc_atom_class atom_class;
    /* TXC_ITEM_SPACE only. */
    txc_space space;
    tex_core_range range;
    struct txc_item *next;
} txc_item;

typedef struct txc_list {
    txc_item *head;
    txc_item *tail;
    size_t count;
} txc_list;

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
