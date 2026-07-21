/* Engine behavior: metrics-backed geometry, mode-dependent styles, explicit
 * spacing, and tokenizer whitespace rules — checked through the public node
 * views against independently derived scaled-point values. */

#include <stdint.h>
#include <string.h>

#include "harness.h"
#include "tex_core.h"

/* Expected geometry is derived here exactly as the generator derives the
 * embedded tables (scripts/generate-metrics.mjs): round the vendored KaTeX
 * em value to 16.16 fixed point, then scale by the 10pt em. Keeping the em
 * values inline makes these checks independent of metrics.inc. */
static double txc_expected_points(double em_value) {
    long long fraction = (long long)(em_value * 65536.0 + 0.5);
    return (double)(fraction * 10) / 65536.0;
}

static tex_core_render_tree *txc_compile(txc_test *test, const char *source, tex_core_mode mode, const char *label) {
    tex_core_options options;
    tex_core_options_init(&options);
    options.mode = mode;
    tex_core_render_tree *tree = NULL;
    tex_core_error error;
    tex_core_status status =
        tex_core_document_compile((const uint8_t *)source, strlen(source), &options, &tree, &error);
    txc_check(test, status == TEX_CORE_STATUS_OK, "%s: compile failed: %s", label, error.message);
    return tree;
}

static char *txc_dump_text(txc_test *test, const char *source, tex_core_mode mode, const char *label) {
    tex_core_render_tree *tree = txc_compile(test, source, mode, label);
    if (tree == NULL) {
        return NULL;
    }
    char *dump = NULL;
    txc_check(test, tex_core_render_tree_dump(tree, &dump, NULL) == TEX_CORE_STATUS_OK, "%s: dump failed", label);
    tex_core_render_tree_free(tree);
    return dump;
}

static void
txc_check_dumps_equal(txc_test *test, const char *left, const char *right, tex_core_mode mode, const char *label) {
    char *left_dump = txc_dump_text(test, left, mode, label);
    char *right_dump = txc_dump_text(test, right, mode, label);
    txc_check(
        test,
        left_dump != NULL && right_dump != NULL && strcmp(left_dump, right_dump) == 0,
        "%s: dumps differ",
        label
    );
    tex_core_dump_free(left_dump);
    tex_core_dump_free(right_dump);
}

static void txc_test_math_glyph(txc_test *test) {
    tex_core_render_tree *tree = txc_compile(test, "x", TEX_CORE_MODE_MATH_INLINE, "math x");
    const tex_core_node *root = tex_core_render_tree_root(tree);
    txc_check_size(test, tex_core_node_child_count(root), 1, "math x has one child");

    const tex_core_node *glyph = tex_core_node_child(root, 0);
    tex_core_glyph view = tex_core_node_glyph(glyph);
    txc_check_int(test, (long long)view.codepoint, 'x', "math x codepoint");
    txc_check_int(test, view.style, TEX_CORE_STYLE_ITALIC, "math letters take the italic face");

    /* KaTeX Math-Italic x: width 0.57153, height 0.43056, depth 0. */
    tex_core_frame frame = tex_core_node_frame(glyph);
    txc_check(test, frame.width == txc_expected_points(0.57153), "math x width");
    txc_check(test, frame.ascent == txc_expected_points(0.43056), "math x ascent");
    txc_check(test, frame.descent == 0.0, "math x descent");
    txc_check(test, frame.italic == 0.0, "math x italic correction");
    txc_check(test, frame.x == 0.0 && frame.y == 0.0, "math x sits at the origin");

    tex_core_frame box = tex_core_node_frame(root);
    txc_check(test, box.width == frame.width, "hbox width equals the glyph advance");
    txc_check(test, box.ascent == frame.ascent, "hbox ascent equals the glyph ascent");

    tex_core_range range = tex_core_node_range(glyph);
    txc_check_size(test, range.begin, 0, "math x range begin");
    txc_check_size(test, range.end, 1, "math x range end");
    tex_core_render_tree_free(tree);

    /* Display math has no skeleton-visible difference for ordinary atoms. */
    txc_check_dumps_equal(test, "x", "x", TEX_CORE_MODE_MATH_DISPLAY, "math display x");
}

