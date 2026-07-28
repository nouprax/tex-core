#include "dump.h"

#include <stdint.h>
#include <string.h>

#include "memory.h"
#include "scaled.h"

typedef struct txc_buffer {
    char *data;
    size_t length;
    size_t capacity;
} txc_buffer;

static bool txc_buffer_reserve(txc_buffer *buffer, size_t extra) {
    if (buffer->capacity - buffer->length > extra) {
        return true;
    }
    size_t capacity = buffer->capacity > 0 ? buffer->capacity : 256;
    while (capacity - buffer->length <= extra) {
        capacity *= 2;
    }
    char *grown = txc_realloc(buffer->data, capacity);
    if (grown == NULL) {
        return false;
    }
    buffer->data = grown;
    buffer->capacity = capacity;
    return true;
}

static bool txc_buffer_append(txc_buffer *buffer, const char *text, size_t length) {
    if (!txc_buffer_reserve(buffer, length)) {
        return false;
    }
    memcpy(buffer->data + buffer->length, text, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
    return true;
}

/* One dump line is assembled in place and appended to the buffer once.
 * The capacity covers the widest line body by a comfortable margin (a
 * glyph line with two full-width source offsets stays under 300 bytes);
 * the per-depth indentation goes straight to the buffer instead, since
 * nesting may reach 255 levels. */
#define TXC_LINE_CAPACITY 512

typedef struct txc_line {
    char data[TXC_LINE_CAPACITY];
    size_t length;
} txc_line;

static void txc_line_append(txc_line *line, const char *text, size_t length) {
    if (TXC_LINE_CAPACITY - line->length <= length) {
        return;
    }
    memcpy(line->data + line->length, text, length);
    line->length += length;
}

#define TXC_LINE_LITERAL(line, text) txc_line_append(line, text, sizeof(text) - 1)

/* Appends ` <label>=<scaled>pt`. */
static void txc_line_measure(txc_line *line, const char *label, size_t label_length, txc_scaled scaled) {
    txc_line_append(line, " ", 1);
    txc_line_append(line, label, label_length);
    txc_line_append(line, "=", 1);
    if (TXC_LINE_CAPACITY - line->length > TXC_SCALED_FORMAT_CAPACITY) {
        line->length += txc_scaled_format(scaled, line->data + line->length);
    }
    txc_line_append(line, "pt", 2);
}

#define TXC_LINE_MEASURE(line, label, scaled) txc_line_measure(line, label, sizeof(label) - 1, scaled)

static void txc_line_decimal(txc_line *line, size_t value) {
    char digits[20];
    size_t count = 0;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);
    if (TXC_LINE_CAPACITY - line->length <= count) {
        return;
    }
    while (count > 0) {
        line->data[line->length++] = digits[--count];
    }
}

/* Uppercase hexadecimal, at least four digits, no padding beyond four. */
static void txc_line_codepoint(txc_line *line, uint32_t value) {
    char digits[8];
    size_t count = 0;
    do {
        digits[count++] = "0123456789ABCDEF"[value % 16];
        value /= 16;
    } while (value != 0);
    while (count < 4) {
        digits[count++] = '0';
    }
    if (TXC_LINE_CAPACITY - line->length <= count) {
        return;
    }
    while (count > 0) {
        line->data[line->length++] = digits[--count];
    }
}

static void txc_line_range(txc_line *line, tex_core_range range) {
    txc_line_append(line, " src=", 5);
    txc_line_decimal(line, range.begin);
    txc_line_append(line, "..", 2);
    txc_line_decimal(line, range.end);
}

static void txc_line_family(txc_line *line, tex_core_family family) {
    switch (family) {
    case TEX_CORE_FAMILY_SIZE1:
        TXC_LINE_LITERAL(line, " family=size1");
        return;
    case TEX_CORE_FAMILY_SIZE2:
        TXC_LINE_LITERAL(line, " family=size2");
        return;
    case TEX_CORE_FAMILY_SIZE3:
        TXC_LINE_LITERAL(line, " family=size3");
        return;
    case TEX_CORE_FAMILY_SIZE4:
        TXC_LINE_LITERAL(line, " family=size4");
        return;
    case TEX_CORE_FAMILY_BOLD:
        TXC_LINE_LITERAL(line, " family=bold");
        return;
    case TEX_CORE_FAMILY_TEXTIT:
        TXC_LINE_LITERAL(line, " family=textit");
        return;
    case TEX_CORE_FAMILY_CAL:
        TXC_LINE_LITERAL(line, " family=cal");
        return;
    case TEX_CORE_FAMILY_BB:
        TXC_LINE_LITERAL(line, " family=bb");
        return;
    case TEX_CORE_FAMILY_SANS:
        TXC_LINE_LITERAL(line, " family=sans");
        return;
    case TEX_CORE_FAMILY_MONO:
        TXC_LINE_LITERAL(line, " family=mono");
        return;
    case TEX_CORE_FAMILY_MAIN:
        break;
    }
    TXC_LINE_LITERAL(line, " family=main");
}

