#include "parse.h"

#include <stdio.h>
#include <string.h>

#include "error.h"
#include "token.h"

/* Characters with special TeX meaning that the walking skeleton does not
 * cover yet. Rejecting them keeps the supported surface honest: nothing is
 * silently skipped or demoted to a literal (plan section 5.1). */
static const char TXC_RESERVED[] = "#$%&^_{}~";

static bool txc_reserved(uint32_t codepoint) {
    return codepoint < 0x80 && strchr(TXC_RESERVED, (int)codepoint) != NULL;
}

static bool txc_ordinary(uint32_t codepoint, tex_core_mode mode) {
    if ((codepoint >= 'A' && codepoint <= 'Z') || (codepoint >= 'a' && codepoint <= 'z')) {
        return true;
    }
    if (codepoint >= '0' && codepoint <= '9') {
        return true;
    }
    if (codepoint == '.') {
        return true;
    }
    /* In math, comma is a Punct atom whose spacing is atom-class spacing —
     * outside the skeleton's ordinary-atoms-only scope. */
    return codepoint == ',' && mode == TEX_CORE_MODE_DOCUMENT;
}

static tex_core_style txc_atom_style(uint32_t codepoint, tex_core_mode mode) {
    if (mode == TEX_CORE_MODE_DOCUMENT) {
        return TEX_CORE_STYLE_UPRIGHT;
    }
    /* Math letters take the math italic face; digits and punctuation stay
     * upright, as in TeX's default \mathcode assignments. */
    if ((codepoint >= 'A' && codepoint <= 'Z') || (codepoint >= 'a' && codepoint <= 'z')) {
        return TEX_CORE_STYLE_ITALIC;
    }
    return TEX_CORE_STYLE_UPRIGHT;
}

typedef struct txc_spacing_command {
    const char *name;
    txc_space space;
} txc_spacing_command;

static const txc_spacing_command TXC_SPACING_COMMANDS[] = {
    {" ", TXC_SPACE_WORD},
    {"!", TXC_SPACE_NEGATIVE_THIN},
    {",", TXC_SPACE_THIN},
    {":", TXC_SPACE_MEDIUM},
    {";", TXC_SPACE_THICK},
    {"qquad", TXC_SPACE_QQUAD},
    {"quad", TXC_SPACE_QUAD},
};

static const txc_spacing_command *txc_spacing(const uint8_t *name, size_t name_length) {
    for (size_t index = 0; index < sizeof(TXC_SPACING_COMMANDS) / sizeof(TXC_SPACING_COMMANDS[0]); index++) {
        const txc_spacing_command *command = &TXC_SPACING_COMMANDS[index];
        if (strlen(command->name) == name_length && memcmp(command->name, name, name_length) == 0) {
            return command;
        }
    }
    return NULL;
}

static txc_item *txc_append(txc_arena *arena, txc_list *list) {
    txc_item *item = txc_arena_alloc(arena, sizeof(txc_item));
    if (item == NULL) {
        return NULL;
    }
    if (list->tail != NULL) {
        list->tail->next = item;
    } else {
        list->head = item;
    }
    list->tail = item;
    list->count += 1;
    return item;
}

/* Renders a control-sequence name into a bounded ASCII-safe form for error
 * messages: letters pass through, anything else appears as <U+XXXX>. */
static void txc_command_label(const uint8_t *name, size_t name_length, char *buffer, size_t capacity) {
    size_t written = 0;
    for (size_t index = 0; index < name_length && written + 12 < capacity; index++) {
        uint8_t byte = name[index];
        if (byte >= 0x20 && byte < 0x7F) {
            buffer[written++] = (char)byte;
        } else {
            int printed = snprintf(buffer + written, capacity - written, "<0x%02X>", byte);
            written += printed > 0 ? (size_t)printed : 0;
        }
    }
    buffer[written] = '\0';
}

tex_core_status txc_parse(
    txc_arena *arena,
    const uint8_t *source,
    size_t length,
    tex_core_mode mode,
    txc_list *list,
    tex_core_error *error
) {
    list->head = NULL;
    list->tail = NULL;
    list->count = 0;

    txc_scanner scanner;
    txc_scanner_init(&scanner, source, length);

    for (;;) {
        txc_token token;
        tex_core_status status = txc_scan(&scanner, &token, error);
        if (status != TEX_CORE_STATUS_OK) {
            return status;
        }

        switch (token.kind) {
        case TXC_TOKEN_END:
            return TEX_CORE_STATUS_OK;

        case TXC_TOKEN_PAR:
            return txc_fail(error, TEX_CORE_STATUS_UNSUPPORTED, &token.range, "unsupported paragraph break");

        case TXC_TOKEN_SPACE: {
            /* TeX ignores blanks in math mode. */
            if (mode != TEX_CORE_MODE_DOCUMENT) {
                break;
            }
            txc_item *item = txc_append(arena, list);
            if (item == NULL) {
                return txc_fail(error, TEX_CORE_STATUS_ALLOCATION_FAILED, NULL, "allocation failed");
            }
            item->kind = TXC_ITEM_SPACE;
            item->space = TXC_SPACE_WORD;
            item->range = token.range;
            break;
        }

        case TXC_TOKEN_CHARACTER: {
            if (txc_reserved(token.codepoint) || !txc_ordinary(token.codepoint, mode)) {
                return txc_fail(
                    error,
                    TEX_CORE_STATUS_UNSUPPORTED,
                    &token.range,
                    "unsupported character U+%04X",
                    (unsigned)token.codepoint
                );
            }
            txc_item *item = txc_append(arena, list);
            if (item == NULL) {
                return txc_fail(error, TEX_CORE_STATUS_ALLOCATION_FAILED, NULL, "allocation failed");
            }
            item->kind = TXC_ITEM_ATOM;
            item->codepoint = token.codepoint;
            item->style = txc_atom_style(token.codepoint, mode);
            item->range = token.range;
            break;
        }

        case TXC_TOKEN_CONTROL: {
            const txc_spacing_command *command = txc_spacing(token.name, token.name_length);
            if (command == NULL) {
                char label[64];
                txc_command_label(token.name, token.name_length, label, sizeof(label));
                return txc_fail(error, TEX_CORE_STATUS_UNSUPPORTED, &token.range, "unsupported command \\%s", label);
            }
            txc_item *item = txc_append(arena, list);
            if (item == NULL) {
                return txc_fail(error, TEX_CORE_STATUS_ALLOCATION_FAILED, NULL, "allocation failed");
            }
            item->kind = TXC_ITEM_SPACE;
            item->space = command->space;
            item->range = token.range;
            break;
        }
        }
    }
}
