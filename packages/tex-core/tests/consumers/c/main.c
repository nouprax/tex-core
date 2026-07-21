#include <tex_core.h>

#include <string.h>

int main(void) {
    const char *source = "ab";
    tex_core_options options;
    tex_core_error error;
    tex_core_render_tree *tree = NULL;
    const tex_core_node *root;
    char *dump = NULL;
    size_t length = 0;

    tex_core_options_init(&options);
    options.mode = TEX_CORE_MODE_MATH_INLINE;
    if (tex_core_document_compile((const uint8_t *)source, strlen(source), &options, &tree, &error) !=
            TEX_CORE_STATUS_OK ||
        tree == NULL) {
        return 1;
    }
    root = tex_core_render_tree_root(tree);
    if (root == NULL || tex_core_node_get_kind(root) != TEX_CORE_NODE_HBOX || tex_core_node_child_count(root) == 0) {
        tex_core_render_tree_free(tree);
        return 2;
    }
    if (tex_core_render_tree_dump(tree, &dump, &length) != TEX_CORE_STATUS_OK || length == 0) {
        tex_core_render_tree_free(tree);
        return 3;
    }
    tex_core_dump_free(dump);
    tex_core_render_tree_free(tree);
    return 0;
}