/* Appends 2*depth spaces of indentation directly to the buffer. */
static bool txc_buffer_indent(txc_buffer *buffer, unsigned depth) {
    size_t spaces = 2u * depth;
    if (!txc_buffer_reserve(buffer, spaces)) {
        return false;
    }
    memset(buffer->data + buffer->length, ' ', spaces);
    buffer->length += spaces;
    buffer->data[buffer->length] = '\0';
    return true;
}

static bool txc_dump_node(txc_buffer *buffer, const txc_node *node, unsigned depth) {
    if (!txc_buffer_indent(buffer, depth)) {
        return false;
    }

    txc_line line = {{0}, 0};
    switch (node->kind) {
    case TEX_CORE_NODE_HBOX:
        TXC_LINE_LITERAL(&line, "hbox");
        TXC_LINE_MEASURE(&line, "x", node->x);
        TXC_LINE_MEASURE(&line, "y", node->y);
        TXC_LINE_MEASURE(&line, "width", node->width);
        TXC_LINE_MEASURE(&line, "ascent", node->ascent);
        TXC_LINE_MEASURE(&line, "descent", node->descent);
        txc_line_range(&line, node->range);
        TXC_LINE_LITERAL(&line, "\n");
        if (!txc_buffer_append(buffer, line.data, line.length)) {
            return false;
        }
        for (size_t index = 0; index < node->child_count; index++) {
            if (!txc_dump_node(buffer, node->children[index], depth + 1)) {
                return false;
            }
        }
        return true;

    case TEX_CORE_NODE_GLYPH:
        TXC_LINE_LITERAL(&line, "glyph");
        TXC_LINE_MEASURE(&line, "x", node->x);
        TXC_LINE_MEASURE(&line, "y", node->y);
        txc_line_append(&line, " cp=U+", 6);
        txc_line_codepoint(&line, node->codepoint);
        if (node->style == TEX_CORE_STYLE_ITALIC) {
            TXC_LINE_LITERAL(&line, " style=italic");
        } else {
            TXC_LINE_LITERAL(&line, " style=upright");
        }
        txc_line_family(&line, node->family);
        TXC_LINE_MEASURE(&line, "size", node->size);
        TXC_LINE_MEASURE(&line, "width", node->width);
        TXC_LINE_MEASURE(&line, "ascent", node->ascent);
        TXC_LINE_MEASURE(&line, "descent", node->descent);
        TXC_LINE_MEASURE(&line, "italic", node->italic);
        txc_line_range(&line, node->range);
        TXC_LINE_LITERAL(&line, "\n");
        return txc_buffer_append(buffer, line.data, line.length);

    case TEX_CORE_NODE_KERN:
        TXC_LINE_LITERAL(&line, "kern");
        TXC_LINE_MEASURE(&line, "x", node->x);
        TXC_LINE_MEASURE(&line, "width", node->width);
        txc_line_range(&line, node->range);
        TXC_LINE_LITERAL(&line, "\n");
        return txc_buffer_append(buffer, line.data, line.length);

    case TEX_CORE_NODE_RULE:
        TXC_LINE_LITERAL(&line, "rule");
        TXC_LINE_MEASURE(&line, "x", node->x);
        TXC_LINE_MEASURE(&line, "y", node->y);
        TXC_LINE_MEASURE(&line, "width", node->width);
        TXC_LINE_MEASURE(&line, "ascent", node->ascent);
        TXC_LINE_MEASURE(&line, "descent", node->descent);
        txc_line_range(&line, node->range);
        TXC_LINE_LITERAL(&line, "\n");
        return txc_buffer_append(buffer, line.data, line.length);

    case TEX_CORE_NODE_NONE:
        break;
    }
    return false;
}

tex_core_status txc_dump(const txc_node *root, char **dump, size_t *length) {
    txc_buffer buffer = {NULL, 0, 0};
    if (!txc_buffer_append(&buffer, "render-tree 5\n", 14) || !txc_dump_node(&buffer, root, 0)) {
        txc_free(buffer.data);
        return TEX_CORE_STATUS_ALLOCATION_FAILED;
    }
    *dump = buffer.data;
    if (length != NULL) {
        *length = buffer.length;
    }
    return TEX_CORE_STATUS_OK;
}
