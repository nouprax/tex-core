/* Conformance runner (CTest label `conformance`). Phase 3 lands the
 * canonical render-tree spec under specs/render-tree/ and points this
 * runner at its manifest of fixture pairs; until then it runs an embedded
 * placeholder suite over the same byte-exact golden-dump discipline. */

#include <string.h>

#include "harness.h"
#include "tex_core.h"

typedef struct txc_fixture {
    const char *name;
    tex_core_mode mode;
    const char *source;
    const char *expected;
} txc_fixture;

static const txc_fixture TXC_FIXTURES[] = {
    {"empty-document",
     TEX_CORE_MODE_DOCUMENT,
     "",
     "render-tree 0\n"
     "hbox width=0.0pt ascent=0.0pt descent=0.0pt src=0..0\n"},

    {"math-inline-letter",
     TEX_CORE_MODE_MATH_INLINE,
     "x",
     "render-tree 0\n"
     "hbox width=5.71533pt ascent=4.30557pt descent=0.0pt src=0..1\n"
     "  glyph x=0.0pt y=0.0pt cp=U+0078 style=italic family=main size=10.0pt width=5.71533pt ascent=4.30557pt "
     "descent=0.0pt italic=0.0pt src=0..1\n"},

    {"document-words",
     TEX_CORE_MODE_DOCUMENT,
     "a b",
     "render-tree 0\n"
     "hbox width=13.88885pt ascent=6.94443pt descent=0.0pt src=0..3\n"
     "  glyph x=0.0pt y=0.0pt cp=U+0061 style=upright family=main size=10.0pt width=5.0pt ascent=4.30557pt "
     "descent=0.0pt italic=0.0pt src=0..1\n"
     "  kern x=5.0pt width=3.33328pt src=1..2\n"
     "  glyph x=8.33328pt y=0.0pt cp=U+0062 style=upright family=main size=10.0pt width=5.55557pt ascent=6.94443pt "
     "descent=0.0pt italic=0.0pt src=2..3\n"},

    {"math-display-quad",
     TEX_CORE_MODE_MATH_DISPLAY,
     "\\quad",
     "render-tree 0\n"
     "hbox width=10.0pt ascent=0.0pt descent=0.0pt src=0..5\n"
     "  kern x=0.0pt width=10.0pt src=0..5\n"},
};

int main(void) {
    txc_test test = {0, 0};
    for (size_t index = 0; index < sizeof(TXC_FIXTURES) / sizeof(TXC_FIXTURES[0]); index++) {
        const txc_fixture *fixture = &TXC_FIXTURES[index];
        tex_core_options options;
        tex_core_options_init(&options);
        options.mode = fixture->mode;
        tex_core_render_tree *tree = NULL;
        tex_core_error error;
        tex_core_status status = tex_core_document_compile(
            (const uint8_t *)fixture->source,
            strlen(fixture->source),
            &options,
            &tree,
            &error
        );
        txc_check(&test, status == TEX_CORE_STATUS_OK, "%s: compile failed: %s", fixture->name, error.message);
        if (status != TEX_CORE_STATUS_OK) {
            continue;
        }
        char *dump = NULL;
        txc_check(
            &test,
            tex_core_render_tree_dump(tree, &dump, NULL) == TEX_CORE_STATUS_OK,
            "%s: dump failed",
            fixture->name
        );
        txc_check_text(&test, dump, fixture->expected, fixture->name);
        tex_core_dump_free(dump);
        tex_core_render_tree_free(tree);
    }
    return txc_test_finish(&test, "conformance");
}
