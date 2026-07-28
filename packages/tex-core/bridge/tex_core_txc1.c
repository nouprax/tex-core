#include "tex_core_txc1.h"

#include <stdlib.h>
#include <string.h>

#include "tex_core.h"

#define TXC1_NODE_BYTES 92u
#define TXC1_HEADER_BYTES 8u

typedef struct txc1_writer {
    uint8_t *data;
    size_t position;
    size_t capacity;
} txc1_writer;

static void txc1_u32(txc1_writer *writer, uint32_t value) {
    for (unsigned shift = 0; shift < 4; shift++) {
        writer->data[writer->position++] = (uint8_t)(value >> (8 * shift));
    }
}

static void txc1_u64(txc1_writer *writer, uint64_t value) {
    for (unsigned shift = 0; shift < 8; shift++) {
        writer->data[writer->position++] = (uint8_t)(value >> (8 * shift));
    }
}

static void txc1_f64(txc1_writer *writer, double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    txc1_u64(writer, bits);
}

static size_t txc1_count(const tex_core_node *node) {
    size_t count = 1;
    size_t children = tex_core_node_child_count(node);
    for (size_t index = 0; index < children; index++) {
        count += txc1_count(tex_core_node_child(node, index));
    }
    return count;
}

static void txc1_node(txc1_writer *writer, const tex_core_node *node) {
    tex_core_node_kind kind = tex_core_node_get_kind(node);
    tex_core_frame frame = tex_core_node_frame(node);
    tex_core_glyph glyph = tex_core_node_glyph(node);
    tex_core_range range = tex_core_node_range(node);
    size_t children = tex_core_node_child_count(node);

    txc1_u32(writer, (uint32_t)kind);
    txc1_f64(writer, frame.x);
    txc1_f64(writer, frame.y);
    txc1_f64(writer, frame.width);
    txc1_f64(writer, frame.ascent);
    txc1_f64(writer, frame.descent);
    txc1_f64(writer, frame.italic);
    txc1_u32(writer, glyph.codepoint);
    txc1_u32(writer, (uint32_t)glyph.style);
    txc1_u32(writer, (uint32_t)glyph.family);
    txc1_f64(writer, glyph.size);
    txc1_u64(writer, range.begin);
    txc1_u64(writer, range.end);
    txc1_u32(writer, (uint32_t)children);
    for (size_t index = 0; index < children; index++) {
        txc1_node(writer, tex_core_node_child(node, index));
    }
}

bool tex_core_txc1_compile(
    const uint8_t *source,
    size_t length,
    uint32_t mode,
    uint8_t **output,
    size_t *output_length
) {
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

    /* The payload size is exact, so the buffer is allocated once: no
     * growth loop, no unused tail, no extra traversal beyond the count
     * the header itself requires. */
    txc1_writer writer = {NULL, 0, 0};
    if (status != TEX_CORE_STATUS_OK) {
        size_t message_length = strlen(error.message);
        writer.capacity = 4u + 4u + 8u + 8u + 4u + message_length;
        writer.data = malloc(writer.capacity);
        if (writer.data == NULL) {
            return false;
        }
        txc1_u32(&writer, (uint32_t)status);
        txc1_u32(&writer, error.has_range ? 1 : 0);
        txc1_u64(&writer, error.has_range ? error.range.begin : 0);
        txc1_u64(&writer, error.has_range ? error.range.end : 0);
        txc1_u32(&writer, (uint32_t)message_length);
        memcpy(writer.data + writer.position, error.message, message_length);
        writer.position += message_length;
    } else {
        const tex_core_node *root = tex_core_render_tree_root(tree);
        size_t count = txc1_count(root);
        if (count > (SIZE_MAX - TXC1_HEADER_BYTES) / TXC1_NODE_BYTES || count > UINT32_MAX) {
            tex_core_render_tree_free(tree);
            return false;
        }
        writer.capacity = TXC1_HEADER_BYTES + count * TXC1_NODE_BYTES;
        writer.data = malloc(writer.capacity);
        if (writer.data == NULL) {
            tex_core_render_tree_free(tree);
            return false;
        }
        txc1_u32(&writer, 0);
        txc1_u32(&writer, (uint32_t)count);
        txc1_node(&writer, root);
        tex_core_render_tree_free(tree);
        if (writer.position != writer.capacity) {
            /* The walk and the count disagree — a protocol bug, never a
             * caller condition. Fail closed instead of shipping a short
             * payload. */
            free(writer.data);
            return false;
        }
    }

    *output = writer.data;
    *output_length = writer.position;
    return true;
}

void tex_core_txc1_free(uint8_t *output) { free(output); }
