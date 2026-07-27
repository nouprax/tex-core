/* Public facade: argument validation, pipeline orchestration, and the
 * borrowed read-only node views. */

#include "tex_core.h"

#include <string.h>

#include "dump.h"
#include "error.h"
#include "layout.h"
#include "memory.h"
#include "parse.h"
#include "scaled.h"
#include "tree.h"

void tex_core_options_init(tex_core_options *options) {
    if (options != NULL) {
        options->mode = TEX_CORE_MODE_DOCUMENT;
    }
}

static double txc_points(txc_scaled scaled) { return (double)scaled / 65536.0; }

tex_core_status tex_core_document_compile(
    const uint8_t *source,
    size_t length,
    const tex_core_options *options,
    tex_core_render_tree **tree,
    tex_core_error *error
) {
    if (tree == NULL) {
        return txc_fail(error, TEX_CORE_STATUS_INVALID_ARGUMENT, NULL, "tree output argument is NULL");
    }
    *tree = NULL;
    if (source == NULL && length != 0) {
        return txc_fail(error, TEX_CORE_STATUS_INVALID_ARGUMENT, NULL, "source is NULL with a non-zero length");
    }

    tex_core_options defaults;
    tex_core_options_init(&defaults);
    if (options == NULL) {
        options = &defaults;
    }
    if (options->mode != TEX_CORE_MODE_DOCUMENT && options->mode != TEX_CORE_MODE_MATH_INLINE &&
        options->mode != TEX_CORE_MODE_MATH_DISPLAY) {
        return txc_fail(error, TEX_CORE_STATUS_INVALID_ARGUMENT, NULL, "options.mode is not a tex_core_mode");
    }

    tex_core_render_tree *compiled = txc_malloc(sizeof(tex_core_render_tree));
    if (compiled == NULL) {
        return txc_fail(error, TEX_CORE_STATUS_ALLOCATION_FAILED, NULL, "allocation failed");
    }
    txc_arena_init(&compiled->arena);
    compiled->root = NULL;

    txc_list list;
    tex_core_status status = txc_parse(&compiled->arena, source, length, options->mode, &list, error);
    if (status == TEX_CORE_STATUS_OK) {
        status = txc_layout(&compiled->arena, &list, options->mode, length, &compiled->root, error);
    }
    if (status != TEX_CORE_STATUS_OK) {
        tex_core_render_tree_free(compiled);
        return status;
    }

    if (error != NULL) {
        memset(error, 0, sizeof(*error));
        error->status = TEX_CORE_STATUS_OK;
    }
    *tree = compiled;
    return TEX_CORE_STATUS_OK;
}

void tex_core_render_tree_free(tex_core_render_tree *tree) {
    if (tree != NULL) {
        txc_arena_release(&tree->arena);
        txc_free(tree);
    }
}

const tex_core_node *tex_core_render_tree_root(const tex_core_render_tree *tree) {
    return tree != NULL ? tree->root : NULL;
}

tex_core_status tex_core_render_tree_dump(const tex_core_render_tree *tree, char **dump, size_t *length) {
    if (dump == NULL) {
        return TEX_CORE_STATUS_INVALID_ARGUMENT;
    }
    *dump = NULL;
    if (length != NULL) {
        *length = 0;
    }
    if (tree == NULL || tree->root == NULL) {
        return TEX_CORE_STATUS_INVALID_ARGUMENT;
    }
    return txc_dump(tree->root, dump, length);
}

void tex_core_dump_free(char *dump) { txc_free(dump); }

tex_core_node_kind tex_core_node_get_kind(const tex_core_node *node) {
    return node != NULL ? node->kind : TEX_CORE_NODE_NONE;
}

const char *tex_core_node_kind_name(tex_core_node_kind kind) {
    switch (kind) {
    case TEX_CORE_NODE_NONE:
        return "none";
    case TEX_CORE_NODE_HBOX:
        return "hbox";
    case TEX_CORE_NODE_GLYPH:
        return "glyph";
    case TEX_CORE_NODE_KERN:
        return "kern";
    case TEX_CORE_NODE_RULE:
        return "rule";
    }
    return "none";
}

size_t tex_core_node_child_count(const tex_core_node *node) { return node != NULL ? node->child_count : 0; }

const tex_core_node *tex_core_node_child(const tex_core_node *node, size_t index) {
    if (node == NULL || index >= node->child_count) {
        return NULL;
    }
    return node->children[index];
}

tex_core_range tex_core_node_range(const tex_core_node *node) {
    tex_core_range range = {0, 0};
    return node != NULL ? node->range : range;
}

tex_core_frame tex_core_node_frame(const tex_core_node *node) {
    tex_core_frame frame = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    if (node != NULL) {
        frame.x = txc_points(node->x);
        frame.y = txc_points(node->y);
        frame.width = txc_points(node->width);
        frame.ascent = txc_points(node->ascent);
        frame.descent = txc_points(node->descent);
        frame.italic = txc_points(node->italic);
    }
    return frame;
}

tex_core_glyph tex_core_node_glyph(const tex_core_node *node) {
    tex_core_glyph glyph = {0, TEX_CORE_STYLE_UPRIGHT, TEX_CORE_FAMILY_MAIN, 0.0};
    if (node != NULL && node->kind == TEX_CORE_NODE_GLYPH) {
        glyph.codepoint = node->codepoint;
        glyph.style = node->style;
        glyph.size = txc_points(node->size);
    }
    return glyph;
}

const char *tex_core_version_string(void) { return TEX_CORE_VERSION_STRING; }
