/* libFuzzer harness (explicit non-default target, plan section 6.1): the
 * first input byte selects the mode, the rest is the source. Every outcome
 * must be a clean success or a structured error — no crashes, no leaks, no
 * sanitizer findings — and both outcomes must satisfy the public
 * postconditions checked here. Build and run via `make libFuzzer`; the
 * corpus is seeded from the shared conformance fixtures by
 * scripts/build-fuzz-corpus.sh and guided by tex.dict. */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "tex_core.h"

static void txc_fuzz_require(bool condition, const char *label) {
    if (!condition) {
        /* abort() so libFuzzer records the failing input as a crash. */
        (void)label;
        abort();
    }
}

/* Walks the tree checking the schema invariants every consumer relies on:
 * known kinds, leaf-only child rules, and source ranges inside the input. */
static void txc_fuzz_check_node(const tex_core_node *node, size_t length) {
    tex_core_node_kind kind = tex_core_node_get_kind(node);
    tex_core_range range = tex_core_node_range(node);
    size_t children = tex_core_node_child_count(node);
    txc_fuzz_require(
        kind == TEX_CORE_NODE_HBOX || kind == TEX_CORE_NODE_GLYPH || kind == TEX_CORE_NODE_KERN ||
            kind == TEX_CORE_NODE_RULE,
        "node kind"
    );
    txc_fuzz_require(range.begin <= range.end && range.end <= length, "node range");
    if (kind == TEX_CORE_NODE_HBOX) {
        for (size_t index = 0; index < children; index++) {
            const tex_core_node *child = tex_core_node_child(node, index);
            txc_fuzz_require(child != NULL, "hbox child");
            txc_fuzz_check_node(child, length);
        }
    } else {
        txc_fuzz_require(children == 0, "leaf arity");
        txc_fuzz_require(tex_core_node_child(node, 0) == NULL, "leaf child access");
    }
    if (kind == TEX_CORE_NODE_GLYPH) {
        tex_core_glyph glyph = tex_core_node_glyph(node);
        txc_fuzz_require(glyph.codepoint <= 0x10FFFF, "glyph scalar");
        txc_fuzz_require(glyph.size > 0.0, "glyph size");
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    tex_core_options options;
    tex_core_options_init(&options);
    const uint8_t *source = data;
    size_t length = size;
    if (size > 0) {
        options.mode = (tex_core_mode)(data[0] % 3);
        source = data + 1;
        length = size - 1;
    }

    tex_core_render_tree *tree = NULL;
    tex_core_error error;
    memset(&error, 0xAB, sizeof(error));
    tex_core_status status = tex_core_document_compile(source, length, &options, &tree, &error);
    if (status == TEX_CORE_STATUS_OK) {
        txc_fuzz_require(tree != NULL, "success yields a tree");
        txc_fuzz_require(error.status == TEX_CORE_STATUS_OK, "success resets the error");
        const tex_core_node *root = tex_core_render_tree_root(tree);
        txc_fuzz_require(root != NULL, "success yields a root");
        txc_fuzz_require(tex_core_node_get_kind(root) == TEX_CORE_NODE_HBOX, "root is an hbox");
        txc_fuzz_check_node(root, length);
        char *dump = NULL;
        size_t dump_length = 0;
        txc_fuzz_require(tex_core_render_tree_dump(tree, &dump, &dump_length) == TEX_CORE_STATUS_OK, "dump succeeds");
        txc_fuzz_require(dump != NULL && dump_length > 0 && dump[dump_length] == '\0', "dump is NUL-terminated");
        txc_fuzz_require(strncmp(dump, "render-tree 5\n", 14) == 0, "dump header");
        txc_fuzz_require(dump[dump_length - 1] == '\n', "dump trailing newline");
        tex_core_dump_free(dump);
        tex_core_render_tree_free(tree);
    } else {
        txc_fuzz_require(tree == NULL, "failure yields no tree");
        txc_fuzz_require(error.status == status, "error status matches the return");
        txc_fuzz_require(memchr(error.message, '\0', sizeof(error.message)) != NULL, "error message terminated");
        txc_fuzz_require(error.message[0] != '\0', "error message non-empty");
        if (error.has_range) {
            txc_fuzz_require(error.range.begin <= error.range.end && error.range.end <= length, "error range");
        }
    }
    return 0;
}
