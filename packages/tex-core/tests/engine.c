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

    /* Blanks after a control word belong to the control word, including a
     * single line end (TeX state S) — but only a single one; a blank line
     * there still fails fast as a paragraph break (covered in errors.c). */
    tree = txc_compile(test, "a\\quad b", TEX_CORE_MODE_DOCUMENT, "a quad b");
    txc_check_size(test, tex_core_node_child_count(tex_core_render_tree_root(tree)), 3, "no space after control word");
    tex_core_render_tree_free(tree);
    tree = txc_compile(test, "a\\quad\nb", TEX_CORE_MODE_DOCUMENT, "a quad newline b");
    txc_check_size(
        test,
        tex_core_node_child_count(tex_core_render_tree_root(tree)),
        3,
        "no space after control word newline"
    );
    tex_core_render_tree_free(tree);
}

/* Inter-atom spacing amounts in scaled points at the 10pt em: thin 10923,
 * medium 14564, thick 18204 in 16.16 em units — the same values as the
 * explicit \, \: \; commands. */
#define TXC_POINTS_THIN (109230.0 / 65536.0)
#define TXC_POINTS_MEDIUM (145640.0 / 65536.0)
#define TXC_POINTS_THICK (182040.0 / 65536.0)

static void
txc_check_kern_at(txc_test *test, const tex_core_node *root, size_t index, double width, const char *label) {
    const tex_core_node *kern = tex_core_node_child(root, index);
    txc_check(test, tex_core_node_get_kind(kern) == TEX_CORE_NODE_KERN, "%s: child %zu is a kern", label, index);
    txc_check(test, tex_core_node_frame(kern).width == width, "%s: kern width at child %zu", label, index);
}

static void txc_test_math_classes(txc_test *test) {
    /* Bin atoms take medium spacing against ordinaries; Rel atoms thick. */
    tex_core_render_tree *tree = txc_compile(test, "a+b", TEX_CORE_MODE_MATH_INLINE, "math a+b");
    const tex_core_node *root = tex_core_render_tree_root(tree);
    txc_check_size(test, tex_core_node_child_count(root), 5, "a+b children");
    txc_check_kern_at(test, root, 1, TXC_POINTS_MEDIUM, "a+b");
    txc_check_kern_at(test, root, 3, TXC_POINTS_MEDIUM, "a+b");
    const tex_core_node *plus = tex_core_node_child(root, 2);
    txc_check_int(test, (long long)tex_core_node_glyph(plus).codepoint, 0x2B, "plus keeps its codepoint");
    txc_check_int(test, tex_core_node_glyph(plus).style, TEX_CORE_STYLE_UPRIGHT, "math characters are upright");
    /* The inter-atom kern's source range is the gap between the atoms —
     * empty for adjacent atoms. */
    tex_core_range gap = tex_core_node_range(tex_core_node_child(root, 1));
    txc_check_size(test, gap.begin, 1, "inter-atom kern range begin");
    txc_check_size(test, gap.end, 1, "inter-atom kern range end");
    tex_core_render_tree_free(tree);

    tree = txc_compile(test, "a=b", TEX_CORE_MODE_MATH_INLINE, "math a=b");
    root = tex_core_render_tree_root(tree);
    txc_check_size(test, tex_core_node_child_count(root), 5, "a=b children");
    txc_check_kern_at(test, root, 1, TXC_POINTS_THICK, "a=b");
    txc_check_kern_at(test, root, 3, TXC_POINTS_THICK, "a=b");
    tex_core_render_tree_free(tree);

    /* Punct spaces only to its right (Ord-Punct 0, Punct-Ord thin). */
    tree = txc_compile(test, "a,b", TEX_CORE_MODE_MATH_INLINE, "math a,b");
    root = tex_core_render_tree_root(tree);
    txc_check_size(test, tex_core_node_child_count(root), 4, "a,b children");
    txc_check_kern_at(test, root, 2, TXC_POINTS_THIN, "a,b");
    tex_core_render_tree_free(tree);

    /* The colon character is a relation; \colon is punctuation. */
    tree = txc_compile(test, "a:b", TEX_CORE_MODE_MATH_INLINE, "math a:b");
    root = tex_core_render_tree_root(tree);
    txc_check_size(test, tex_core_node_child_count(root), 5, "a:b children");
    txc_check_kern_at(test, root, 1, TXC_POINTS_THICK, "a:b");
    tex_core_render_tree_free(tree);
    tree = txc_compile(test, "a\\colon b", TEX_CORE_MODE_MATH_INLINE, "math a colon b");
    root = tex_core_render_tree_root(tree);
    txc_check_size(test, tex_core_node_child_count(root), 4, "a colon b children");
    txc_check_int(
        test,
        (long long)tex_core_node_glyph(tex_core_node_child(root, 1)).codepoint,
        0x3A,
        "\\colon renders the colon"
    );
    txc_check_kern_at(test, root, 2, TXC_POINTS_THIN, "a colon b");
    tex_core_render_tree_free(tree);

    /* Remapped math characters render the plain TeX glyphs. */
    static const struct {
        const char *source;
        uint32_t codepoint;
    } remapped[] = {{"-", 0x2212}, {"*", 0x2217}, {"|", 0x2223}};
    for (size_t index = 0; index < sizeof(remapped) / sizeof(remapped[0]); index++) {
        tree = txc_compile(test, remapped[index].source, TEX_CORE_MODE_MATH_INLINE, remapped[index].source);
        root = tex_core_render_tree_root(tree);
        txc_check(
            test,
            tex_core_node_glyph(tex_core_node_child(root, 0)).codepoint == remapped[index].codepoint,
            "remapped codepoint for %s",
            remapped[index].source
        );
        tex_core_render_tree_free(tree);
    }

    /* Document mode has no atom classes: the comma stays a plain ordinary
     * and no spacing is inserted. */
    tree = txc_compile(test, "a,b", TEX_CORE_MODE_DOCUMENT, "document a,b");
    root = tex_core_render_tree_root(tree);
    txc_check_size(test, tex_core_node_child_count(root), 3, "document a,b children");
    tex_core_render_tree_free(tree);
}

