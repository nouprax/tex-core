/* libFuzzer harness (explicit non-default target, plan section 6.1): the
 * first input byte selects the mode, the rest is the source. Every outcome
 * must be a clean success or a structured error — no crashes, no leaks, no
 * sanitizer findings. Build and run via `make libFuzzer`. */

#include <stddef.h>
#include <stdint.h>

#include "tex_core.h"

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
    if (tex_core_document_compile(source, length, &options, &tree, &error) == TEX_CORE_STATUS_OK) {
        char *dump = NULL;
        if (tex_core_render_tree_dump(tree, &dump, NULL) == TEX_CORE_STATUS_OK) {
            tex_core_dump_free(dump);
        }
        tex_core_render_tree_free(tree);
    }
    return 0;
}
