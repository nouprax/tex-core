/* Parser: token stream to semantic list. The semantic lists are internal;
 * the public contract is the render tree only (plan section 5.2). The
 * walking-skeleton grammar is ordinary atoms plus explicit spacing. */

#ifndef TXC_PARSE_H
#define TXC_PARSE_H

#include <stddef.h>
#include <stdint.h>

#include "memory.h"
#include "tex_core.h"

typedef enum txc_item_kind { TXC_ITEM_ATOM = 0, TXC_ITEM_SPACE = 1 } txc_item_kind;

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
    /* TXC_ITEM_ATOM only. */
    uint32_t codepoint;
    tex_core_style style;
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