static void txc_test_bin_context(txc_test *test) {
    /* A leading Bin is an ordinary atom (TeXbook chapter 18). */
    tex_core_render_tree *tree = txc_compile(test, "-a", TEX_CORE_MODE_MATH_INLINE, "math -a");
    const tex_core_node *root = tex_core_render_tree_root(tree);
    txc_check_size(test, tex_core_node_child_count(root), 2, "-a has no spacing");
    tex_core_render_tree_free(tree);

    /* A Bin after a Bin demotes; the surviving Bin still spaces. */
    tree = txc_compile(test, "a+-b", TEX_CORE_MODE_MATH_INLINE, "math a+-b");
    root = tex_core_render_tree_root(tree);
    txc_check_size(test, tex_core_node_child_count(root), 6, "a+-b children");
    txc_check_kern_at(test, root, 1, TXC_POINTS_MEDIUM, "a+-b");
    txc_check_kern_at(test, root, 3, TXC_POINTS_MEDIUM, "a+-b");
    tex_core_render_tree_free(tree);

    /* A Bin before a Rel demotes retroactively. */
    tree = txc_compile(test, "a-=b", TEX_CORE_MODE_MATH_INLINE, "math a-=b");
    root = tex_core_render_tree_root(tree);
    txc_check_size(test, tex_core_node_child_count(root), 6, "a-=b children");
    txc_check_int(
        test,
        tex_core_node_get_kind(tex_core_node_child(root, 1)),
        TEX_CORE_NODE_GLYPH,
        "demoted minus binds tight to its left"
    );
    txc_check_kern_at(test, root, 2, TXC_POINTS_THICK, "a-=b");
    tex_core_render_tree_free(tree);

    /* Bins demote after Open and before Close; a trailing Bin demotes. */
    tree = txc_compile(test, "(-a-)", TEX_CORE_MODE_MATH_INLINE, "math (-a-)");
    root = tex_core_render_tree_root(tree);
    txc_check_size(test, tex_core_node_child_count(root), 5, "(-a-) has no spacing");
    tex_core_render_tree_free(tree);
    tree = txc_compile(test, "a-", TEX_CORE_MODE_MATH_INLINE, "math a-");
    root = tex_core_render_tree_root(tree);
    txc_check_size(test, tex_core_node_child_count(root), 2, "a- has no spacing");
    tex_core_render_tree_free(tree);

    /* Explicit spacing neither suppresses the inter-atom space nor changes
     * demotion: both kerns appear, explicit first. */
    tree = txc_compile(test, "a\\,+b", TEX_CORE_MODE_MATH_INLINE, "math a thin +b");
    root = tex_core_render_tree_root(tree);
    txc_check_size(test, tex_core_node_child_count(root), 6, "a thin +b children");
    txc_check_kern_at(test, root, 1, TXC_POINTS_THIN, "a thin +b explicit");
    txc_check_kern_at(test, root, 2, TXC_POINTS_MEDIUM, "a thin +b inter-atom");
    tex_core_render_tree_free(tree);
}

