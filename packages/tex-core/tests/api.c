/* Public API contract: argument validation, defaults, ownership, and the
 * NULL-tolerant node views. */

#include <stdio.h>
#include <string.h>

#include "harness.h"
#include "tex_core.h"

static void txc_test_version(txc_test *test) {
    txc_check_text(test, tex_core_version_string(), TEX_CORE_VERSION_STRING, "version string matches the macro");

    int major = 0;
    int minor = 0;
    int patch = 0;
    int fields = sscanf(TEX_CORE_VERSION_STRING, "%d.%d.%d", &major, &minor, &patch);
    txc_check_int(test, fields, 3, "version string is x.y.z");
    txc_check_int(test, TEX_CORE_VERSION, (major << 16) | (minor << 8) | patch, "packed version matches the string");
}

static void txc_test_arguments(txc_test *test) {
    tex_core_error error;
    tex_core_render_tree *tree = NULL;

    txc_check_int(
        test,
        tex_core_document_compile(NULL, 0, NULL, NULL, &error),
        TEX_CORE_STATUS_INVALID_ARGUMENT,
        "NULL tree output is rejected"
    );
    txc_check_int(test, error.status, TEX_CORE_STATUS_INVALID_ARGUMENT, "error mirrors the returned status");
    txc_check_int(test, error.has_range, 0, "argument errors carry no source range");

    txc_check_int(
        test,
        tex_core_document_compile(NULL, 3, NULL, &tree, &error),
        TEX_CORE_STATUS_INVALID_ARGUMENT,
        "NULL source with non-zero length is rejected"
    );
    txc_check(test, tree == NULL, "tree stays NULL on failure");

    tex_core_options options;
    tex_core_options_init(&options);
    txc_check_int(test, options.mode, TEX_CORE_MODE_DOCUMENT, "default mode is document");
    options.mode = (tex_core_mode)99;
    txc_check_int(
        test,
        tex_core_document_compile((const uint8_t *)"x", 1, &options, &tree, &error),
        TEX_CORE_STATUS_INVALID_ARGUMENT,
        "an out-of-range mode is rejected"
    );

    /* NULL error output is allowed everywhere. */
    txc_check_int(
        test,
        tex_core_document_compile(NULL, 0, NULL, NULL, NULL),
        TEX_CORE_STATUS_INVALID_ARGUMENT,
        "NULL error output is allowed"
    );

    tex_core_render_tree_free(NULL);
    tex_core_dump_free(NULL);
    txc_check(test, tex_core_render_tree_root(NULL) == NULL, "root of NULL tree is NULL");
}

static void txc_test_empty(txc_test *test) {
    tex_core_error error;
    memset(&error, 0xAA, sizeof(error));
    tex_core_render_tree *tree = NULL;
    txc_check_int(
        test,
        tex_core_document_compile(NULL, 0, NULL, &tree, &error),
        TEX_CORE_STATUS_OK,
        "empty input compiles"
    );
    txc_check_int(test, error.status, TEX_CORE_STATUS_OK, "error resets to OK on success");
    txc_check_int(test, error.has_range, 0, "error range resets on success");

    const tex_core_node *root = tex_core_render_tree_root(tree);
    txc_check_int(test, tex_core_node_get_kind(root), TEX_CORE_NODE_HBOX, "root is an hbox");
    txc_check_size(test, tex_core_node_child_count(root), 0, "empty input has no children");
    tex_core_frame frame = tex_core_node_frame(root);
    txc_check(test, frame.width == 0.0 && frame.ascent == 0.0 && frame.descent == 0.0, "empty hbox is zero-sized");
    tex_core_range range = tex_core_node_range(root);
    txc_check_size(test, range.begin, 0, "empty root range begin");
    txc_check_size(test, range.end, 0, "empty root range end");

    char *dump = NULL;
    size_t length = 0;
    txc_check_int(test, tex_core_render_tree_dump(tree, &dump, &length), TEX_CORE_STATUS_OK, "empty tree dumps");
    txc_check_text(
        test,
        dump,
        "render-tree 5\nhbox x=0.0pt y=0.0pt width=0.0pt ascent=0.0pt descent=0.0pt src=0..0\n",
        "empty dump"
    );
    txc_check_size(test, length, strlen(dump), "dump length excludes the NUL");
    tex_core_dump_free(dump);
    tex_core_render_tree_free(tree);
}

static void txc_test_views(txc_test *test) {
    tex_core_render_tree *tree = NULL;
    txc_check_int(
        test,
        tex_core_document_compile((const uint8_t *)"x y", 3, NULL, &tree, NULL),
        TEX_CORE_STATUS_OK,
        "document input compiles"
    );
    const tex_core_node *root = tex_core_render_tree_root(tree);
    txc_check_size(test, tex_core_node_child_count(root), 3, "glyph, kern, glyph");
    txc_check(test, tex_core_node_child(root, 3) == NULL, "out-of-range child is NULL");
    txc_check(test, tex_core_node_child(NULL, 0) == NULL, "child of NULL is NULL");

    const tex_core_node *glyph = tex_core_node_child(root, 0);
    const tex_core_node *kern = tex_core_node_child(root, 1);
    txc_check_int(test, tex_core_node_get_kind(glyph), TEX_CORE_NODE_GLYPH, "first child is a glyph");
    txc_check_int(test, tex_core_node_get_kind(kern), TEX_CORE_NODE_KERN, "second child is a kern");
    txc_check_int(test, tex_core_node_get_kind(NULL), TEX_CORE_NODE_NONE, "NULL node reads as NONE");

    tex_core_glyph view = tex_core_node_glyph(glyph);
    txc_check_int(test, (long long)view.codepoint, 'x', "glyph codepoint");
    txc_check_int(test, view.style, TEX_CORE_STYLE_UPRIGHT, "document glyphs are upright");
    txc_check_int(test, view.family, TEX_CORE_FAMILY_MAIN, "glyph family");
    txc_check(test, view.size == 10.0, "the skeleton em is 10pt");
    tex_core_glyph none = tex_core_node_glyph(kern);
    txc_check_int(test, (long long)none.codepoint, 0, "non-glyph nodes read as codepoint 0");

    txc_check_text(test, tex_core_node_kind_name(TEX_CORE_NODE_HBOX), "hbox", "kind name hbox");
    txc_check_text(test, tex_core_node_kind_name(TEX_CORE_NODE_GLYPH), "glyph", "kind name glyph");
    txc_check_text(test, tex_core_node_kind_name(TEX_CORE_NODE_KERN), "kern", "kind name kern");
    txc_check_text(test, tex_core_node_kind_name(TEX_CORE_NODE_RULE), "rule", "kind name rule");
    txc_check_text(test, tex_core_node_kind_name(TEX_CORE_NODE_NONE), "none", "kind name none");

    char *dump = NULL;
    txc_check_int(
        test,
        tex_core_render_tree_dump(NULL, &dump, NULL),
        TEX_CORE_STATUS_INVALID_ARGUMENT,
        "dump of NULL tree is rejected"
    );
    txc_check_int(
        test,
        tex_core_render_tree_dump(tree, NULL, NULL),
        TEX_CORE_STATUS_INVALID_ARGUMENT,
        "dump into NULL output is rejected"
    );
    tex_core_render_tree_free(tree);
}

int main(void) {
    txc_test test = {0, 0};
    txc_test_version(&test);
    txc_test_arguments(&test);
    txc_test_empty(&test);
    txc_test_views(&test);
    return txc_test_finish(&test, "api");
}
