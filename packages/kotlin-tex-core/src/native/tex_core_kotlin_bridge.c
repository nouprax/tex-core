/* TXC1 payload writer over the public C facade only. Layout (all integers
 * and doubles little-endian):
 *
 *   u32 status                          tex_core_status
 *   status != OK:
 *     u32 has_range; u64 begin; u64 end
 *     u32 message_length; message bytes (ASCII, no NUL)
 *   status == OK:
 *     u32 node_count                    preorder walk
 *     per node:
 *       u32 kind                        1 hbox, 2 glyph, 3 kern
 *       f64 x, y, width, ascent, descent, italic
 *       u32 codepoint; u32 style        zero outside glyphs
 *       f64 size                        zero outside glyphs
 *       u64 src_begin; u64 src_end
 *       u32 child_count
 *
 * The record is fixed-shape so the Kotlin decoder stays a single loop; the
 * tree is small and payloads are short-lived. */

#include "tex_core_kotlin_bridge.h"

#include <stdlib.h>
#include <string.h>

#include "tex_core.h"

typedef struct txc_kotlin_buffer {
    uint8_t *data;
    size_t length;
    size_t capacity;
} txc_kotlin_buffer;

static bool txc_kotlin_reserve(txc_kotlin_buffer *buffer, size_t extra) {
    if (buffer->capacity - buffer->length >= extra) {
        return true;
    }
    size_t capacity = buffer->capacity > 0 ? buffer->capacity : 256;
    while (capacity - buffer->length < extra) {
        capacity *= 2;
    }
    uint8_t *grown = realloc(buffer->data, capacity);
    if (grown == NULL) {
        return false;
    }
    buffer->data = grown;
    buffer->capacity = capacity;
    return true;
}

static bool txc_kotlin_u32(txc_kotlin_buffer *buffer, uint32_t value) {
    if (!txc_kotlin_reserve(buffer, 4)) {
        return false;
    }
    for (unsigned shift = 0; shift < 4; shift++) {
        buffer->data[buffer->length++] = (uint8_t)(value >> (8 * shift));
    }
    return true;
}

static bool txc_kotlin_u64(txc_kotlin_buffer *buffer, uint64_t value) {
    if (!txc_kotlin_reserve(buffer, 8)) {
        return false;
    }
    for (unsigned shift = 0; shift < 8; shift++) {
        buffer->data[buffer->length++] = (uint8_t)(value >> (8 * shift));
    }
    return true;
}

static bool txc_kotlin_f64(txc_kotlin_buffer *buffer, double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return txc_kotlin_u64(buffer, bits);
}

static size_t txc_kotlin_count(const tex_core_node *node) {
    size_t count = 1;
    size_t children = tex_core_node_child_count(node);
    for (size_t index = 0; index < children; index++) {
        count += txc_kotlin_count(tex_core_node_child(node, index));
    }
    return count;
}

static bool txc_kotlin_node(txc_kotlin_buffer *buffer, const tex_core_node *node) {
    tex_core_node_kind kind = tex_core_node_get_kind(node);
    tex_core_frame frame = tex_core_node_frame(node);
    tex_core_glyph glyph = tex_core_node_glyph(node);
    tex_core_range range = tex_core_node_range(node);
    size_t children = tex_core_node_child_count(node);

    if (!txc_kotlin_u32(buffer, (uint32_t)kind) || !txc_kotlin_f64(buffer, frame.x) ||
        !txc_kotlin_f64(buffer, frame.y) || !txc_kotlin_f64(buffer, frame.width) ||
        !txc_kotlin_f64(buffer, frame.ascent) || !txc_kotlin_f64(buffer, frame.descent) ||
        !txc_kotlin_f64(buffer, frame.italic) || !txc_kotlin_u32(buffer, glyph.codepoint) ||
        !txc_kotlin_u32(buffer, (uint32_t)glyph.style) || !txc_kotlin_f64(buffer, glyph.size) ||
        !txc_kotlin_u64(buffer, range.begin) || !txc_kotlin_u64(buffer, range.end) ||
        !txc_kotlin_u32(buffer, (uint32_t)children)) {
        return false;
    }
    for (size_t index = 0; index < children; index++) {
        if (!txc_kotlin_node(buffer, tex_core_node_child(node, index))) {
            return false;
        }
    }
    return true;
}

bool tex_core_kotlin_compile(const uint8_t *source, size_t length, uint32_t mode, uint8_t **output,
                             size_t *output_length) {
    tex_core_options options;
    tex_core_options_init(&options);
    switch (mode) {
    case 1:
        options.mode = TEX_CORE_MODE_MATH_INLINE;
        break;
    case 2:
        options.mode = TEX_CORE_MODE_MATH_DISPLAY;
        break;
    default:
        options.mode = TEX_CORE_MODE_DOCUMENT;
        break;
    }

    tex_core_render_tree *tree = NULL;
    tex_core_error error;
    tex_core_status status = tex_core_document_compile(source, length, &options, &tree, &error);

    txc_kotlin_buffer buffer = {NULL, 0, 0};
    bool ok;
    if (status != TEX_CORE_STATUS_OK) {
        size_t message_length = strlen(error.message);
        ok = txc_kotlin_u32(&buffer, (uint32_t)status) && txc_kotlin_u32(&buffer, error.has_range ? 1 : 0) &&
             txc_kotlin_u64(&buffer, error.has_range ? error.range.begin : 0) &&
             txc_kotlin_u64(&buffer, error.has_range ? error.range.end : 0) &&
             txc_kotlin_u32(&buffer, (uint32_t)message_length) && txc_kotlin_reserve(&buffer, message_length);
        if (ok) {
            memcpy(buffer.data + buffer.length, error.message, message_length);
            buffer.length += message_length;
        }
    } else {
        const tex_core_node *root = tex_core_render_tree_root(tree);
        ok = txc_kotlin_u32(&buffer, 0) && txc_kotlin_u32(&buffer, (uint32_t)txc_kotlin_count(root)) &&
             txc_kotlin_node(&buffer, root);
    }
    tex_core_render_tree_free(tree);

    if (!ok) {
        free(buffer.data);
        return false;
    }
    *output = buffer.data;
    *output_length = buffer.length;
    return true;
}

void tex_core_kotlin_free(uint8_t *output) { free(output); }