static void txc_test_symbol_commands(txc_test *test) {
    /* Lowercase Greek takes the math italic face; uppercase stays upright. */
    tex_core_render_tree *tree = txc_compile(test, "\\alpha", TEX_CORE_MODE_MATH_INLINE, "alpha");
    const tex_core_node *glyph = tex_core_node_child(tex_core_render_tree_root(tree), 0);
    txc_check_int(test, (long long)tex_core_node_glyph(glyph).codepoint, 0x3B1, "alpha codepoint");
    txc_check_int(test, tex_core_node_glyph(glyph).style, TEX_CORE_STYLE_ITALIC, "alpha is italic");
    tex_core_render_tree_free(tree);
    tree = txc_compile(test, "\\Gamma", TEX_CORE_MODE_MATH_INLINE, "Gamma");
    glyph = tex_core_node_child(tex_core_render_tree_root(tree), 0);
    txc_check_int(test, (long long)tex_core_node_glyph(glyph).codepoint, 0x393, "Gamma codepoint");
    txc_check_int(test, tex_core_node_glyph(glyph).style, TEX_CORE_STYLE_UPRIGHT, "Gamma is upright");
    tex_core_render_tree_free(tree);

    /* Symbol commands carry their class into spacing. */
    tree = txc_compile(test, "a\\leq b", TEX_CORE_MODE_MATH_DISPLAY, "a leq b");
    const tex_core_node *root = tex_core_render_tree_root(tree);
    txc_check_size(test, tex_core_node_child_count(root), 5, "a leq b children");
    txc_check_kern_at(test, root, 1, TXC_POINTS_THICK, "a leq b");
    txc_check_kern_at(test, root, 3, TXC_POINTS_THICK, "a leq b");
    tex_core_render_tree_free(tree);

    /* Aliases resolve to the same glyph and class. */
    static const struct {
        const char *alias;
        uint32_t codepoint;
    } aliases[] = {{"\\le", 0x2264}, {"\\to", 0x2192}, {"\\gets", 0x2190}, {"\\lnot", 0xAC}, {"\\owns", 0x220B}};
    for (size_t index = 0; index < sizeof(aliases) / sizeof(aliases[0]); index++) {
        tree = txc_compile(test, aliases[index].alias, TEX_CORE_MODE_MATH_INLINE, aliases[index].alias);
        glyph = tex_core_node_child(tex_core_render_tree_root(tree), 0);
        txc_check(
            test,
            tex_core_node_glyph(glyph).codepoint == aliases[index].codepoint,
            "alias codepoint for %s",
            aliases[index].alias
        );
        tex_core_render_tree_free(tree);
    }

    /* Control-symbol delimiters reach the brace glyphs the bare reserved
     * characters reject. */
    tree = txc_compile(test, "\\{x\\}", TEX_CORE_MODE_MATH_INLINE, "braces");
    root = tex_core_render_tree_root(tree);
    txc_check_size(test, tex_core_node_child_count(root), 3, "braces children");
    txc_check_int(
        test,
        (long long)tex_core_node_glyph(tex_core_node_child(root, 0)).codepoint,
        0x7B,
        "left brace codepoint"
    );
    txc_check_int(
        test,
        (long long)tex_core_node_glyph(tex_core_node_child(root, 2)).codepoint,
        0x7D,
        "right brace codepoint"
    );
    tex_core_render_tree_free(tree);
}

/* Expected script-size points: the em fraction scaled by the 7pt em. */
static double txc_expected_script_points(double em_value) {
    long long fraction = (long long)(em_value * 65536.0 + 0.5);
    return (double)(fraction * 7) / 65536.0;
}