static void txc_test_document_glyph(txc_test *test) {
    tex_core_render_tree *tree = txc_compile(test, "x", TEX_CORE_MODE_DOCUMENT, "document x");
    const tex_core_node *glyph = tex_core_node_child(tex_core_render_tree_root(tree), 0);
    tex_core_glyph view = tex_core_node_glyph(glyph);
    txc_check_int(test, view.style, TEX_CORE_STYLE_UPRIGHT, "document glyphs are upright");
    /* KaTeX Main-Regular x: width 0.52778. */
    tex_core_frame frame = tex_core_node_frame(glyph);
    txc_check(test, frame.width == txc_expected_points(0.52778), "document x width");
    tex_core_render_tree_free(tree);
}

static void txc_test_math_digits(txc_test *test) {
    tex_core_render_tree *tree = txc_compile(test, "x0", TEX_CORE_MODE_MATH_INLINE, "math x0");
    const tex_core_node *root = tex_core_render_tree_root(tree);
    const tex_core_node *digit = tex_core_node_child(root, 1);
    txc_check_int(test, tex_core_node_glyph(digit).style, TEX_CORE_STYLE_UPRIGHT, "math digits stay upright");
    /* The second advance starts after the first: Math-Italic x width
     * 0.57153, italic correction 0. */
    tex_core_frame frame = tex_core_node_frame(digit);
    txc_check(test, frame.x == txc_expected_points(0.57153), "math x0 digit offset");
    tex_core_render_tree_free(tree);
}

static void txc_test_italic_correction(txc_test *test) {
    /* KaTeX Math-Italic f: width 0.48959, italic 0.10764, depth 0.19444 —
     * the advance appends the italic correction (TeX Appendix G for Ord
     * atoms; the skeleton has no scripts, so unconditionally). */
    tex_core_render_tree *tree = txc_compile(test, "fx", TEX_CORE_MODE_MATH_INLINE, "math fx");
    const tex_core_node *root = tex_core_render_tree_root(tree);
    const tex_core_node *f = tex_core_node_child(root, 0);
    const tex_core_node *x = tex_core_node_child(root, 1);
    tex_core_frame f_frame = tex_core_node_frame(f);
    txc_check(test, f_frame.italic == txc_expected_points(0.10764), "math f italic correction");
    txc_check(test, f_frame.descent == txc_expected_points(0.19444), "math f descent");
    tex_core_frame x_frame = tex_core_node_frame(x);
    txc_check(
        test,
        x_frame.x == txc_expected_points(0.48959) + txc_expected_points(0.10764),
        "math fx advance includes the italic correction"
    );
    tex_core_frame box = tex_core_node_frame(root);
    txc_check(test, box.descent == f_frame.descent, "hbox descent tracks the deepest glyph");
    tex_core_render_tree_free(tree);
}

static void txc_test_spacing(txc_test *test) {
    /* Explicit spacing in scaled points at the 10pt em: the em fractions
     * are fixed in layout.c (interword 21845, thin 10923, medium 14564,
     * thick 18204, quad 65536, qquad 131072 in 16.16 units). */
    static const struct {
        const char *source;
        double width;
    } cases[] = {
        {"\\,", 109230.0 / 65536.0},
        {"\\:", 145640.0 / 65536.0},
        {"\\;", 182040.0 / 65536.0},
        {"\\!", -109230.0 / 65536.0},
        {"\\quad", 655360.0 / 65536.0},
        {"\\qquad", 1310720.0 / 65536.0},
        {"\\ ", 218450.0 / 65536.0},
    };
    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
        tex_core_render_tree *tree =
            txc_compile(test, cases[index].source, TEX_CORE_MODE_MATH_INLINE, cases[index].source);
        const tex_core_node *kern = tex_core_node_child(tex_core_render_tree_root(tree), 0);
        txc_check_int(test, tex_core_node_get_kind(kern), TEX_CORE_NODE_KERN, "explicit space is a kern");
        tex_core_frame frame = tex_core_node_frame(kern);
        txc_check(test, frame.width == cases[index].width, "kern width for %s", cases[index].source);
        tex_core_render_tree_free(tree);
    }

    /* The interword space in document mode. */
    tex_core_render_tree *tree = txc_compile(test, "a b", TEX_CORE_MODE_DOCUMENT, "document a b");
    const tex_core_node *kern = tex_core_node_child(tex_core_render_tree_root(tree), 1);
    tex_core_frame frame = tex_core_node_frame(kern);
    txc_check(test, frame.width == 218450.0 / 65536.0, "interword space width");
    tex_core_range range = tex_core_node_range(kern);
    txc_check_size(test, range.begin, 1, "interword range begin");
    txc_check_size(test, range.end, 2, "interword range end");
    tex_core_render_tree_free(tree);
}

