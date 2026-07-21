#include "dump.h"

#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "scaled.h"

typedef struct txc_buffer {
    char *data;
    size_t length;
    size_t capacity;
} txc_buffer;

static bool txc_buffer_append(txc_buffer *buffer, const char *text, size_t length) {
    if (buffer->capacity - buffer->length <= length) {
        size_t capacity = buffer->capacity > 0 ? buffer->capacity : 256;
        while (capacity - buffer->length <= length) {
            capacity *= 2;
        }
        char *grown = txc_realloc(buffer->data, capacity);
        if (grown == NULL) {
            return false;
        }
        buffer->data = grown;
        buffer->capacity = capacity;
    }
    memcpy(buffer->data + buffer->length, text, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
    return true;
}

static bool txc_buffer_text(txc_buffer *buffer, const char *text) {
    return txc_buffer_append(buffer, text, strlen(text));
}

/* Appends ` <label>=<scaled>pt`. */
static bool txc_buffer_measure(txc_buffer *buffer, const char *label, txc_scaled scaled) {
    char formatted[TXC_SCALED_FORMAT_CAPACITY];
    txc_scaled_format(scaled, formatted);
    return txc_buffer_text(buffer, " ") && txc_buffer_text(buffer, label) && txc_buffer_text(buffer, "=") &&
           txc_buffer_text(buffer, formatted) && txc_buffer_text(buffer, "pt");
}

static bool txc_buffer_range(txc_buffer *buffer, tex_core_range range) {
    char formatted[64];
    snprintf(formatted, sizeof(formatted), " src=%zu..%zu", range.begin, range.end);
    return txc_buffer_text(buffer, formatted);
}

static bool txc_dump_node(txc_buffer *buffer, const txc_node *node, unsigned depth) {
    for (unsigned level = 0; level < depth; level++) {
        if (!txc_buffer_text(buffer, "  ")) {
            return false;
        }
    }

    switch (node->kind) {
    case TEX_CORE_NODE_HBOX:
        if (!txc_buffer_text(buffer, "hbox") || !txc_buffer_measure(buffer, "width", node->width) ||
            !txc_buffer_measure(buffer, "ascent", node->ascent) ||
            !txc_buffer_measure(buffer, "descent", node->descent) || !txc_buffer_range(buffer, node->range) ||
            !txc_buffer_text(buffer, "\n")) {
            return false;
        }
        for (size_t index = 0; index < node->child_count; index++) {
            if (!txc_dump_node(buffer, node->children[index], depth + 1)) {
                return false;
            }
        }
        return true;

    case TEX_CORE_NODE_GLYPH: {
        char codepoint[16];
        snprintf(codepoint, sizeof(codepoint), " cp=U+%04X", (unsigned)node->codepoint);
        return txc_buffer_text(buffer, "glyph") && txc_buffer_measure(buffer, "x", node->x) &&
               txc_buffer_measure(buffer, "y", node->y) && txc_buffer_text(buffer, codepoint) &&
               txc_buffer_text(buffer, node->style == TEX_CORE_STYLE_ITALIC ? " style=italic" : " style=upright") &&
               txc_buffer_text(buffer, " family=main") && txc_buffer_measure(buffer, "size", TXC_EM_SP) &&
               txc_buffer_measure(buffer, "width", node->width) && txc_buffer_measure(buffer, "ascent", node->ascent) &&
               txc_buffer_measure(buffer, "descent", node->descent) &&
               txc_buffer_measure(buffer, "italic", node->italic) && txc_buffer_range(buffer, node->range) &&
               txc_buffer_text(buffer, "\n");
    }

    case TEX_CORE_NODE_KERN:
        return txc_buffer_text(buffer, "kern") && txc_buffer_measure(buffer, "x", node->x) &&
               txc_buffer_measure(buffer, "width", node->width) && txc_buffer_range(buffer, node->range) &&
               txc_buffer_text(buffer, "\n");

    case TEX_CORE_NODE_NONE:
        break;
    }
    return false;
}

tex_core_status txc_dump(const txc_node *root, char **dump, size_t *length) {
    txc_buffer buffer = {NULL, 0, 0};
    if (!txc_buffer_text(&buffer, "render-tree 0\n") || !txc_dump_node(&buffer, root, 0)) {
        txc_free(buffer.data);
        return TEX_CORE_STATUS_ALLOCATION_FAILED;
    }
    *dump = buffer.data;
    if (length != NULL) {
        *length = buffer.length;
    }
    return TEX_CORE_STATUS_OK;
}