static void txc_test_fractions(txc_test *test) {
    /* \nulldelimiterspace is 1.2pt (78643sp) at every size. */
    const double null_delimiter = 78643.0 / 65536.0;

    /* Text style: shifts num2/denom2, bar defaultRuleThickness thick,
     * centered on axisHeight with its top half(thickness) above (KaTeX
     * text columns: num2 0.394, denom2 0.345, thickness 0.04, axis 0.25).
     * The 1/2 parts set at the script size share the digit width, so no
     * centering inset appears. */
    tex_core_render_tree *tree = txc_compile(test, "\\frac{1}{2}", TEX_CORE_MODE_MATH_INLINE, "frac 1 2");
    const tex_core_node *root = tex_core_render_tree_root(tree);
    txc_check_size(test, tex_core_node_child_count(root), 1, "frac is one atom");
    const tex_core_node *box = tex_core_node_child(root, 0);
    txc_check_size(test, tex_core_node_child_count(box), 5, "frac box children");
    const tex_core_node *left = tex_core_node_child(box, 0);
    const tex_core_node *rule = tex_core_node_child(box, 1);
    const tex_core_node *num = tex_core_node_child(box, 2);
    const tex_core_node *den = tex_core_node_child(box, 3);
    const tex_core_node *right = tex_core_node_child(box, 4);
    txc_check_int(test, tex_core_node_get_kind(left), TEX_CORE_NODE_KERN, "left null delimiter is a kern");
    txc_check(test, tex_core_node_frame(left).width == null_delimiter, "left null delimiter width");
    txc_check_int(test, tex_core_node_get_kind(rule), TEX_CORE_NODE_RULE, "the bar is a rule");
    txc_check(test, tex_core_node_frame(rule).ascent == txc_expected_points(0.04), "bar thickness");
    txc_check(test, tex_core_node_frame(rule).descent == 0.0, "bar descent");
    /* Bar bottom = axis + half(thickness) - thickness in scaled points:
     * 163840 + 13105 - 26210. */
    txc_check(test, tex_core_node_frame(rule).y == 150735.0 / 65536.0, "bar sits on the axis");
    txc_check_size(test, tex_core_node_range(rule).begin, 0, "bar src begin is the command");
    txc_check_size(test, tex_core_node_range(rule).end, 5, "bar src end is the command");
    txc_check(test, tex_core_node_frame(num).y == txc_expected_points(0.394), "numerator shift num2");
    txc_check(test, tex_core_node_frame(den).y == -txc_expected_points(0.345), "denominator shift denom2");
    txc_check(
        test,
        tex_core_node_frame(num).x == null_delimiter && tex_core_node_frame(den).x == null_delimiter,
        "equal-width parts take no centering inset"
    );
    txc_check(test, tex_core_node_glyph(tex_core_node_child(num, 0)).size == 7.0, "numerator sets at the script size");
    txc_check(test, tex_core_node_frame(right).width == null_delimiter, "right null delimiter width");
    tex_core_frame frame = tex_core_node_frame(box);
    txc_check(
        test,
        frame.width == 2.0 * null_delimiter + tex_core_node_frame(rule).width,
        "frac box width spans the delimiters"
    );
    txc_check(
        test,
        frame.ascent == tex_core_node_frame(num).y + tex_core_node_frame(num).ascent,
        "frac box ascent tracks the numerator"
    );
    tex_core_render_tree_free(tree);

    /* Display style: shifts num1/denom1, parts one style down at the text
     * size, the narrower part centered over the wider. */
    tree = txc_compile(test, "\\frac{a+b}{2}", TEX_CORE_MODE_MATH_DISPLAY, "display frac");
    box = tex_core_node_child(tex_core_render_tree_root(tree), 0);
    num = tex_core_node_child(box, 2);
    den = tex_core_node_child(box, 3);
    txc_check(test, tex_core_node_frame(num).y == txc_expected_points(0.677), "display numerator shift num1");
    txc_check(test, tex_core_node_frame(den).y == -txc_expected_points(0.686), "display denominator shift denom1");
    txc_check(test, tex_core_node_glyph(tex_core_node_child(num, 0)).size == 10.0, "display parts keep the text size");
    double inset = (tex_core_node_frame(num).width - tex_core_node_frame(den).width) / 2.0;
    txc_check(
        test,
        tex_core_node_frame(den).x == null_delimiter + inset,
        "the narrower denominator centers over the numerator"
    );
    tex_core_render_tree_free(tree);

    /* \dfrac forces display style; \tfrac forces text style. */
    tree = txc_compile(test, "\\dfrac{1}{2}", TEX_CORE_MODE_MATH_INLINE, "dfrac");
    box = tex_core_node_child(tex_core_render_tree_root(tree), 0);
    txc_check(
        test,
        tex_core_node_frame(tex_core_node_child(box, 2)).y == txc_expected_points(0.677),
        "dfrac takes the display shift"
    );
    tex_core_render_tree_free(tree);
    tree = txc_compile(test, "\\tfrac{1}{2}", TEX_CORE_MODE_MATH_DISPLAY, "tfrac");
    box = tex_core_node_child(tex_core_render_tree_root(tree), 0);
    txc_check(
        test,
        tex_core_node_frame(tex_core_node_child(box, 2)).y == txc_expected_points(0.394),
        "tfrac takes the text shift"
    );
    txc_check(
        test,
        tex_core_node_glyph(tex_core_node_child(tex_core_node_child(box, 2), 0)).size == 7.0,
        "tfrac parts drop to the script size"
    );
    tex_core_render_tree_free(tree);

    /* Undelimited character arguments parse like braced ones. */
    tree = txc_compile(test, "\\frac12", TEX_CORE_MODE_MATH_INLINE, "frac char args");
    box = tex_core_node_child(tex_core_render_tree_root(tree), 0);
    txc_check_size(test, tex_core_node_child_count(box), 5, "char-arg frac box children");
    txc_check(
        test,
        tex_core_node_frame(tex_core_node_child(box, 2)).y == txc_expected_points(0.394),
        "char-arg numerator shift"
    );
    tex_core_render_tree_free(tree);

    /* A fraction is a legal script argument, set one style deeper: script
     * columns for the fraction, scriptscript parts (KaTeX script columns:
     * num2 0.384, thickness 0.049). */
    tree = txc_compile(test, "x^\\frac{1}{2}", TEX_CORE_MODE_MATH_INLINE, "frac as superscript");
    root = tex_core_render_tree_root(tree);
    const tex_core_node *sup = tex_core_node_child(root, 1);
    txc_check_size(test, tex_core_node_child_count(sup), 5, "superscript fraction box children");
    rule = tex_core_node_child(sup, 1);
    txc_check_int(test, tex_core_node_get_kind(rule), TEX_CORE_NODE_RULE, "superscript fraction keeps its bar");
    txc_check(test, tex_core_node_frame(rule).ascent == txc_expected_script_points(0.049), "script bar thickness");
    num = tex_core_node_child(sup, 2);
    txc_check(test, tex_core_node_frame(num).y == txc_expected_script_points(0.384), "script numerator shift");
    txc_check(test, tex_core_node_glyph(tex_core_node_child(num, 0)).size == 5.0, "scriptscript parts");
    txc_check(
        test,
        tex_core_node_frame(tex_core_node_child(sup, 0)).width == null_delimiter,
        "null delimiters stay 1.2pt inside scripts"
    );
    tex_core_render_tree_free(tree);

    /* Scripts attach to the fraction atom like to any box nucleus. */
    tree = txc_compile(test, "\\frac{1}{2}^3", TEX_CORE_MODE_MATH_INLINE, "scripted fraction");
    root = tex_core_render_tree_root(tree);
    txc_check_size(test, tex_core_node_child_count(root), 2, "scripted fraction children");
    txc_check(test, tex_core_node_frame(tex_core_node_child(root, 1)).y > 0.0, "superscript shifts up");
    tex_core_render_tree_free(tree);

    /* The fraction atom is Inner: thin space against an ordinary, medium
     * against a kept Bin, thick against a Rel. */
    tree = txc_compile(test, "1+\\frac{1}{2}=x", TEX_CORE_MODE_MATH_INLINE, "inner spacing");
    root = tex_core_render_tree_root(tree);
    txc_check_size(test, tex_core_node_child_count(root), 9, "inner spacing children");
    txc_check_kern_at(test, root, 1, TXC_POINTS_MEDIUM, "ord to kept bin");
    txc_check_kern_at(test, root, 3, TXC_POINTS_MEDIUM, "bin to fraction");
    txc_check_kern_at(test, root, 5, TXC_POINTS_THICK, "fraction to rel");
    txc_check_kern_at(test, root, 7, TXC_POINTS_THICK, "rel to ord");
    tex_core_render_tree_free(tree);
}