static void txc_test_whitespace(txc_test *test) {
    /* Dumps embed source ranges, so equality checks across different
     * sources must be structural, not byte comparisons of dumps. */

    /* TeX ignores blanks in math mode: no kern appears between glyphs. */
    tex_core_render_tree *tree = txc_compile(test, "a b", TEX_CORE_MODE_MATH_INLINE, "math a b");
    const tex_core_node *root = tex_core_render_tree_root(tree);
    txc_check_size(test, tex_core_node_child_count(root), 2, "math blanks are ignored");
    txc_check_int(
        test,
        tex_core_node_get_kind(tex_core_node_child(root, 1)),
        TEX_CORE_NODE_GLYPH,
        "math a b second child"
    );
    tex_core_render_tree_free(tree);

    /* A whitespace run collapses to one interword space covering the run. */
    tree = txc_compile(test, "a  \t b", TEX_CORE_MODE_DOCUMENT, "blank run");
    root = tex_core_render_tree_root(tree);
    txc_check_size(test, tex_core_node_child_count(root), 3, "blank runs collapse to one kern");
    const tex_core_node *kern = tex_core_node_child(root, 1);
    txc_check_int(test, tex_core_node_get_kind(kern), TEX_CORE_NODE_KERN, "blank run kern kind");
    txc_check(test, tex_core_node_frame(kern).width == 218450.0 / 65536.0, "blank run kern width");
    txc_check_size(test, tex_core_node_range(kern).begin, 1, "blank run kern range begin");
    txc_check_size(test, tex_core_node_range(kern).end, 5, "blank run kern range end");
    tex_core_render_tree_free(tree);

    /* A single line end is an interword space at the same byte positions. */
    txc_check_dumps_equal(test, "a\nb", "a b", TEX_CORE_MODE_DOCUMENT, "newline is a space");

    /* CRLF scans like LF: one interword space spanning both bytes. */
    tree = txc_compile(test, "a\r\nb", TEX_CORE_MODE_DOCUMENT, "CRLF");
    root = tex_core_render_tree_root(tree);
    txc_check_size(test, tex_core_node_child_count(root), 3, "CRLF is one space");
    kern = tex_core_node_child(root, 1);
    txc_check(test, tex_core_node_frame(kern).width == 218450.0 / 65536.0, "CRLF kern width");
    txc_check_size(test, tex_core_node_range(kern).end, 3, "CRLF kern range end");
    txc_check_size(test, tex_core_node_range(tex_core_node_child(root, 2)).begin, 3, "glyph after CRLF");
    tex_core_render_tree_free(tree);

    /* Blanks after a control word belong to the control word. */
    tree = txc_compile(test, "a\\quad b", TEX_CORE_MODE_DOCUMENT, "a quad b");
    txc_check_size(test, tex_core_node_child_count(tex_core_render_tree_root(tree)), 3, "no space after control word");
    tex_core_render_tree_free(tree);
}

int main(void) {
    txc_test test = {0, 0};
    txc_test_math_glyph(&test);
    txc_test_document_glyph(&test);
    txc_test_math_digits(&test);
    txc_test_italic_correction(&test);
    txc_test_spacing(&test);
    txc_test_whitespace(&test);
    return txc_test_finish(&test, "engine");
}
