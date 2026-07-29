/* Engine behavior: metrics-backed geometry, mode-dependent styles, explicit
 * spacing, and tokenizer whitespace rules — checked through the public node
 * views against independently derived scaled-point values. */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "dump.h"
#include "harness.h"
#include "memory.h"
#include "parse.h"
#include "scaled.h"
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

static void txc_check_dumps_equal(
    txc_test *test,
    const char *left,
    const char *right,
    tex_core_mode mode,
    const char *label
) {
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

static void txc_check_kern_at(
    txc_test *test,
    const tex_core_node *root,
    size_t index,
    double width,
    const char *label
) {
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
    /* Both parens share the command's source, so the right one precedes
     * the parts in child (source) order and is placed by x alone. */
    txc_check_int(
        test,
        tex_core_node_glyph(tex_core_node_child(box, 1)).family,
        TEX_CORE_FAMILY_SIZE1,
        "binom right paren follows in source order"
    );
    txc_check(
        test,
        tex_core_node_frame(tex_core_node_child(box, 1)).x > tex_core_node_frame(tex_core_node_child(box, 3)).x,
        "binom right paren sits past the parts"
    );
    txc_check(test, tex_core_node_frame(tex_core_node_child(box, 2)).y == txc_expected_points(0.444), "binom num3");
    tex_core_render_tree_free(tree);
}

static void txc_test_radicals(txc_test *test) {
    /* Rule 11 in text style: clearance starts at 1.25 thicknesses and
     * gains half the sign's excess; the bar (one thickness thick) sits
     * flush with the sign's ink top. */
    tex_core_render_tree *tree = txc_compile(test, "\\sqrt{x+1}", TEX_CORE_MODE_MATH_INLINE, "sqrt");
    const tex_core_node *box = tex_core_node_child(tex_core_render_tree_root(tree), 0);
    txc_check_size(test, tex_core_node_child_count(box), 3, "sqrt children");
    const tex_core_node *sign = tex_core_node_child(box, 0);
    const tex_core_node *bar = tex_core_node_child(box, 1);
    const tex_core_node *radicand = tex_core_node_child(box, 2);
    txc_check_int(test, (long long)tex_core_node_glyph(sign).codepoint, 0x221A, "sqrt sign");
    txc_check_int(test, tex_core_node_get_kind(bar), TEX_CORE_NODE_RULE, "sqrt bar is a rule");
    txc_check(test, tex_core_node_frame(bar).ascent == txc_expected_points(0.04), "bar thickness");
    tex_core_frame sign_frame = tex_core_node_frame(sign);
    tex_core_frame bar_frame = tex_core_node_frame(bar);
    txc_check(
        test,
        bar_frame.y + bar_frame.ascent == sign_frame.y + sign_frame.ascent,
        "the bar top is flush with the sign top"
    );
    txc_check(test, bar_frame.width == tex_core_node_frame(radicand).width, "the bar spans the radicand");
    tex_core_frame whole = tex_core_node_frame(box);
    txc_check(test, whole.ascent == bar_frame.y + bar_frame.ascent, "box ascent is the bar top");
    tex_core_render_tree_free(tree);

    /* The optional index sets at 5pt, raised, with the -10mu tuck. */
    tree = txc_compile(test, "\\sqrt[3]{x}", TEX_CORE_MODE_MATH_INLINE, "sqrt index");
    box = tex_core_node_child(tex_core_render_tree_root(tree), 0);
    txc_check_size(test, tex_core_node_child_count(box), 6, "indexed sqrt children");
    const tex_core_node *index_box = tex_core_node_child(box, 3);
    txc_check(test, tex_core_node_frame(index_box).y > 0.0, "index is raised");
    txc_check(
        test,
        tex_core_node_glyph(tex_core_node_child(index_box, 0)).size == 5.0,
        "index sets at scriptscript size"
    );
    txc_check(test, tex_core_node_frame(tex_core_node_child(box, 4)).width < 0.0, "the -10mu kern tucks the sign");
    tex_core_render_tree_free(tree);

    /* A tall radicand climbs the size faces. */
    tree = txc_compile(test, "\\sqrt{\\dfrac{1}{2}}", TEX_CORE_MODE_MATH_DISPLAY, "sqrt tall");
    box = tex_core_node_child(tex_core_render_tree_root(tree), 0);
    txc_check_int(
        test,
        tex_core_node_glyph(tex_core_node_child(box, 0)).family,
        TEX_CORE_FAMILY_SIZE3,
        "tall sqrt sign from Size3"
    );
    tex_core_render_tree_free(tree);
}

static void txc_test_operators(txc_test *test) {
    /* Display style takes the Size2 variant with limits above and below
     * (rule 13a); the assembly is one box padded by bigOpSpacing5. */
    tex_core_render_tree *tree = txc_compile(test, "\\sum_{i}^{n}", TEX_CORE_MODE_MATH_DISPLAY, "sum limits");
    const tex_core_node *root = tex_core_render_tree_root(tree);
    txc_check_size(test, tex_core_node_child_count(root), 1, "sum is one assembly");
    const tex_core_node *assembly = tex_core_node_child(root, 0);
    txc_check_size(test, tex_core_node_child_count(assembly), 3, "assembly children");
    const tex_core_node *op = tex_core_node_child(assembly, 0);
    txc_check_int(test, tex_core_node_glyph(op).family, TEX_CORE_FAMILY_SIZE2, "display sum from Size2");
    txc_check(test, tex_core_node_frame(tex_core_node_child(assembly, 1)).y < 0.0, "subscript limit sits below");
    txc_check(test, tex_core_node_frame(tex_core_node_child(assembly, 2)).y > 0.0, "superscript limit sits above");
    tex_core_render_tree_free(tree);

    /* Text style keeps Size1 and normal scripts. */
    tree = txc_compile(test, "\\sum_{i}", TEX_CORE_MODE_MATH_INLINE, "sum inline");
    root = tex_core_render_tree_root(tree);
    txc_check_size(test, tex_core_node_child_count(root), 2, "inline sum has sibling scripts");
    txc_check_int(
        test,
        tex_core_node_glyph(tex_core_node_child(root, 0)).family,
        TEX_CORE_FAMILY_SIZE1,
        "text sum from Size1"
    );
    tex_core_render_tree_free(tree);

    /* Integrals default to nolimits: the subscript tucks under the slant
     * by the italic correction. */
    tree = txc_compile(test, "\\int_a^b", TEX_CORE_MODE_MATH_DISPLAY, "integral");
    root = tex_core_render_tree_root(tree);
    txc_check_size(test, tex_core_node_child_count(root), 3, "integral children");
    const tex_core_node *integral = tex_core_node_child(root, 0);
    tex_core_frame sub = tex_core_node_frame(tex_core_node_child(root, 1));
    tex_core_frame sup = tex_core_node_frame(tex_core_node_child(root, 2));
    tex_core_frame base = tex_core_node_frame(integral);
    txc_check(test, sub.y < 0.0 && sup.y > 0.0, "integral scripts attach normally");
    txc_check(
        test,
        sup.x == base.width + tex_core_node_frame(integral).italic && sub.x == base.width,
        "the superscript sits the italic correction past the subscript"
    );
    tex_core_render_tree_free(tree);

    /* \limits and \nolimits override the defaults. */
    tree = txc_compile(test, "\\int\\limits_a", TEX_CORE_MODE_MATH_DISPLAY, "int limits");
    txc_check_size(test, tex_core_node_child_count(tex_core_render_tree_root(tree)), 1, "\\limits forces the assembly");
    tex_core_render_tree_free(tree);
    tree = txc_compile(test, "\\sum\\nolimits_i", TEX_CORE_MODE_MATH_DISPLAY, "sum nolimits");
    txc_check_size(
        test,
        tex_core_node_child_count(tex_core_render_tree_root(tree)),
        2,
        "\\nolimits keeps sibling scripts"
    );
    tex_core_render_tree_free(tree);

    /* Function names are upright letter runs; \limsup carries its thin
     * space; Op-Ord spacing separates the name from its operand. */
    tree = txc_compile(test, "\\sin x", TEX_CORE_MODE_MATH_INLINE, "sin");
    root = tex_core_render_tree_root(tree);
    txc_check_size(test, tex_core_node_child_count(root), 3, "sin x children");
    const tex_core_node *name = tex_core_node_child(root, 0);
    txc_check_size(test, tex_core_node_child_count(name), 3, "sin letters");
    txc_check_int(
        test,
        tex_core_node_glyph(tex_core_node_child(name, 0)).style,
        TEX_CORE_STYLE_UPRIGHT,
        "function letters are upright"
    );
    txc_check_kern_at(test, root, 1, TXC_POINTS_THIN, "Op-Ord thin space");
    tex_core_render_tree_free(tree);
    tree = txc_compile(test, "\\limsup", TEX_CORE_MODE_MATH_INLINE, "limsup");
    name = tex_core_node_child(tex_core_render_tree_root(tree), 0);
    txc_check_size(test, tex_core_node_child_count(name), 7, "limsup letters and thin space");
    txc_check_int(
        test,
        tex_core_node_get_kind(tex_core_node_child(name, 3)),
        TEX_CORE_NODE_KERN,
        "limsup inner thin space"
    );
    tex_core_render_tree_free(tree);
}

static void txc_test_accents(txc_test *test) {
    /* Rule 12: the accent centers over the nucleus width plus the
     * nucleus character's skew, at its natural height over x-height
     * material, and the box keeps the nucleus width. */
    tex_core_render_tree *tree = txc_compile(test, "\\hat x", TEX_CORE_MODE_MATH_INLINE, "hat");
    const tex_core_node *box = tex_core_node_child(tex_core_render_tree_root(tree), 0);
    txc_check_size(test, tex_core_node_child_count(box), 2, "accent children");
    const tex_core_node *accent = tex_core_node_child(box, 0);
    const tex_core_node *nucleus = tex_core_node_child(box, 1);
    txc_check_int(test, (long long)tex_core_node_glyph(accent).codepoint, 0x2C6, "hat glyph");
    txc_check(test, tex_core_node_frame(accent).y == 0.0, "x-height nucleus keeps the accent at rest");
    txc_check(test, tex_core_node_frame(box).width == tex_core_node_frame(nucleus).width, "accent keeps the width");
    /* Math-Italic x: skew 0.02778, width 0.57153; accent width 0.5. */
    /* The skew applies to a bare character argument only — a braced
     * {x} is a sub-list nucleus and takes none, exactly as TeX. */
    txc_check(
        test,
        tex_core_node_frame(accent).x ==
            txc_expected_points(0.02778) + (txc_expected_points(0.57153) - txc_expected_points(0.5)) / 2.0,
        "accent skews and centers"
    );
    tex_core_render_tree_free(tree);

    /* A tall nucleus lifts the accent by its ascent minus the x-height. */
    tree = txc_compile(test, "\\vec{f}", TEX_CORE_MODE_MATH_INLINE, "vec f");
    box = tex_core_node_child(tex_core_render_tree_root(tree), 0);
    txc_check(test, tex_core_node_frame(tex_core_node_child(box, 0)).y > 0.0, "tall nucleus lifts the accent");
    tex_core_render_tree_free(tree);

    /* An operator routes into a pending accent (\hat\sum). */
    tree = txc_compile(test, "\\hat\\sum", TEX_CORE_MODE_MATH_INLINE, "hat sum");
    box = tex_core_node_child(tex_core_render_tree_root(tree), 0);
    txc_check_size(test, tex_core_node_child_count(box), 2, "accented operator children");
    txc_check_int(
        test,
        tex_core_node_glyph(tex_core_node_child(tex_core_node_child(box, 1), 0)).family,
        TEX_CORE_FAMILY_SIZE1,
        "the accented nucleus is the operator glyph"
    );
    tex_core_render_tree_free(tree);

    /* Wide accents climb the width ladder. */
    tree = txc_compile(test, "\\widehat{x+y}", TEX_CORE_MODE_MATH_INLINE, "widehat");
    box = tex_core_node_child(tex_core_render_tree_root(tree), 0);
    txc_check_int(
        test,
        tex_core_node_glyph(tex_core_node_child(box, 0)).family,
        TEX_CORE_FAMILY_SIZE4,
        "wide accent reaches Size4"
    );
    tex_core_render_tree_free(tree);
}

static void txc_test_styles(txc_test *test) {
    /* Letters and digits rewrite onto the face; other atoms keep their
     * glyphs and classes. */
    tex_core_render_tree *tree = txc_compile(test, "\\mathbf{a+1}", TEX_CORE_MODE_MATH_INLINE, "mathbf");
    const tex_core_node *box = tex_core_node_child(tex_core_render_tree_root(tree), 0);
    txc_check_int(test, tex_core_node_glyph(tex_core_node_child(box, 0)).family, TEX_CORE_FAMILY_BOLD, "bold a");
    txc_check_int(
        test,
        tex_core_node_glyph(tex_core_node_child(box, 2)).family,
        TEX_CORE_FAMILY_MAIN,
        "the plus keeps the main face"
    );
    txc_check_int(test, tex_core_node_glyph(tex_core_node_child(box, 4)).family, TEX_CORE_FAMILY_BOLD, "bold 1");
    tex_core_render_tree_free(tree);

    /* \text keeps interword spaces and scales in scripts. */
    tree = txc_compile(test, "x^{\\text{up up}}", TEX_CORE_MODE_MATH_INLINE, "text in script");
    const tex_core_node *root = tex_core_render_tree_root(tree);
    const tex_core_node *sup = tex_core_node_child(root, 1);
    const tex_core_node *content = tex_core_node_child(sup, 0);
    txc_check(test, tex_core_node_child_count(content) == 5, "text words and space");
    txc_check_int(
        test,
        tex_core_node_get_kind(tex_core_node_child(content, 2)),
        TEX_CORE_NODE_KERN,
        "interword space inside \\text"
    );
    txc_check(test, tex_core_node_glyph(tex_core_node_child(content, 0)).size == 7.0, "text scales in scripts");
    tex_core_render_tree_free(tree);
}

static void txc_test_command_table(txc_test *test) {
    /* Sorted for the binary search, and every row's class index must land
     * on the payload row carrying the same command name. */
    txc_check(test, txc_command_table_sorted(), "the command table is sorted and name-consistent");
}

static void txc_test_nested_styles(txc_test *test) {
    /* The innermost math alphabet wins: an outer style switch must never
     * overwrite an explicit inner one. */
    tex_core_render_tree *tree =
        txc_compile(test, "\\mathbf{a\\mathit{b}c}", TEX_CORE_MODE_MATH_INLINE, "nested styles");
    const tex_core_node *root = tex_core_render_tree_root(tree);
    const tex_core_node *group = tex_core_node_child(root, 0);
    txc_check_int(
        test,
        tex_core_node_glyph(tex_core_node_child(group, 0)).family,
        TEX_CORE_FAMILY_BOLD,
        "outer letter takes the outer face"
    );
    const tex_core_node *inner = tex_core_node_child(group, 1);
    txc_check_int(
        test,
        tex_core_node_glyph(tex_core_node_child(inner, 0)).family,
        TEX_CORE_FAMILY_TEXTIT,
        "inner letter keeps the inner face"
    );
    txc_check_int(
        test,
        tex_core_node_glyph(tex_core_node_child(group, 2)).family,
        TEX_CORE_FAMILY_BOLD,
        "trailing letter returns to the outer face"
    );
    tex_core_render_tree_free(tree);
}

static void txc_test_scaled_rounding(txc_test *test) {
    /* txc_em floors toward negative infinity — the arithmetic-shift result
     * — for negative metric fractions on every platform. */
    txc_check_int(test, txc_em(-65536, 655360), -655360, "negative whole em");
    txc_check_int(test, txc_em(-1, 655360), -10, "negative fraction floors");
    txc_check_int(test, txc_em(-3, 7), -1, "negative sub-unit floors to -1");
    txc_check_int(test, txc_em(1, -655360), -10, "negative em floors");
    txc_check_int(test, txc_em(3, 65536), 3, "positive fraction truncates");
}

/* A single 64 KiB line: geometry must stay finite, non-negative, and
 * monotonic — the 32-bit representation overflowed here — and the returned
 * tree must retain memory proportional to the render tree, not to the
 * transient parse (the parser IR is released before compile returns). */
static void txc_test_long_input(txc_test *test) {
    static const char phrase[] = "The quick brown fox \\quad jumps over the lazy dog 0123456789. \\, ";
    size_t phrase_length = sizeof(phrase) - 1;
    size_t repeats = 65536 / phrase_length;
    size_t length = repeats * phrase_length;
    char *source = malloc(length + 1);
    txc_check(test, source != NULL, "long input allocates");
    if (source == NULL) {
        return;
    }
    for (size_t index = 0; index < repeats; index++) {
        memcpy(source + index * phrase_length, phrase, phrase_length);
    }
    source[length] = '\0';

    long before = txc_allocation_outstanding();
    for (int mode = 0; mode < 2; mode++) {
        tex_core_render_tree *tree =
            txc_compile(test, source, mode == 0 ? TEX_CORE_MODE_DOCUMENT : TEX_CORE_MODE_MATH_INLINE, "long document");
        if (tree == NULL) {
            continue;
        }
        const tex_core_node *root = tex_core_render_tree_root(tree);
        tex_core_frame frame = tex_core_node_frame(root);
        txc_check(test, frame.width > 0.0, "long root width is positive");
        double cursor = -1.0;
        bool monotonic = true;
        for (size_t child = 0; child < tex_core_node_child_count(root); child++) {
            double x = tex_core_node_frame(tex_core_node_child(root, child)).x;
            if (x < cursor) {
                monotonic = false;
            }
            cursor = x;
        }
        txc_check(test, monotonic, "long line advances monotonically");
        /* Geometric arena chunks: the live tree needs tens of blocks, not
         * the thousands the fixed 4 KiB refill produced, and none of the
         * parser's scratch blocks may survive compile. */
        long retained = txc_allocation_outstanding() - before;
        txc_check(test, retained > 0 && retained < 64, "long tree retains %ld blocks", retained);
        tex_core_render_tree_free(tree);
        txc_check_int(test, txc_allocation_outstanding() - before, 0, "long tree frees completely");
    }
    free(source);
}

static void txc_test_deep_dump(txc_test *test) {
    /* The public parser deliberately caps syntactic nesting, but render
     * trees are also consumed through internal/native seams. Prove the
     * canonical dumper does not spend one C stack frame per tree level. */
    enum { DEPTH = 4096 };
    txc_node *nodes = calloc(DEPTH, sizeof(*nodes));
    const txc_node **children = calloc(DEPTH - 1, sizeof(*children));
    txc_check(test, nodes != NULL && children != NULL, "deep dump fixture allocation");
    if (nodes == NULL || children == NULL) {
        free(children);
        free(nodes);
        return;
    }
    for (size_t index = 0; index < DEPTH; index++) {
        nodes[index].kind = TEX_CORE_NODE_HBOX;
        if (index + 1 < DEPTH) {
            children[index] = &nodes[index + 1];
            nodes[index].children = &children[index];
            nodes[index].child_count = 1;
        }
    }
    char *dump = NULL;
    size_t length = 0;
    txc_check(test, txc_dump(nodes, &dump, &length) == TEX_CORE_STATUS_OK, "deep dump is stack safe");
    txc_check(test, dump != NULL && length > DEPTH, "deep dump produced every level");
    tex_core_dump_free(dump);
    free(children);
    free(nodes);
}

static void txc_test_text_face_protection(txc_test *test) {
    /* A bare \text character keeps upright main under an outer style
     * switch, like braced \text content. */
    tex_core_render_tree *tree = txc_compile(test, "\\mathbf{\\text a}", TEX_CORE_MODE_MATH_INLINE, "bf text");
    const tex_core_node *box = tex_core_node_child(tex_core_render_tree_root(tree), 0);
    txc_check_int(
        test,
        tex_core_node_glyph(tex_core_node_child(box, 0)).family,
        TEX_CORE_FAMILY_MAIN,
        "bare text stays main under \\mathbf"
    );
    tex_core_render_tree_free(tree);
}

/* Exact points for an internal scaled-point value; published measures
 * are exact integer-sp multiples, so equality is exact. */
static double txc_scaled_as_points(long value) { return (double)value / 65536.0; }

static void txc_test_environments(txc_test *test) {
    /* A 2x2 matrix: both rows take the \@arstrut floor (550500 over
     * 235932 sp — .7/.3 of the 12 pt \baselineskip by TeX's decimal
     * scan), the baselines sit \baselineskip apart, and the stack
     * centers on the 2.5 pt axis: 14.5 pt over 9.5 pt. */
    tex_core_render_tree *tree =
        txc_compile(test, "\\begin{matrix}a&b\\\\c&d\\end{matrix}", TEX_CORE_MODE_MATH_INLINE, "matrix");
    const tex_core_node *root = tex_core_render_tree_root(tree);
    txc_check_size(test, tex_core_node_child_count(root), 1, "matrix is one atom");
    const tex_core_node *matrix = tex_core_node_child(root, 0);
    txc_check_size(test, tex_core_node_child_count(matrix), 2, "matrix has two rows");
    tex_core_frame box = tex_core_node_frame(matrix);
    txc_check(test, box.ascent == txc_scaled_as_points(950272), "matrix ascent is axis-centered");
    txc_check(test, box.descent == txc_scaled_as_points(622592), "matrix descent is axis-centered");

    const tex_core_node *first = tex_core_node_child(matrix, 0);
    const tex_core_node *second = tex_core_node_child(matrix, 1);
    tex_core_frame first_frame = tex_core_node_frame(first);
    tex_core_frame second_frame = tex_core_node_frame(second);
    txc_check(test, first_frame.ascent == txc_scaled_as_points(550500), "row ascent floors at the strut");
    txc_check(test, first_frame.descent == txc_scaled_as_points(235932), "row descent floors at the strut");
    txc_check(
        test,
        first_frame.y - second_frame.y == txc_scaled_as_points(786432),
        "row baselines sit \\baselineskip apart"
    );
    txc_check(test, first_frame.width == second_frame.width, "rows share the alignment width");

    /* Columns take their widest cell (a over c, d over b) separated by
     * 2\arraycolsep; the narrower cell centers by half the excess,
     * rounding up. */
    tex_core_frame cell_a = tex_core_node_frame(tex_core_node_child(first, 0));
    tex_core_frame cell_c = tex_core_node_frame(tex_core_node_child(second, 0));
    tex_core_frame cell_d = tex_core_node_frame(tex_core_node_child(second, 1));
    txc_check(test, cell_a.x == 0.0, "widest first-column cell sits flush");
    txc_check(
        test,
        cell_d.x == cell_a.width + txc_scaled_as_points(655360),
        "second column starts 2\\arraycolsep after the first"
    );
    double centering = 2.0 * cell_c.x - (cell_a.width - cell_c.width);
    txc_check(
        test,
        centering == 0.0 || centering == txc_scaled_as_points(1),
        "narrower cell centers by half the excess rounding up"
    );
    tex_core_render_tree_free(tree);

    /* The delimited variants wrap the same stack in the rule-19 fences:
     * the one-row pmatrix needs the Size1 parenthesis. Child order is
     * source order — left fence, rows, right fence. */
    tree = txc_compile(test, "\\begin{pmatrix}x\\end{pmatrix}", TEX_CORE_MODE_MATH_INLINE, "pmatrix");
    root = tex_core_render_tree_root(tree);
    const tex_core_node *fenced = tex_core_node_child(root, 0);
    txc_check_size(test, tex_core_node_child_count(fenced), 3, "pmatrix has fences around one row");
    const tex_core_node *left = tex_core_node_child(fenced, 0);
    const tex_core_node *right = tex_core_node_child(fenced, 2);
    tex_core_glyph left_view = tex_core_node_glyph(left);
    txc_check_int(test, (long long)left_view.codepoint, '(', "pmatrix left parenthesis");
    txc_check_int(test, left_view.family, TEX_CORE_FAMILY_SIZE1, "one-row pmatrix takes the Size1 face");
    tex_core_frame row = tex_core_node_frame(tex_core_node_child(fenced, 1));
    tex_core_frame left_frame = tex_core_node_frame(left);
    tex_core_frame right_frame = tex_core_node_frame(right);
    txc_check(test, row.x == left_frame.width, "row sits after the left fence");
    txc_check(test, right_frame.x == left_frame.width + row.width, "right fence sits after the row");
    tex_core_render_tree_free(tree);

    /* matrix is an Ord atom, the delimited variants are Inner: only the
     * latter takes the Ord-Inner thin space after a letter. */
    tree = txc_compile(test, "x\\begin{matrix}a\\end{matrix}", TEX_CORE_MODE_MATH_INLINE, "ord matrix");
    txc_check_size(
        test,
        tex_core_node_child_count(tex_core_render_tree_root(tree)),
        2,
        "no space before an undelimited matrix"
    );
    tex_core_render_tree_free(tree);
    tree = txc_compile(test, "x\\begin{pmatrix}a\\end{pmatrix}", TEX_CORE_MODE_MATH_INLINE, "inner matrix");
    txc_check_size(
        test,
        tex_core_node_child_count(tex_core_render_tree_root(tree)),
        3,
        "thin space before a delimited matrix"
    );
    tex_core_render_tree_free(tree);

    /* A trailing \\ contributes no row, and an all-blank body has zero
     * rows: the empty \vcenter splits the zero stack around the axis. */
    tree = txc_compile(test, "\\begin{matrix}a\\\\\\end{matrix}", TEX_CORE_MODE_MATH_INLINE, "trailing row");
    txc_check_size(
        test,
        tex_core_node_child_count(tex_core_node_child(tex_core_render_tree_root(tree), 0)),
        1,
        "trailing \\\\ drops the empty last row"
    );
    tex_core_render_tree_free(tree);
    tree = txc_compile(test, "\\begin{matrix} \\end{matrix}", TEX_CORE_MODE_MATH_INLINE, "empty matrix");
    matrix = tex_core_node_child(tex_core_render_tree_root(tree), 0);
    txc_check_size(test, tex_core_node_child_count(matrix), 0, "blank body has zero rows");
    box = tex_core_node_frame(matrix);
    txc_check(test, box.width == 0.0, "empty matrix has zero width");
    txc_check(test, box.ascent == txc_scaled_as_points(163840), "empty matrix ascent is the axis");
    txc_check(test, box.descent == -txc_scaled_as_points(163840), "empty matrix descent is minus the axis");
    tex_core_render_tree_free(tree);

    /* LaTeX's \\* no-page-break terminator is consumed — meaningless
     * before pagination — so the starred form keeps the plain form's
     * two rows and publishes no asterisk glyph. */
    tree = txc_compile(test, "\\begin{matrix}a\\\\*b\\end{matrix}", TEX_CORE_MODE_MATH_INLINE, "starred");
    matrix = tex_core_node_child(tex_core_render_tree_root(tree), 0);
    txc_check_size(test, tex_core_node_child_count(matrix), 2, "starred terminator keeps two rows");
    const tex_core_node *starred_cell = tex_core_node_child(tex_core_node_child(matrix, 1), 0);
    txc_check_int(
        test,
        (long long)tex_core_node_glyph(tex_core_node_child(starred_cell, 0)).codepoint,
        'b',
        "the star is consumed, not typeset"
    );
    tex_core_render_tree_free(tree);

    /* Ragged rows keep their own cell counts; the alignment width still
     * covers every column. */
    tree = txc_compile(test, "\\begin{matrix}a&b\\\\c\\end{matrix}", TEX_CORE_MODE_MATH_INLINE, "ragged");
    matrix = tex_core_node_child(tex_core_render_tree_root(tree), 0);
    txc_check_size(test, tex_core_node_child_count(tex_core_node_child(matrix, 0)), 2, "full row keeps two cells");
    txc_check_size(test, tex_core_node_child_count(tex_core_node_child(matrix, 1)), 1, "short row keeps one cell");
    txc_check(
        test,
        tex_core_node_frame(tex_core_node_child(matrix, 0)).width ==
            tex_core_node_frame(tex_core_node_child(matrix, 1)).width,
        "short row spans the alignment width"
    );
    tex_core_render_tree_free(tree);
}

static void txc_test_cases_smallmatrix(txc_test *test) {
    /* cases: \left\lbrace over two left-aligned text-style columns one
     * \quad apart, rows floored by the 1.2-\arraystretch strut (TeX's
     * factor arithmetic exactly: 660598 over 283117 sp), closed by the
     * null right delimiter's 1.2 pt kern. */
    tex_core_render_tree *tree =
        txc_compile(test, "\\begin{cases}x&a\\\\-x&b\\end{cases}", TEX_CORE_MODE_MATH_INLINE, "cases");
    const tex_core_node *fenced = tex_core_node_child(tex_core_render_tree_root(tree), 0);
    txc_check_size(test, tex_core_node_child_count(fenced), 4, "cases holds brace, two rows, null kern");
    const tex_core_node *first = tex_core_node_child(fenced, 1);
    const tex_core_node *second = tex_core_node_child(fenced, 2);
    txc_check(
        test,
        tex_core_node_frame(first).ascent == txc_scaled_as_points(660598),
        "cases rows floor at the 1.2 strut ascent"
    );
    txc_check(
        test,
        tex_core_node_frame(first).descent == txc_scaled_as_points(283117),
        "cases rows floor at the 1.2 strut descent"
    );
    tex_core_frame narrow = tex_core_node_frame(tex_core_node_child(first, 0));
    tex_core_frame wide = tex_core_node_frame(tex_core_node_child(second, 0));
    txc_check(test, narrow.x == 0.0 && wide.x == 0.0, "l columns set cells flush left");
    txc_check(test, wide.width > narrow.width, "the flush cells differ in width");
    txc_check(
        test,
        tex_core_node_frame(tex_core_node_child(first, 1)).x == wide.width + txc_scaled_as_points(655360),
        "second column sits one \\quad after the first"
    );
    const tex_core_node *null_right = tex_core_node_child(fenced, 3);
    txc_check_int(test, tex_core_node_get_kind(null_right), TEX_CORE_NODE_KERN, "the null right delimiter is a kern");
    txc_check(
        test,
        tex_core_node_frame(null_right).width == txc_scaled_as_points(78643),
        "the null right kern is \\nulldelimiterspace"
    );
    tex_core_render_tree_free(tree);

    /* cases' local \def\arraystretch{1.2} scopes over its whole body:
     * a matrix nested inside builds the stretched strut too, while the
     * same matrix outside keeps the base floors. */
    tree = txc_compile(
        test,
        "\\begin{cases}\\begin{matrix}a\\\\b\\end{matrix}&x\\end{cases}",
        TEX_CORE_MODE_MATH_INLINE,
        "stretched nesting"
    );
    const tex_core_node *outer_row = tex_core_node_child(tex_core_node_child(tex_core_render_tree_root(tree), 0), 1);
    const tex_core_node *inner_matrix = tex_core_node_child(tex_core_node_child(outer_row, 0), 0);
    txc_check(
        test,
        tex_core_node_frame(tex_core_node_child(inner_matrix, 0)).ascent == txc_scaled_as_points(660598),
        "a matrix nested in cases inherits the stretched strut"
    );
    tex_core_render_tree_free(tree);

    /* smallmatrix: script-size centered cells, real interline glue
     * (6\ex@ baselineskip, 1.5\ex@ lineskip and limit, \ex@ exactly
     * 1 pt at the 10 pt size), no strut, and the flanking \, folded
     * into the box edges. */
    tree = txc_compile(test, "\\begin{smallmatrix}a\\\\c\\end{smallmatrix}", TEX_CORE_MODE_MATH_INLINE, "small");
    const tex_core_node *matrix = tex_core_node_child(tex_core_render_tree_root(tree), 0);
    txc_check_size(test, tex_core_node_child_count(matrix), 2, "smallmatrix keeps its two rows");
    const tex_core_node *row = tex_core_node_child(matrix, 0);
    tex_core_frame row_frame = tex_core_node_frame(row);
    txc_check(
        test,
        tex_core_node_glyph(tex_core_node_child(tex_core_node_child(row, 0), 0)).size == 7.0,
        "smallmatrix cells are script size"
    );
    txc_check(
        test,
        row_frame.y - tex_core_node_frame(tex_core_node_child(matrix, 1)).y == txc_scaled_as_points(393216),
        "short rows pitch at the 6 pt baselineskip"
    );
    txc_check(test, row_frame.x > 0.0, "the flanking thin space pads the rows");
    txc_check(
        test,
        tex_core_node_frame(matrix).width == row_frame.width + 2.0 * row_frame.x,
        "the box width carries both pads"
    );
    txc_check(
        test,
        tex_core_node_frame(row).ascent == tex_core_node_frame(tex_core_node_child(row, 0)).ascent,
        "smallmatrix rows have no strut floor"
    );
    tex_core_render_tree_free(tree);

    /* A row taller than the 6 pt pitch falls back to abutting extents
     * plus the 1.5 pt lineskip. */
    tree = txc_compile(test, "\\begin{smallmatrix}a&b\\\\c&d\\end{smallmatrix}", TEX_CORE_MODE_MATH_INLINE, "glue");
    matrix = tex_core_node_child(tex_core_render_tree_root(tree), 0);
    first = tex_core_node_child(matrix, 0);
    second = tex_core_node_child(matrix, 1);
    txc_check(
        test,
        tex_core_node_frame(first).y - tex_core_node_frame(second).y ==
            tex_core_node_frame(first).descent + tex_core_node_frame(second).ascent + txc_scaled_as_points(98304),
        "tall small rows abut plus \\lineskip"
    );
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
    txc_test_radicals(&test);
    txc_test_operators(&test);
    txc_test_accents(&test);
    txc_test_styles(&test);
    txc_test_text_face_protection(&test);
    txc_test_nested_styles(&test);
    txc_test_environments(&test);
    txc_test_cases_smallmatrix(&test);
    txc_test_command_table(&test);
    txc_test_scaled_rounding(&test);
    txc_test_long_input(&test);
    txc_test_deep_dump(&test);
    return txc_test_finish(&test, "engine");
}