static void txc_test_delimiters(txc_test *test) {
    /* A short operand keeps the text-size paren: rule 19 over x's reach
     * (delta1 = 2.5pt) targets 4.49564pt, under the 10pt small glyph. */
    tex_core_render_tree *tree = txc_compile(test, "\\left(x\\right)", TEX_CORE_MODE_MATH_INLINE, "small fence");
    const tex_core_node *box = tex_core_node_child(tex_core_render_tree_root(tree), 0);
    txc_check_size(test, tex_core_node_child_count(box), 3, "small fence children");
    const tex_core_node *left = tex_core_node_child(box, 0);
    txc_check_int(test, tex_core_node_glyph(left).family, TEX_CORE_FAMILY_MAIN, "small fence keeps the main paren");
    txc_check(test, tex_core_node_frame(left).y == 0.0, "the main paren sits axis-centered already");
    tex_core_render_tree_free(tree);

    /* A fraction operand overflows the small paren: Size1 at the text em. */
    tree = txc_compile(test, "\\left(\\frac{1}{2}\\right)", TEX_CORE_MODE_MATH_INLINE, "size1 fence");
    box = tex_core_node_child(tex_core_render_tree_root(tree), 0);
    left = tex_core_node_child(box, 0);
    txc_check_int(test, tex_core_node_glyph(left).family, TEX_CORE_FAMILY_SIZE1, "size1 paren");
    txc_check(test, tex_core_node_glyph(left).size == 10.0, "size faces stay at the text em");
    tex_core_render_tree_free(tree);

    /* Very tall content assembles bracket pieces from Size4. */
    tree = txc_compile(test, "\\left[\\dfrac{1}{\\dfrac{1}{2}}\\right]", TEX_CORE_MODE_MATH_DISPLAY, "assembly");
    box = tex_core_node_child(tex_core_render_tree_root(tree), 0);
    left = tex_core_node_child(box, 0);
    txc_check_int(test, tex_core_node_get_kind(left), TEX_CORE_NODE_HBOX, "assembly is a box");
    txc_check(test, tex_core_node_child_count(left) >= 3, "assembly has top, pieces, bottom");
    txc_check_int(
        test,
        tex_core_node_glyph(tex_core_node_child(left, 0)).family,
        TEX_CORE_FAMILY_SIZE4,
        "bracket pieces come from Size4"
    );
    tex_core_render_tree_free(tree);

    /* The explicit sizes force fixed targets whatever the content. */
    tree = txc_compile(test, "\\bigl(x\\bigr)", TEX_CORE_MODE_MATH_INLINE, "bigl");
    const tex_core_node *root = tex_core_render_tree_root(tree);
    txc_check_size(test, tex_core_node_child_count(root), 3, "bigl children");
    const tex_core_node *nucleus = tex_core_node_child(tex_core_node_child(root, 0), 0);
    txc_check_int(test, tex_core_node_glyph(nucleus).family, TEX_CORE_FAMILY_SIZE1, "big reaches Size1");
    tex_core_render_tree_free(tree);

    /* The null delimiter is a \nulldelimiterspace kern. */
    tree = txc_compile(test, "\\left.x\\right|", TEX_CORE_MODE_MATH_INLINE, "null delimiter");
    box = tex_core_node_child(tex_core_render_tree_root(tree), 0);
    txc_check_int(test, tex_core_node_get_kind(tex_core_node_child(box, 0)), TEX_CORE_NODE_KERN, "null left is a kern");
    txc_check(test, tex_core_node_frame(tex_core_node_child(box, 0)).width == 78643.0 / 65536.0, "null kern width");
    tex_core_render_tree_free(tree);

    /* \binom: barless shifts (num3 in text style) inside real parens. */
    tree = txc_compile(test, "\\binom{n}{k}", TEX_CORE_MODE_MATH_INLINE, "binom");
    box = tex_core_node_child(tex_core_render_tree_root(tree), 0);
    txc_check_size(test, tex_core_node_child_count(box), 4, "binom children");
    txc_check_int(test, tex_core_node_glyph(tex_core_node_child(box, 0)).family, TEX_CORE_FAMILY_SIZE1, "binom paren");
    txc_check(test, tex_core_node_frame(tex_core_node_child(box, 1)).y == txc_expected_points(0.444), "binom num3");
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
    txc_test_math_classes(&test);
    txc_test_bin_context(&test);
    txc_test_symbol_commands(&test);
    txc_test_fractions(&test);
    txc_test_delimiters(&test);
    return txc_test_finish(&test, "engine");
}
