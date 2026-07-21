/* Tokenizer. Catcodes are fixed (plan section 5.2): the walking skeleton
 * scans escapes, whitespace, and characters; user catcode changes arrive
 * with the 3.0.0 programmable layer. */

#ifndef TXC_TOKEN_H
#define TXC_TOKEN_H

#include <stddef.h>
#include <stdint.h>

#include "tex_core.h"

typedef enum txc_token_kind {
    TXC_TOKEN_CHARACTER = 0,
    TXC_TOKEN_CONTROL = 1,
    TXC_TOKEN_SPACE = 2,
    TXC_TOKEN_PAR = 3,
    TXC_TOKEN_END = 4
} txc_token_kind;

typedef struct txc_token {
    txc_token_kind kind;
    /* TXC_TOKEN_CHARACTER only. */
    uint32_t codepoint;
    /* TXC_TOKEN_CONTROL only: the name after the backslash, borrowed from
     * the source buffer. */
    const uint8_t *name;
    size_t name_length;
    tex_core_range range;
} txc_token;

typedef struct txc_scanner {
    const uint8_t *source;
    size_t length;
    size_t position;
} txc_scanner;

void txc_scanner_init(txc_scanner *scanner, const uint8_t *source, size_t length);

/* Scans one token, returning TEX_CORE_STATUS_OK or a structured error.
 * After the final token, every call yields TXC_TOKEN_END. */
tex_core_status txc_scan(txc_scanner *scanner, txc_token *token, tex_core_error *error);

#endif
