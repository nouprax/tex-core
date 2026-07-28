#include "parse.h"

#include <stdio.h>
#include <string.h>

#include "error.h"
#include "token.h"

/* Characters with special TeX meaning that the engine does not cover yet.
 * Rejecting them keeps the supported surface honest: nothing is silently
 * skipped or demoted to a literal (plan section 5.1). The math modes carve
 * `{ } ^ _` out of this set as live syntax; document mode keeps rejecting
 * all of them until the text surface (milestone M3) arrives. */
static const char TXC_RESERVED[] = "#$%&^_{}~";

static bool txc_reserved(uint32_t codepoint) {
    return codepoint < 0x80 && strchr(TXC_RESERVED, (int)codepoint) != NULL;
}

static bool txc_letter_codepoint(uint32_t codepoint) {
    return (codepoint >= 'A' && codepoint <= 'Z') || (codepoint >= 'a' && codepoint <= 'z');
}

static bool txc_digit_codepoint(uint32_t codepoint) { return codepoint >= '0' && codepoint <= '9'; }

/* Math character atoms beyond letters, digits, and the period: the source
 * character, its TeX atom class, and the codepoint it renders as. All
 * entries take the upright main face; the remapped codepoints follow the
 * plain TeX mathcode assignments (`-` is the minus sign, `*` the asterisk
 * operator, `|` the divides bar). */
typedef struct txc_math_character {
    uint32_t character;
    txc_atom_class atom_class;
    uint32_t codepoint;
} txc_math_character;

static const txc_math_character TXC_MATH_CHARACTERS[] = {
    {'!', TXC_ATOM_CLOSE, 0x0021},
    {'(', TXC_ATOM_OPEN, 0x0028},
    {')', TXC_ATOM_CLOSE, 0x0029},
    {'*', TXC_ATOM_BIN, 0x2217},
    {'+', TXC_ATOM_BIN, 0x002B},
    {',', TXC_ATOM_PUNCT, 0x002C},
    {'-', TXC_ATOM_BIN, 0x2212},
    {'/', TXC_ATOM_ORD, 0x002F},
    {':', TXC_ATOM_REL, 0x003A},
    {';', TXC_ATOM_PUNCT, 0x003B},
    {'<', TXC_ATOM_REL, 0x003C},
    {'=', TXC_ATOM_REL, 0x003D},
    {'>', TXC_ATOM_REL, 0x003E},
    {'?', TXC_ATOM_CLOSE, 0x003F},
    {'[', TXC_ATOM_OPEN, 0x005B},
    {']', TXC_ATOM_CLOSE, 0x005D},
    {'|', TXC_ATOM_ORD, 0x2223},
};

static const txc_math_character *txc_math_character_find(uint32_t codepoint) {
    for (size_t index = 0; index < sizeof(TXC_MATH_CHARACTERS) / sizeof(TXC_MATH_CHARACTERS[0]); index++) {
        if (TXC_MATH_CHARACTERS[index].character == codepoint) {
            return &TXC_MATH_CHARACTERS[index];
        }
    }
    return NULL;
}

/* Math symbol commands (milestone M1): each maps to one glyph atom. Classes
 * follow plain TeX; the codepoints and faces follow the embedded KaTeX
 * Computer Modern metrics (lowercase Greek on the math italic face,
 * everything else upright). Aliases (\le, \to, \gets, \lnot, \land, \lor,
 * \owns) share their target's row values. */
typedef struct txc_math_symbol {
    const char *name;
    txc_atom_class atom_class;
    uint32_t codepoint;
    tex_core_style style;
} txc_math_symbol;

static const txc_math_symbol TXC_MATH_SYMBOLS[] = {
    /* Lowercase Greek. */
    {"alpha", TXC_ATOM_ORD, 0x03B1, TEX_CORE_STYLE_ITALIC},
    {"beta", TXC_ATOM_ORD, 0x03B2, TEX_CORE_STYLE_ITALIC},
    {"gamma", TXC_ATOM_ORD, 0x03B3, TEX_CORE_STYLE_ITALIC},
    {"delta", TXC_ATOM_ORD, 0x03B4, TEX_CORE_STYLE_ITALIC},
    {"epsilon", TXC_ATOM_ORD, 0x03F5, TEX_CORE_STYLE_ITALIC},
    {"varepsilon", TXC_ATOM_ORD, 0x03B5, TEX_CORE_STYLE_ITALIC},
    {"zeta", TXC_ATOM_ORD, 0x03B6, TEX_CORE_STYLE_ITALIC},
    {"eta", TXC_ATOM_ORD, 0x03B7, TEX_CORE_STYLE_ITALIC},
    {"theta", TXC_ATOM_ORD, 0x03B8, TEX_CORE_STYLE_ITALIC},
    {"vartheta", TXC_ATOM_ORD, 0x03D1, TEX_CORE_STYLE_ITALIC},
    {"iota", TXC_ATOM_ORD, 0x03B9, TEX_CORE_STYLE_ITALIC},
    {"kappa", TXC_ATOM_ORD, 0x03BA, TEX_CORE_STYLE_ITALIC},
    {"lambda", TXC_ATOM_ORD, 0x03BB, TEX_CORE_STYLE_ITALIC},
    {"mu", TXC_ATOM_ORD, 0x03BC, TEX_CORE_STYLE_ITALIC},
    {"nu", TXC_ATOM_ORD, 0x03BD, TEX_CORE_STYLE_ITALIC},
    {"xi", TXC_ATOM_ORD, 0x03BE, TEX_CORE_STYLE_ITALIC},
    {"pi", TXC_ATOM_ORD, 0x03C0, TEX_CORE_STYLE_ITALIC},
    {"varpi", TXC_ATOM_ORD, 0x03D6, TEX_CORE_STYLE_ITALIC},
    {"rho", TXC_ATOM_ORD, 0x03C1, TEX_CORE_STYLE_ITALIC},
    {"varrho", TXC_ATOM_ORD, 0x03F1, TEX_CORE_STYLE_ITALIC},
    {"sigma", TXC_ATOM_ORD, 0x03C3, TEX_CORE_STYLE_ITALIC},
    {"varsigma", TXC_ATOM_ORD, 0x03C2, TEX_CORE_STYLE_ITALIC},
    {"tau", TXC_ATOM_ORD, 0x03C4, TEX_CORE_STYLE_ITALIC},
    {"upsilon", TXC_ATOM_ORD, 0x03C5, TEX_CORE_STYLE_ITALIC},
    {"phi", TXC_ATOM_ORD, 0x03D5, TEX_CORE_STYLE_ITALIC},
    {"varphi", TXC_ATOM_ORD, 0x03C6, TEX_CORE_STYLE_ITALIC},
    {"chi", TXC_ATOM_ORD, 0x03C7, TEX_CORE_STYLE_ITALIC},
    {"psi", TXC_ATOM_ORD, 0x03C8, TEX_CORE_STYLE_ITALIC},
    {"omega", TXC_ATOM_ORD, 0x03C9, TEX_CORE_STYLE_ITALIC},
    /* Uppercase Greek (upright, the plain TeX default). */
    {"Gamma", TXC_ATOM_ORD, 0x0393, TEX_CORE_STYLE_UPRIGHT},
    {"Delta", TXC_ATOM_ORD, 0x0394, TEX_CORE_STYLE_UPRIGHT},
    {"Theta", TXC_ATOM_ORD, 0x0398, TEX_CORE_STYLE_UPRIGHT},
    {"Lambda", TXC_ATOM_ORD, 0x039B, TEX_CORE_STYLE_UPRIGHT},
    {"Xi", TXC_ATOM_ORD, 0x039E, TEX_CORE_STYLE_UPRIGHT},
    {"Pi", TXC_ATOM_ORD, 0x03A0, TEX_CORE_STYLE_UPRIGHT},
    {"Sigma", TXC_ATOM_ORD, 0x03A3, TEX_CORE_STYLE_UPRIGHT},
    {"Upsilon", TXC_ATOM_ORD, 0x03A5, TEX_CORE_STYLE_UPRIGHT},
    {"Phi", TXC_ATOM_ORD, 0x03A6, TEX_CORE_STYLE_UPRIGHT},
    {"Psi", TXC_ATOM_ORD, 0x03A8, TEX_CORE_STYLE_UPRIGHT},
    {"Omega", TXC_ATOM_ORD, 0x03A9, TEX_CORE_STYLE_UPRIGHT},
    /* Letter-like and miscellaneous ordinaries. */
    {"ell", TXC_ATOM_ORD, 0x2113, TEX_CORE_STYLE_UPRIGHT},
    {"wp", TXC_ATOM_ORD, 0x2118, TEX_CORE_STYLE_UPRIGHT},
    {"Re", TXC_ATOM_ORD, 0x211C, TEX_CORE_STYLE_UPRIGHT},
    {"Im", TXC_ATOM_ORD, 0x2111, TEX_CORE_STYLE_UPRIGHT},
    {"aleph", TXC_ATOM_ORD, 0x2135, TEX_CORE_STYLE_UPRIGHT},
    {"partial", TXC_ATOM_ORD, 0x2202, TEX_CORE_STYLE_UPRIGHT},
    {"infty", TXC_ATOM_ORD, 0x221E, TEX_CORE_STYLE_UPRIGHT},
    {"prime", TXC_ATOM_ORD, 0x2032, TEX_CORE_STYLE_UPRIGHT},
    {"emptyset", TXC_ATOM_ORD, 0x2205, TEX_CORE_STYLE_UPRIGHT},
    {"nabla", TXC_ATOM_ORD, 0x2207, TEX_CORE_STYLE_UPRIGHT},
    {"surd", TXC_ATOM_ORD, 0x221A, TEX_CORE_STYLE_UPRIGHT},
    {"top", TXC_ATOM_ORD, 0x22A4, TEX_CORE_STYLE_UPRIGHT},
    {"bot", TXC_ATOM_ORD, 0x22A5, TEX_CORE_STYLE_UPRIGHT},
    {"angle", TXC_ATOM_ORD, 0x2220, TEX_CORE_STYLE_UPRIGHT},
    {"triangle", TXC_ATOM_ORD, 0x25B3, TEX_CORE_STYLE_UPRIGHT},
    {"forall", TXC_ATOM_ORD, 0x2200, TEX_CORE_STYLE_UPRIGHT},
    {"exists", TXC_ATOM_ORD, 0x2203, TEX_CORE_STYLE_UPRIGHT},
    {"neg", TXC_ATOM_ORD, 0x00AC, TEX_CORE_STYLE_UPRIGHT},
    {"lnot", TXC_ATOM_ORD, 0x00AC, TEX_CORE_STYLE_UPRIGHT},
    {"flat", TXC_ATOM_ORD, 0x266D, TEX_CORE_STYLE_UPRIGHT},
    {"natural", TXC_ATOM_ORD, 0x266E, TEX_CORE_STYLE_UPRIGHT},
    {"sharp", TXC_ATOM_ORD, 0x266F, TEX_CORE_STYLE_UPRIGHT},
    {"clubsuit", TXC_ATOM_ORD, 0x2663, TEX_CORE_STYLE_UPRIGHT},
    {"diamondsuit", TXC_ATOM_ORD, 0x2662, TEX_CORE_STYLE_UPRIGHT},
    {"heartsuit", TXC_ATOM_ORD, 0x2661, TEX_CORE_STYLE_UPRIGHT},
    {"spadesuit", TXC_ATOM_ORD, 0x2660, TEX_CORE_STYLE_UPRIGHT},
    {"backslash", TXC_ATOM_ORD, 0x005C, TEX_CORE_STYLE_UPRIGHT},
    {"vert", TXC_ATOM_ORD, 0x2223, TEX_CORE_STYLE_UPRIGHT},
    {"Vert", TXC_ATOM_ORD, 0x2225, TEX_CORE_STYLE_UPRIGHT},
    {"|", TXC_ATOM_ORD, 0x2225, TEX_CORE_STYLE_UPRIGHT},
    /* Binary operators. */
    {"pm", TXC_ATOM_BIN, 0x00B1, TEX_CORE_STYLE_UPRIGHT},
    {"mp", TXC_ATOM_BIN, 0x2213, TEX_CORE_STYLE_UPRIGHT},
    {"times", TXC_ATOM_BIN, 0x00D7, TEX_CORE_STYLE_UPRIGHT},
    {"div", TXC_ATOM_BIN, 0x00F7, TEX_CORE_STYLE_UPRIGHT},
    {"cdot", TXC_ATOM_BIN, 0x22C5, TEX_CORE_STYLE_UPRIGHT},
    {"ast", TXC_ATOM_BIN, 0x2217, TEX_CORE_STYLE_UPRIGHT},
    {"star", TXC_ATOM_BIN, 0x22C6, TEX_CORE_STYLE_UPRIGHT},
    {"circ", TXC_ATOM_BIN, 0x2218, TEX_CORE_STYLE_UPRIGHT},
    {"bullet", TXC_ATOM_BIN, 0x2219, TEX_CORE_STYLE_UPRIGHT},
    {"cap", TXC_ATOM_BIN, 0x2229, TEX_CORE_STYLE_UPRIGHT},
    {"cup", TXC_ATOM_BIN, 0x222A, TEX_CORE_STYLE_UPRIGHT},
    {"sqcap", TXC_ATOM_BIN, 0x2293, TEX_CORE_STYLE_UPRIGHT},
    {"sqcup", TXC_ATOM_BIN, 0x2294, TEX_CORE_STYLE_UPRIGHT},
    {"uplus", TXC_ATOM_BIN, 0x228E, TEX_CORE_STYLE_UPRIGHT},
    {"vee", TXC_ATOM_BIN, 0x2228, TEX_CORE_STYLE_UPRIGHT},
    {"lor", TXC_ATOM_BIN, 0x2228, TEX_CORE_STYLE_UPRIGHT},
    {"wedge", TXC_ATOM_BIN, 0x2227, TEX_CORE_STYLE_UPRIGHT},
    {"land", TXC_ATOM_BIN, 0x2227, TEX_CORE_STYLE_UPRIGHT},
    {"setminus", TXC_ATOM_BIN, 0x2216, TEX_CORE_STYLE_UPRIGHT},
    {"wr", TXC_ATOM_BIN, 0x2240, TEX_CORE_STYLE_UPRIGHT},
    {"diamond", TXC_ATOM_BIN, 0x22C4, TEX_CORE_STYLE_UPRIGHT},
    {"bigtriangleup", TXC_ATOM_BIN, 0x25B3, TEX_CORE_STYLE_UPRIGHT},
    {"bigtriangledown", TXC_ATOM_BIN, 0x25BD, TEX_CORE_STYLE_UPRIGHT},
    {"triangleleft", TXC_ATOM_BIN, 0x25C3, TEX_CORE_STYLE_UPRIGHT},
    {"triangleright", TXC_ATOM_BIN, 0x25B9, TEX_CORE_STYLE_UPRIGHT},
    {"oplus", TXC_ATOM_BIN, 0x2295, TEX_CORE_STYLE_UPRIGHT},
    {"ominus", TXC_ATOM_BIN, 0x2296, TEX_CORE_STYLE_UPRIGHT},
    {"otimes", TXC_ATOM_BIN, 0x2297, TEX_CORE_STYLE_UPRIGHT},
    {"oslash", TXC_ATOM_BIN, 0x2298, TEX_CORE_STYLE_UPRIGHT},
    {"odot", TXC_ATOM_BIN, 0x2299, TEX_CORE_STYLE_UPRIGHT},
    {"dagger", TXC_ATOM_BIN, 0x2020, TEX_CORE_STYLE_UPRIGHT},
    {"ddagger", TXC_ATOM_BIN, 0x2021, TEX_CORE_STYLE_UPRIGHT},
    {"amalg", TXC_ATOM_BIN, 0x2A3F, TEX_CORE_STYLE_UPRIGHT},
    /* Relations. */
    {"leq", TXC_ATOM_REL, 0x2264, TEX_CORE_STYLE_UPRIGHT},
    {"le", TXC_ATOM_REL, 0x2264, TEX_CORE_STYLE_UPRIGHT},
    {"geq", TXC_ATOM_REL, 0x2265, TEX_CORE_STYLE_UPRIGHT},
    {"ge", TXC_ATOM_REL, 0x2265, TEX_CORE_STYLE_UPRIGHT},
    {"equiv", TXC_ATOM_REL, 0x2261, TEX_CORE_STYLE_UPRIGHT},
    {"prec", TXC_ATOM_REL, 0x227A, TEX_CORE_STYLE_UPRIGHT},
    {"succ", TXC_ATOM_REL, 0x227B, TEX_CORE_STYLE_UPRIGHT},
    {"preceq", TXC_ATOM_REL, 0x2AAF, TEX_CORE_STYLE_UPRIGHT},
    {"succeq", TXC_ATOM_REL, 0x2AB0, TEX_CORE_STYLE_UPRIGHT},
    {"ll", TXC_ATOM_REL, 0x226A, TEX_CORE_STYLE_UPRIGHT},
    {"gg", TXC_ATOM_REL, 0x226B, TEX_CORE_STYLE_UPRIGHT},
    {"subset", TXC_ATOM_REL, 0x2282, TEX_CORE_STYLE_UPRIGHT},
    {"supset", TXC_ATOM_REL, 0x2283, TEX_CORE_STYLE_UPRIGHT},
    {"subseteq", TXC_ATOM_REL, 0x2286, TEX_CORE_STYLE_UPRIGHT},
    {"supseteq", TXC_ATOM_REL, 0x2287, TEX_CORE_STYLE_UPRIGHT},
    {"sqsubseteq", TXC_ATOM_REL, 0x2291, TEX_CORE_STYLE_UPRIGHT},
    {"sqsupseteq", TXC_ATOM_REL, 0x2292, TEX_CORE_STYLE_UPRIGHT},
    {"in", TXC_ATOM_REL, 0x2208, TEX_CORE_STYLE_UPRIGHT},
    {"ni", TXC_ATOM_REL, 0x220B, TEX_CORE_STYLE_UPRIGHT},
    {"owns", TXC_ATOM_REL, 0x220B, TEX_CORE_STYLE_UPRIGHT},
    {"vdash", TXC_ATOM_REL, 0x22A2, TEX_CORE_STYLE_UPRIGHT},
    {"dashv", TXC_ATOM_REL, 0x22A3, TEX_CORE_STYLE_UPRIGHT},
    {"smile", TXC_ATOM_REL, 0x2323, TEX_CORE_STYLE_UPRIGHT},
    {"frown", TXC_ATOM_REL, 0x2322, TEX_CORE_STYLE_UPRIGHT},
    {"mid", TXC_ATOM_REL, 0x2223, TEX_CORE_STYLE_UPRIGHT},
    {"parallel", TXC_ATOM_REL, 0x2225, TEX_CORE_STYLE_UPRIGHT},
    {"perp", TXC_ATOM_REL, 0x22A5, TEX_CORE_STYLE_UPRIGHT},
    {"models", TXC_ATOM_REL, 0x22A8, TEX_CORE_STYLE_UPRIGHT},
    {"asymp", TXC_ATOM_REL, 0x224D, TEX_CORE_STYLE_UPRIGHT},
    {"sim", TXC_ATOM_REL, 0x223C, TEX_CORE_STYLE_UPRIGHT},
    {"simeq", TXC_ATOM_REL, 0x2243, TEX_CORE_STYLE_UPRIGHT},
    {"approx", TXC_ATOM_REL, 0x2248, TEX_CORE_STYLE_UPRIGHT},
    {"cong", TXC_ATOM_REL, 0x2245, TEX_CORE_STYLE_UPRIGHT},
    {"doteq", TXC_ATOM_REL, 0x2250, TEX_CORE_STYLE_UPRIGHT},
    {"propto", TXC_ATOM_REL, 0x221D, TEX_CORE_STYLE_UPRIGHT},
    {"bowtie", TXC_ATOM_REL, 0x22C8, TEX_CORE_STYLE_UPRIGHT},
    /* Arrows (relations). */
    {"leftarrow", TXC_ATOM_REL, 0x2190, TEX_CORE_STYLE_UPRIGHT},
    {"gets", TXC_ATOM_REL, 0x2190, TEX_CORE_STYLE_UPRIGHT},
    {"rightarrow", TXC_ATOM_REL, 0x2192, TEX_CORE_STYLE_UPRIGHT},
    {"to", TXC_ATOM_REL, 0x2192, TEX_CORE_STYLE_UPRIGHT},
    {"leftrightarrow", TXC_ATOM_REL, 0x2194, TEX_CORE_STYLE_UPRIGHT},
    {"Leftarrow", TXC_ATOM_REL, 0x21D0, TEX_CORE_STYLE_UPRIGHT},
    {"Rightarrow", TXC_ATOM_REL, 0x21D2, TEX_CORE_STYLE_UPRIGHT},
    {"Leftrightarrow", TXC_ATOM_REL, 0x21D4, TEX_CORE_STYLE_UPRIGHT},
    {"longleftarrow", TXC_ATOM_REL, 0x27F5, TEX_CORE_STYLE_UPRIGHT},
    {"longrightarrow", TXC_ATOM_REL, 0x27F6, TEX_CORE_STYLE_UPRIGHT},
    {"longleftrightarrow", TXC_ATOM_REL, 0x27F7, TEX_CORE_STYLE_UPRIGHT},
    {"Longleftarrow", TXC_ATOM_REL, 0x27F8, TEX_CORE_STYLE_UPRIGHT},
    {"Longrightarrow", TXC_ATOM_REL, 0x27F9, TEX_CORE_STYLE_UPRIGHT},
    {"Longleftrightarrow", TXC_ATOM_REL, 0x27FA, TEX_CORE_STYLE_UPRIGHT},
    {"mapsto", TXC_ATOM_REL, 0x21A6, TEX_CORE_STYLE_UPRIGHT},
    {"longmapsto", TXC_ATOM_REL, 0x27FC, TEX_CORE_STYLE_UPRIGHT},
    {"hookleftarrow", TXC_ATOM_REL, 0x21A9, TEX_CORE_STYLE_UPRIGHT},
    {"hookrightarrow", TXC_ATOM_REL, 0x21AA, TEX_CORE_STYLE_UPRIGHT},
    {"uparrow", TXC_ATOM_REL, 0x2191, TEX_CORE_STYLE_UPRIGHT},
    {"downarrow", TXC_ATOM_REL, 0x2193, TEX_CORE_STYLE_UPRIGHT},
    {"updownarrow", TXC_ATOM_REL, 0x2195, TEX_CORE_STYLE_UPRIGHT},
    {"Uparrow", TXC_ATOM_REL, 0x21D1, TEX_CORE_STYLE_UPRIGHT},
    {"Downarrow", TXC_ATOM_REL, 0x21D3, TEX_CORE_STYLE_UPRIGHT},
    {"Updownarrow", TXC_ATOM_REL, 0x21D5, TEX_CORE_STYLE_UPRIGHT},
    {"nearrow", TXC_ATOM_REL, 0x2197, TEX_CORE_STYLE_UPRIGHT},
    {"searrow", TXC_ATOM_REL, 0x2198, TEX_CORE_STYLE_UPRIGHT},
    {"swarrow", TXC_ATOM_REL, 0x2199, TEX_CORE_STYLE_UPRIGHT},
    {"nwarrow", TXC_ATOM_REL, 0x2196, TEX_CORE_STYLE_UPRIGHT},
    {"leftharpoonup", TXC_ATOM_REL, 0x21BC, TEX_CORE_STYLE_UPRIGHT},
    {"leftharpoondown", TXC_ATOM_REL, 0x21BD, TEX_CORE_STYLE_UPRIGHT},
    {"rightharpoonup", TXC_ATOM_REL, 0x21C0, TEX_CORE_STYLE_UPRIGHT},
    {"rightharpoondown", TXC_ATOM_REL, 0x21C1, TEX_CORE_STYLE_UPRIGHT},
    {"rightleftharpoons", TXC_ATOM_REL, 0x21CC, TEX_CORE_STYLE_UPRIGHT},
    /* Delimiter symbols as plain atoms; \left/\right sizing is a later M1
     * increment. */
    {"lbrace", TXC_ATOM_OPEN, 0x007B, TEX_CORE_STYLE_UPRIGHT},
    {"{", TXC_ATOM_OPEN, 0x007B, TEX_CORE_STYLE_UPRIGHT},
    {"rbrace", TXC_ATOM_CLOSE, 0x007D, TEX_CORE_STYLE_UPRIGHT},
    {"}", TXC_ATOM_CLOSE, 0x007D, TEX_CORE_STYLE_UPRIGHT},
    {"langle", TXC_ATOM_OPEN, 0x27E8, TEX_CORE_STYLE_UPRIGHT},
    {"rangle", TXC_ATOM_CLOSE, 0x27E9, TEX_CORE_STYLE_UPRIGHT},
    {"lfloor", TXC_ATOM_OPEN, 0x230A, TEX_CORE_STYLE_UPRIGHT},
    {"rfloor", TXC_ATOM_CLOSE, 0x230B, TEX_CORE_STYLE_UPRIGHT},
    {"lceil", TXC_ATOM_OPEN, 0x2308, TEX_CORE_STYLE_UPRIGHT},
    {"rceil", TXC_ATOM_CLOSE, 0x2309, TEX_CORE_STYLE_UPRIGHT},
    /* Punctuation. */
    {"colon", TXC_ATOM_PUNCT, 0x003A, TEX_CORE_STYLE_UPRIGHT},
};

/* Fraction commands (milestone M1): \frac follows the surrounding style,
 * \dfrac and \tfrac force display and text style; the \binom family is
 * the barless fraction wrapped in sized parentheses. */
typedef struct txc_fraction_command {
    const char *name;
    txc_fraction_style style;
    bool binom;
} txc_fraction_command;

static const txc_fraction_command TXC_FRACTION_COMMANDS[] = {
    {"binom", TXC_FRACTION_STYLE_AUTO, true},
    {"dbinom", TXC_FRACTION_STYLE_DISPLAY, true},
    {"dfrac", TXC_FRACTION_STYLE_DISPLAY, false},
    {"frac", TXC_FRACTION_STYLE_AUTO, false},
    {"tbinom", TXC_FRACTION_STYLE_TEXT, true},
    {"tfrac", TXC_FRACTION_STYLE_TEXT, false},
};

/* Big operators (milestone M1): Op atoms whose glyph comes from the
 * Size1 face (Size2 in display style). Sums place their limits above
 * and below in display style; integrals never do unless \limits. */
typedef struct txc_big_op {
    const char *name;
    uint32_t codepoint;
    txc_op_limits limits;
} txc_big_op;

static const txc_big_op TXC_BIG_OPS[] = {
    {"bigcap", 0x22C2, TXC_LIMITS_DISPLAY},
    {"bigcup", 0x22C3, TXC_LIMITS_DISPLAY},
    {"bigodot", 0x2A00, TXC_LIMITS_DISPLAY},
    {"bigoplus", 0x2A01, TXC_LIMITS_DISPLAY},
    {"bigotimes", 0x2A02, TXC_LIMITS_DISPLAY},
    {"bigsqcup", 0x2A06, TXC_LIMITS_DISPLAY},
    {"biguplus", 0x2A04, TXC_LIMITS_DISPLAY},
    {"bigvee", 0x22C1, TXC_LIMITS_DISPLAY},
    {"bigwedge", 0x22C0, TXC_LIMITS_DISPLAY},
    {"coprod", 0x2210, TXC_LIMITS_DISPLAY},
    {"int", 0x222B, TXC_LIMITS_NEVER},
    {"oint", 0x222E, TXC_LIMITS_NEVER},
    {"prod", 0x220F, TXC_LIMITS_DISPLAY},
    {"sum", 0x2211, TXC_LIMITS_DISPLAY},
};

/* Function names (milestone M1): Op atoms whose nucleus is the upright
 * letter run; `spelling` may carry one '\'' for the thin space inside
 * \limsup and \liminf. The \lim family takes display limits. */
typedef struct txc_function_name {
    const char *name;
    const char *spelling;
    txc_op_limits limits;
} txc_function_name;

static const txc_function_name TXC_FUNCTION_NAMES[] = {
    {"Pr", "Pr", TXC_LIMITS_DISPLAY},
    {"arccos", "arccos", TXC_LIMITS_NEVER},
    {"arcsin", "arcsin", TXC_LIMITS_NEVER},
    {"arctan", "arctan", TXC_LIMITS_NEVER},
    {"arg", "arg", TXC_LIMITS_NEVER},
    {"cos", "cos", TXC_LIMITS_NEVER},
    {"cosh", "cosh", TXC_LIMITS_NEVER},
    {"cot", "cot", TXC_LIMITS_NEVER},
    {"coth", "coth", TXC_LIMITS_NEVER},
    {"csc", "csc", TXC_LIMITS_NEVER},
    {"deg", "deg", TXC_LIMITS_NEVER},
    {"det", "det", TXC_LIMITS_DISPLAY},
    {"dim", "dim", TXC_LIMITS_NEVER},
    {"exp", "exp", TXC_LIMITS_NEVER},
    {"gcd", "gcd", TXC_LIMITS_DISPLAY},
    {"hom", "hom", TXC_LIMITS_NEVER},
    {"inf", "inf", TXC_LIMITS_DISPLAY},
    {"ker", "ker", TXC_LIMITS_NEVER},
    {"lg", "lg", TXC_LIMITS_NEVER},
    {"lim", "lim", TXC_LIMITS_DISPLAY},
    {"liminf", "lim'inf", TXC_LIMITS_DISPLAY},
    {"limsup", "lim'sup", TXC_LIMITS_DISPLAY},
    {"ln", "ln", TXC_LIMITS_NEVER},
    {"log", "log", TXC_LIMITS_NEVER},
    {"max", "max", TXC_LIMITS_DISPLAY},
    {"min", "min", TXC_LIMITS_DISPLAY},
    {"sec", "sec", TXC_LIMITS_NEVER},
    {"sin", "sin", TXC_LIMITS_NEVER},
    {"sinh", "sinh", TXC_LIMITS_NEVER},
    {"sup", "sup", TXC_LIMITS_DISPLAY},
    {"tan", "tan", TXC_LIMITS_NEVER},
    {"tanh", "tanh", TXC_LIMITS_NEVER},
};

/* Style switches (milestone M1): each takes one argument and rewrites
 * its letter and digit characters onto a face; \text instead sets its
 * braced content by document rules at the current size. */
typedef struct txc_style_command {
    const char *name;
    txc_face face;
} txc_style_command;

static const txc_style_command TXC_STYLE_COMMANDS[] = {
    {"mathbb", TXC_FACE_BB},
    {"mathbf", TXC_FACE_BF},
    {"mathcal", TXC_FACE_CAL},
    {"mathit", TXC_FACE_IT},
    {"mathrm", TXC_FACE_RM},
    {"mathsf", TXC_FACE_SF},
    {"mathtt", TXC_FACE_TT},
    {"text", TXC_FACE_TEXT},
};

static const tex_core_family TXC_FACE_FAMILIES[7] = {
    TEX_CORE_FAMILY_MAIN,
    TEX_CORE_FAMILY_BOLD,
    TEX_CORE_FAMILY_TEXTIT,
    TEX_CORE_FAMILY_CAL,
    TEX_CORE_FAMILY_BB,
    TEX_CORE_FAMILY_SANS,
    TEX_CORE_FAMILY_MONO,
};

/* Math accents (milestone M1): each maps to one accent glyph over the
 * argument; the wide pair grows through the size-face width steps. */
typedef struct txc_accent_command {
    const char *name;
    uint32_t codepoint;
    bool wide;
} txc_accent_command;

static const txc_accent_command TXC_ACCENT_COMMANDS[] = {
    {"acute", 0x02CA, false},
    {"bar", 0x02C9, false},
    {"breve", 0x02D8, false},
    {"check", 0x02C7, false},
    {"ddot", 0x00A8, false},
    {"dot", 0x02D9, false},
    {"grave", 0x02CB, false},
    {"hat", 0x02C6, false},
    {"tilde", 0x02DC, false},
    {"vec", 0x20D7, false},
    {"widehat", 0x02C6, true},
    {"widetilde", 0x02DC, true},
};

/* The math command dispatch: every math-mode control word in one table
 * sorted by strcmp order, resolved with a single binary search instead
 * of one linear scan per command class. `index` is the row within the
 * class's own table. The spacing commands stay outside — they are live
 * in document mode too — and the delimiter spellings resolve only where
 * a delimiter is awaited. */
typedef enum txc_command_kind {
    TXC_COMMAND_SYMBOL = 0,
    TXC_COMMAND_FRACTION = 1,
    TXC_COMMAND_BIG_OP = 2,
    TXC_COMMAND_FUNCTION = 3,
    TXC_COMMAND_STYLE = 4,
    TXC_COMMAND_ACCENT = 5,
    TXC_COMMAND_SIZE = 6,
    TXC_COMMAND_SQRT = 7,
    TXC_COMMAND_LEFT = 8,
    TXC_COMMAND_RIGHT = 9,
    TXC_COMMAND_LIMITS = 10,
    TXC_COMMAND_NOLIMITS = 11,
    TXC_COMMAND_BEGIN = 12,
    TXC_COMMAND_END = 13
} txc_command_kind;

typedef struct txc_command_entry {
    const char *name;
    txc_command_kind kind;
    uint16_t index;
} txc_command_entry;

static const txc_command_entry TXC_COMMANDS[] = {
    {"Big", TXC_COMMAND_SIZE, 0},
    {"Bigg", TXC_COMMAND_SIZE, 1},
    {"Biggl", TXC_COMMAND_SIZE, 2},
    {"Biggm", TXC_COMMAND_SIZE, 3},
    {"Biggr", TXC_COMMAND_SIZE, 4},
    {"Bigl", TXC_COMMAND_SIZE, 5},
    {"Bigm", TXC_COMMAND_SIZE, 6},
    {"Bigr", TXC_COMMAND_SIZE, 7},
    {"Delta", TXC_COMMAND_SYMBOL, 30},
    {"Downarrow", TXC_COMMAND_SYMBOL, 161},
    {"Gamma", TXC_COMMAND_SYMBOL, 29},
    {"Im", TXC_COMMAND_SYMBOL, 43},
    {"Lambda", TXC_COMMAND_SYMBOL, 32},
    {"Leftarrow", TXC_COMMAND_SYMBOL, 144},
    {"Leftrightarrow", TXC_COMMAND_SYMBOL, 146},
    {"Longleftarrow", TXC_COMMAND_SYMBOL, 150},
    {"Longleftrightarrow", TXC_COMMAND_SYMBOL, 152},
    {"Longrightarrow", TXC_COMMAND_SYMBOL, 151},
    {"Omega", TXC_COMMAND_SYMBOL, 39},
    {"Phi", TXC_COMMAND_SYMBOL, 37},
    {"Pi", TXC_COMMAND_SYMBOL, 34},
    {"Pr", TXC_COMMAND_FUNCTION, 0},
    {"Psi", TXC_COMMAND_SYMBOL, 38},
    {"Re", TXC_COMMAND_SYMBOL, 42},
    {"Rightarrow", TXC_COMMAND_SYMBOL, 145},
    {"Sigma", TXC_COMMAND_SYMBOL, 35},
    {"Theta", TXC_COMMAND_SYMBOL, 31},
    {"Uparrow", TXC_COMMAND_SYMBOL, 160},
    {"Updownarrow", TXC_COMMAND_SYMBOL, 162},
    {"Upsilon", TXC_COMMAND_SYMBOL, 36},
    {"Vert", TXC_COMMAND_SYMBOL, 68},
    {"Xi", TXC_COMMAND_SYMBOL, 33},
    {"acute", TXC_COMMAND_ACCENT, 0},
    {"aleph", TXC_COMMAND_SYMBOL, 44},
    {"alpha", TXC_COMMAND_SYMBOL, 0},
    {"amalg", TXC_COMMAND_SYMBOL, 102},
    {"angle", TXC_COMMAND_SYMBOL, 53},
    {"approx", TXC_COMMAND_SYMBOL, 134},
    {"arccos", TXC_COMMAND_FUNCTION, 1},
    {"arcsin", TXC_COMMAND_FUNCTION, 2},
    {"arctan", TXC_COMMAND_FUNCTION, 3},
    {"arg", TXC_COMMAND_FUNCTION, 4},
    {"ast", TXC_COMMAND_SYMBOL, 75},
    {"asymp", TXC_COMMAND_SYMBOL, 131},
    {"backslash", TXC_COMMAND_SYMBOL, 66},
    {"bar", TXC_COMMAND_ACCENT, 1},
    {"begin", TXC_COMMAND_BEGIN, 0},
    {"beta", TXC_COMMAND_SYMBOL, 1},
    {"big", TXC_COMMAND_SIZE, 8},
    {"bigcap", TXC_COMMAND_BIG_OP, 0},
    {"bigcup", TXC_COMMAND_BIG_OP, 1},
    {"bigg", TXC_COMMAND_SIZE, 9},
    {"biggl", TXC_COMMAND_SIZE, 10},
    {"biggm", TXC_COMMAND_SIZE, 11},
    {"biggr", TXC_COMMAND_SIZE, 12},
    {"bigl", TXC_COMMAND_SIZE, 13},
    {"bigm", TXC_COMMAND_SIZE, 14},
    {"bigodot", TXC_COMMAND_BIG_OP, 2},
    {"bigoplus", TXC_COMMAND_BIG_OP, 3},
    {"bigotimes", TXC_COMMAND_BIG_OP, 4},
    {"bigr", TXC_COMMAND_SIZE, 15},
    {"bigsqcup", TXC_COMMAND_BIG_OP, 5},
    {"bigtriangledown", TXC_COMMAND_SYMBOL, 92},
    {"bigtriangleup", TXC_COMMAND_SYMBOL, 91},
    {"biguplus", TXC_COMMAND_BIG_OP, 6},
    {"bigvee", TXC_COMMAND_BIG_OP, 7},
    {"bigwedge", TXC_COMMAND_BIG_OP, 8},
    {"binom", TXC_COMMAND_FRACTION, 0},
    {"bot", TXC_COMMAND_SYMBOL, 52},
    {"bowtie", TXC_COMMAND_SYMBOL, 138},
    {"breve", TXC_COMMAND_ACCENT, 2},
    {"bullet", TXC_COMMAND_SYMBOL, 78},
    {"cap", TXC_COMMAND_SYMBOL, 79},
    {"cdot", TXC_COMMAND_SYMBOL, 74},
    {"check", TXC_COMMAND_ACCENT, 3},
    {"chi", TXC_COMMAND_SYMBOL, 26},
    {"circ", TXC_COMMAND_SYMBOL, 77},
    {"clubsuit", TXC_COMMAND_SYMBOL, 62},
    {"colon", TXC_COMMAND_SYMBOL, 182},
    {"cong", TXC_COMMAND_SYMBOL, 135},
    {"coprod", TXC_COMMAND_BIG_OP, 9},
    {"cos", TXC_COMMAND_FUNCTION, 5},
    {"cosh", TXC_COMMAND_FUNCTION, 6},
    {"cot", TXC_COMMAND_FUNCTION, 7},
    {"coth", TXC_COMMAND_FUNCTION, 8},
    {"csc", TXC_COMMAND_FUNCTION, 9},
    {"cup", TXC_COMMAND_SYMBOL, 80},
    {"dagger", TXC_COMMAND_SYMBOL, 100},
    {"dashv", TXC_COMMAND_SYMBOL, 124},
    {"dbinom", TXC_COMMAND_FRACTION, 1},
    {"ddagger", TXC_COMMAND_SYMBOL, 101},
    {"ddot", TXC_COMMAND_ACCENT, 4},
    {"deg", TXC_COMMAND_FUNCTION, 10},
    {"delta", TXC_COMMAND_SYMBOL, 3},
    {"det", TXC_COMMAND_FUNCTION, 11},
    {"dfrac", TXC_COMMAND_FRACTION, 2},
    {"diamond", TXC_COMMAND_SYMBOL, 90},
    {"diamondsuit", TXC_COMMAND_SYMBOL, 63},
    {"dim", TXC_COMMAND_FUNCTION, 12},
    {"div", TXC_COMMAND_SYMBOL, 73},
    {"dot", TXC_COMMAND_ACCENT, 5},
    {"doteq", TXC_COMMAND_SYMBOL, 136},
    {"downarrow", TXC_COMMAND_SYMBOL, 158},
    {"ell", TXC_COMMAND_SYMBOL, 40},
    {"emptyset", TXC_COMMAND_SYMBOL, 48},
    {"end", TXC_COMMAND_END, 0},
    {"epsilon", TXC_COMMAND_SYMBOL, 4},
    {"equiv", TXC_COMMAND_SYMBOL, 107},
    {"eta", TXC_COMMAND_SYMBOL, 7},
    {"exists", TXC_COMMAND_SYMBOL, 56},
    {"exp", TXC_COMMAND_FUNCTION, 13},
    {"flat", TXC_COMMAND_SYMBOL, 59},
    {"forall", TXC_COMMAND_SYMBOL, 55},
    {"frac", TXC_COMMAND_FRACTION, 3},
    {"frown", TXC_COMMAND_SYMBOL, 126},
    {"gamma", TXC_COMMAND_SYMBOL, 2},
    {"gcd", TXC_COMMAND_FUNCTION, 14},
    {"ge", TXC_COMMAND_SYMBOL, 106},
    {"geq", TXC_COMMAND_SYMBOL, 105},
    {"gets", TXC_COMMAND_SYMBOL, 140},
    {"gg", TXC_COMMAND_SYMBOL, 113},
    {"grave", TXC_COMMAND_ACCENT, 6},
    {"hat", TXC_COMMAND_ACCENT, 7},
    {"heartsuit", TXC_COMMAND_SYMBOL, 64},
    {"hom", TXC_COMMAND_FUNCTION, 15},
    {"hookleftarrow", TXC_COMMAND_SYMBOL, 155},
    {"hookrightarrow", TXC_COMMAND_SYMBOL, 156},
    {"in", TXC_COMMAND_SYMBOL, 120},
    {"inf", TXC_COMMAND_FUNCTION, 16},
    {"infty", TXC_COMMAND_SYMBOL, 46},
    {"int", TXC_COMMAND_BIG_OP, 10},
    {"iota", TXC_COMMAND_SYMBOL, 10},
    {"kappa", TXC_COMMAND_SYMBOL, 11},
    {"ker", TXC_COMMAND_FUNCTION, 17},
    {"lambda", TXC_COMMAND_SYMBOL, 12},
    {"land", TXC_COMMAND_SYMBOL, 87},
    {"langle", TXC_COMMAND_SYMBOL, 176},
    {"lbrace", TXC_COMMAND_SYMBOL, 172},
    {"lceil", TXC_COMMAND_SYMBOL, 180},
    {"le", TXC_COMMAND_SYMBOL, 104},
    {"left", TXC_COMMAND_LEFT, 0},
    {"leftarrow", TXC_COMMAND_SYMBOL, 139},
    {"leftharpoondown", TXC_COMMAND_SYMBOL, 168},
    {"leftharpoonup", TXC_COMMAND_SYMBOL, 167},
    {"leftrightarrow", TXC_COMMAND_SYMBOL, 143},
    {"leq", TXC_COMMAND_SYMBOL, 103},
    {"lfloor", TXC_COMMAND_SYMBOL, 178},
    {"lg", TXC_COMMAND_FUNCTION, 18},
    {"lim", TXC_COMMAND_FUNCTION, 19},
    {"liminf", TXC_COMMAND_FUNCTION, 20},
    {"limits", TXC_COMMAND_LIMITS, 0},
    {"limsup", TXC_COMMAND_FUNCTION, 21},
    {"ll", TXC_COMMAND_SYMBOL, 112},
    {"ln", TXC_COMMAND_FUNCTION, 22},
    {"lnot", TXC_COMMAND_SYMBOL, 58},
    {"log", TXC_COMMAND_FUNCTION, 23},
    {"longleftarrow", TXC_COMMAND_SYMBOL, 147},
    {"longleftrightarrow", TXC_COMMAND_SYMBOL, 149},
    {"longmapsto", TXC_COMMAND_SYMBOL, 154},
    {"longrightarrow", TXC_COMMAND_SYMBOL, 148},
    {"lor", TXC_COMMAND_SYMBOL, 85},
    {"mapsto", TXC_COMMAND_SYMBOL, 153},
    {"mathbb", TXC_COMMAND_STYLE, 0},
    {"mathbf", TXC_COMMAND_STYLE, 1},
    {"mathcal", TXC_COMMAND_STYLE, 2},
    {"mathit", TXC_COMMAND_STYLE, 3},
    {"mathrm", TXC_COMMAND_STYLE, 4},
    {"mathsf", TXC_COMMAND_STYLE, 5},
    {"mathtt", TXC_COMMAND_STYLE, 6},
    {"max", TXC_COMMAND_FUNCTION, 24},
    {"mid", TXC_COMMAND_SYMBOL, 127},
    {"min", TXC_COMMAND_FUNCTION, 25},
    {"models", TXC_COMMAND_SYMBOL, 130},
    {"mp", TXC_COMMAND_SYMBOL, 71},
    {"mu", TXC_COMMAND_SYMBOL, 13},
    {"nabla", TXC_COMMAND_SYMBOL, 49},
    {"natural", TXC_COMMAND_SYMBOL, 60},
    {"nearrow", TXC_COMMAND_SYMBOL, 163},
    {"neg", TXC_COMMAND_SYMBOL, 57},
    {"ni", TXC_COMMAND_SYMBOL, 121},
    {"nolimits", TXC_COMMAND_NOLIMITS, 0},
    {"nu", TXC_COMMAND_SYMBOL, 14},
    {"nwarrow", TXC_COMMAND_SYMBOL, 166},
    {"odot", TXC_COMMAND_SYMBOL, 99},
    {"oint", TXC_COMMAND_BIG_OP, 11},
    {"omega", TXC_COMMAND_SYMBOL, 28},
    {"ominus", TXC_COMMAND_SYMBOL, 96},
    {"oplus", TXC_COMMAND_SYMBOL, 95},
    {"oslash", TXC_COMMAND_SYMBOL, 98},
    {"otimes", TXC_COMMAND_SYMBOL, 97},
    {"owns", TXC_COMMAND_SYMBOL, 122},
    {"parallel", TXC_COMMAND_SYMBOL, 128},
    {"partial", TXC_COMMAND_SYMBOL, 45},
    {"perp", TXC_COMMAND_SYMBOL, 129},
    {"phi", TXC_COMMAND_SYMBOL, 24},
    {"pi", TXC_COMMAND_SYMBOL, 16},
    {"pm", TXC_COMMAND_SYMBOL, 70},
    {"prec", TXC_COMMAND_SYMBOL, 108},
    {"preceq", TXC_COMMAND_SYMBOL, 110},
    {"prime", TXC_COMMAND_SYMBOL, 47},
    {"prod", TXC_COMMAND_BIG_OP, 12},
    {"propto", TXC_COMMAND_SYMBOL, 137},
    {"psi", TXC_COMMAND_SYMBOL, 27},
    {"rangle", TXC_COMMAND_SYMBOL, 177},
    {"rbrace", TXC_COMMAND_SYMBOL, 174},
    {"rceil", TXC_COMMAND_SYMBOL, 181},
    {"rfloor", TXC_COMMAND_SYMBOL, 179},
    {"rho", TXC_COMMAND_SYMBOL, 18},
    {"right", TXC_COMMAND_RIGHT, 0},
    {"rightarrow", TXC_COMMAND_SYMBOL, 141},
    {"rightharpoondown", TXC_COMMAND_SYMBOL, 170},
    {"rightharpoonup", TXC_COMMAND_SYMBOL, 169},
    {"rightleftharpoons", TXC_COMMAND_SYMBOL, 171},
    {"searrow", TXC_COMMAND_SYMBOL, 164},
    {"sec", TXC_COMMAND_FUNCTION, 26},
    {"setminus", TXC_COMMAND_SYMBOL, 88},
    {"sharp", TXC_COMMAND_SYMBOL, 61},
    {"sigma", TXC_COMMAND_SYMBOL, 20},
    {"sim", TXC_COMMAND_SYMBOL, 132},
    {"simeq", TXC_COMMAND_SYMBOL, 133},
    {"sin", TXC_COMMAND_FUNCTION, 27},
    {"sinh", TXC_COMMAND_FUNCTION, 28},
    {"smile", TXC_COMMAND_SYMBOL, 125},
    {"spadesuit", TXC_COMMAND_SYMBOL, 65},
    {"sqcap", TXC_COMMAND_SYMBOL, 81},
    {"sqcup", TXC_COMMAND_SYMBOL, 82},
    {"sqrt", TXC_COMMAND_SQRT, 0},
    {"sqsubseteq", TXC_COMMAND_SYMBOL, 118},
    {"sqsupseteq", TXC_COMMAND_SYMBOL, 119},
    {"star", TXC_COMMAND_SYMBOL, 76},
    {"subset", TXC_COMMAND_SYMBOL, 114},
    {"subseteq", TXC_COMMAND_SYMBOL, 116},
    {"succ", TXC_COMMAND_SYMBOL, 109},
    {"succeq", TXC_COMMAND_SYMBOL, 111},
    {"sum", TXC_COMMAND_BIG_OP, 13},
    {"sup", TXC_COMMAND_FUNCTION, 29},
    {"supset", TXC_COMMAND_SYMBOL, 115},
    {"supseteq", TXC_COMMAND_SYMBOL, 117},
    {"surd", TXC_COMMAND_SYMBOL, 50},
    {"swarrow", TXC_COMMAND_SYMBOL, 165},
    {"tan", TXC_COMMAND_FUNCTION, 30},
    {"tanh", TXC_COMMAND_FUNCTION, 31},
    {"tau", TXC_COMMAND_SYMBOL, 22},
    {"tbinom", TXC_COMMAND_FRACTION, 4},
    {"text", TXC_COMMAND_STYLE, 7},
    {"tfrac", TXC_COMMAND_FRACTION, 5},
    {"theta", TXC_COMMAND_SYMBOL, 8},
    {"tilde", TXC_COMMAND_ACCENT, 8},
    {"times", TXC_COMMAND_SYMBOL, 72},
    {"to", TXC_COMMAND_SYMBOL, 142},
    {"top", TXC_COMMAND_SYMBOL, 51},
    {"triangle", TXC_COMMAND_SYMBOL, 54},
    {"triangleleft", TXC_COMMAND_SYMBOL, 93},
    {"triangleright", TXC_COMMAND_SYMBOL, 94},
    {"uparrow", TXC_COMMAND_SYMBOL, 157},
    {"updownarrow", TXC_COMMAND_SYMBOL, 159},
    {"uplus", TXC_COMMAND_SYMBOL, 83},
    {"upsilon", TXC_COMMAND_SYMBOL, 23},
    {"varepsilon", TXC_COMMAND_SYMBOL, 5},
    {"varphi", TXC_COMMAND_SYMBOL, 25},
    {"varpi", TXC_COMMAND_SYMBOL, 17},
    {"varrho", TXC_COMMAND_SYMBOL, 19},
    {"varsigma", TXC_COMMAND_SYMBOL, 21},
    {"vartheta", TXC_COMMAND_SYMBOL, 9},
    {"vdash", TXC_COMMAND_SYMBOL, 123},
    {"vec", TXC_COMMAND_ACCENT, 9},
    {"vee", TXC_COMMAND_SYMBOL, 84},
    {"vert", TXC_COMMAND_SYMBOL, 67},
    {"wedge", TXC_COMMAND_SYMBOL, 86},
    {"widehat", TXC_COMMAND_ACCENT, 10},
    {"widetilde", TXC_COMMAND_ACCENT, 11},
    {"wp", TXC_COMMAND_SYMBOL, 41},
    {"wr", TXC_COMMAND_SYMBOL, 89},
    {"xi", TXC_COMMAND_SYMBOL, 15},
    {"zeta", TXC_COMMAND_SYMBOL, 6},
    {"{", TXC_COMMAND_SYMBOL, 173},
    {"|", TXC_COMMAND_SYMBOL, 69},
    {"}", TXC_COMMAND_SYMBOL, 175},
};

static int txc_command_order(const uint8_t *name, size_t name_length, const char *entry) {
    size_t entry_length = strlen(entry);
    size_t shared = name_length < entry_length ? name_length : entry_length;
    int order = memcmp(name, entry, shared);
    if (order != 0) {
        return order;
    }
    return name_length < entry_length ? -1 : (name_length > entry_length ? 1 : 0);
}

static const txc_command_entry *txc_command_lookup(const uint8_t *name, size_t name_length) {
    size_t low = 0;
    size_t high = sizeof(TXC_COMMANDS) / sizeof(TXC_COMMANDS[0]);
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        int order = txc_command_order(name, name_length, TXC_COMMANDS[middle].name);
        if (order == 0) {
            return &TXC_COMMANDS[middle];
        }
        if (order > 0) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return NULL;
}

/* The plain TeX delimiter set (\delcode assignments plus the delimiter
 * control words): each row maps a source spelling to its main-family
 * text glyph, its growth ladder, and its extensible pieces. Piece data
 * mirrors the Computer Modern recipes as vendored from KaTeX
 * (Size1 pieces for verticals and arrows, Size4 corners elsewhere;
 * `middle` only for the brace waists). */
#define TXC_NO_PIECE 0

typedef struct txc_delimiter_row {
    const char *name;   /* command spelling, or NULL for a character */
    uint32_t character; /* source character, or 0 for a command */
    txc_delimiter delimiter;
} txc_delimiter_row;

static const txc_delimiter_row TXC_DELIMITERS[] = {
    {NULL, '.', {0, TXC_LADDER_NEVER, 0, 0, 0, 0, TEX_CORE_FAMILY_MAIN}},
    {NULL, '(', {0x0028, TXC_LADDER_LARGE, 0x239B, 0x239C, 0x239D, TXC_NO_PIECE, TEX_CORE_FAMILY_SIZE4}},
    {NULL, ')', {0x0029, TXC_LADDER_LARGE, 0x239E, 0x239F, 0x23A0, TXC_NO_PIECE, TEX_CORE_FAMILY_SIZE4}},
    {NULL, '[', {0x005B, TXC_LADDER_LARGE, 0x23A1, 0x23A2, 0x23A3, TXC_NO_PIECE, TEX_CORE_FAMILY_SIZE4}},
    {NULL, ']', {0x005D, TXC_LADDER_LARGE, 0x23A4, 0x23A5, 0x23A6, TXC_NO_PIECE, TEX_CORE_FAMILY_SIZE4}},
    {NULL, '<', {0x27E8, TXC_LADDER_NEVER, 0, 0, 0, 0, TEX_CORE_FAMILY_MAIN}},
    {NULL, '>', {0x27E9, TXC_LADDER_NEVER, 0, 0, 0, 0, TEX_CORE_FAMILY_MAIN}},
    {NULL, '/', {0x002F, TXC_LADDER_NEVER, 0, 0, 0, 0, TEX_CORE_FAMILY_MAIN}},
    {NULL, '|', {0x2223, TXC_LADDER_ALWAYS, 0x2223, 0x2223, 0x2223, TXC_NO_PIECE, TEX_CORE_FAMILY_SIZE1}},
    {"lbrace", 0, {0x007B, TXC_LADDER_LARGE, 0x23A7, 0x23AA, 0x23A9, 0x23A8, TEX_CORE_FAMILY_SIZE4}},
    {"{", 0, {0x007B, TXC_LADDER_LARGE, 0x23A7, 0x23AA, 0x23A9, 0x23A8, TEX_CORE_FAMILY_SIZE4}},
    {"rbrace", 0, {0x007D, TXC_LADDER_LARGE, 0x23AB, 0x23AA, 0x23AD, 0x23AC, TEX_CORE_FAMILY_SIZE4}},
    {"}", 0, {0x007D, TXC_LADDER_LARGE, 0x23AB, 0x23AA, 0x23AD, 0x23AC, TEX_CORE_FAMILY_SIZE4}},
    {"lbrack", 0, {0x005B, TXC_LADDER_LARGE, 0x23A1, 0x23A2, 0x23A3, TXC_NO_PIECE, TEX_CORE_FAMILY_SIZE4}},
    {"rbrack", 0, {0x005D, TXC_LADDER_LARGE, 0x23A4, 0x23A5, 0x23A6, TXC_NO_PIECE, TEX_CORE_FAMILY_SIZE4}},
    {"langle", 0, {0x27E8, TXC_LADDER_NEVER, 0, 0, 0, 0, TEX_CORE_FAMILY_MAIN}},
    {"rangle", 0, {0x27E9, TXC_LADDER_NEVER, 0, 0, 0, 0, TEX_CORE_FAMILY_MAIN}},
    {"lfloor", 0, {0x230A, TXC_LADDER_LARGE, 0x23A2, 0x23A2, 0x23A3, TXC_NO_PIECE, TEX_CORE_FAMILY_SIZE4}},
    {"rfloor", 0, {0x230B, TXC_LADDER_LARGE, 0x23A5, 0x23A5, 0x23A6, TXC_NO_PIECE, TEX_CORE_FAMILY_SIZE4}},
    {"lceil", 0, {0x2308, TXC_LADDER_LARGE, 0x23A1, 0x23A2, 0x23A2, TXC_NO_PIECE, TEX_CORE_FAMILY_SIZE4}},
    {"rceil", 0, {0x2309, TXC_LADDER_LARGE, 0x23A4, 0x23A5, 0x23A5, TXC_NO_PIECE, TEX_CORE_FAMILY_SIZE4}},
    {"vert", 0, {0x2223, TXC_LADDER_ALWAYS, 0x2223, 0x2223, 0x2223, TXC_NO_PIECE, TEX_CORE_FAMILY_SIZE1}},
    {"Vert", 0, {0x2225, TXC_LADDER_ALWAYS, 0x2225, 0x2225, 0x2225, TXC_NO_PIECE, TEX_CORE_FAMILY_SIZE1}},
    {"|", 0, {0x2225, TXC_LADDER_ALWAYS, 0x2225, 0x2225, 0x2225, TXC_NO_PIECE, TEX_CORE_FAMILY_SIZE1}},
    {"backslash", 0, {0x005C, TXC_LADDER_NEVER, 0, 0, 0, 0, TEX_CORE_FAMILY_MAIN}},
    {"\\", 0, {0x005C, TXC_LADDER_NEVER, 0, 0, 0, 0, TEX_CORE_FAMILY_MAIN}},
    {"uparrow", 0, {0x2191, TXC_LADDER_ALWAYS, 0x2191, 0x23D0, 0x23D0, TXC_NO_PIECE, TEX_CORE_FAMILY_SIZE1}},
    {"downarrow", 0, {0x2193, TXC_LADDER_ALWAYS, 0x23D0, 0x23D0, 0x2193, TXC_NO_PIECE, TEX_CORE_FAMILY_SIZE1}},
    {"updownarrow", 0, {0x2195, TXC_LADDER_ALWAYS, 0x2191, 0x23D0, 0x2193, TXC_NO_PIECE, TEX_CORE_FAMILY_SIZE1}},
    {"Uparrow", 0, {0x21D1, TXC_LADDER_ALWAYS, 0x21D1, 0x2016, 0x2016, TXC_NO_PIECE, TEX_CORE_FAMILY_SIZE1}},
    {"Downarrow", 0, {0x21D3, TXC_LADDER_ALWAYS, 0x2016, 0x2016, 0x21D3, TXC_NO_PIECE, TEX_CORE_FAMILY_SIZE1}},
    {"Updownarrow", 0, {0x21D5, TXC_LADDER_ALWAYS, 0x21D1, 0x2016, 0x21D3, TXC_NO_PIECE, TEX_CORE_FAMILY_SIZE1}},
};

static const txc_delimiter *txc_delimiter_character(uint32_t codepoint) {
    for (size_t index = 0; index < sizeof(TXC_DELIMITERS) / sizeof(TXC_DELIMITERS[0]); index++) {
        if (TXC_DELIMITERS[index].name == NULL && TXC_DELIMITERS[index].character == codepoint) {
            return &TXC_DELIMITERS[index].delimiter;
        }
    }
    return NULL;
}

static const txc_delimiter *txc_delimiter_command(const uint8_t *name, size_t name_length) {
    for (size_t index = 0; index < sizeof(TXC_DELIMITERS) / sizeof(TXC_DELIMITERS[0]); index++) {
        const txc_delimiter_row *row = &TXC_DELIMITERS[index];
        if (row->name != NULL && strlen(row->name) == name_length && memcmp(row->name, name, name_length) == 0) {
            return &row->delimiter;
        }
    }
    return NULL;
}

/* Math alignment environments (milestone M2): the amsmath matrix family.
 * Each row names the environment and the fence pair its amsmath
 * definition wraps around the array — matrix is bare, the others are
 * \left<delim> matrix \right<delim>. Delimiter values mirror the
 * TXC_DELIMITERS rows for the same spellings. */
typedef struct txc_environment_row {
    const char *name;
    bool delimited;
    txc_delimiter left;
    txc_delimiter right;
} txc_environment_row;

static const txc_environment_row TXC_ENVIRONMENTS[] = {
    {"Bmatrix",
     true,
     {0x007B, TXC_LADDER_LARGE, 0x23A7, 0x23AA, 0x23A9, 0x23A8, TEX_CORE_FAMILY_SIZE4},
     {0x007D, TXC_LADDER_LARGE, 0x23AB, 0x23AA, 0x23AD, 0x23AC, TEX_CORE_FAMILY_SIZE4}},
    {"Vmatrix",
     true,
     {0x2225, TXC_LADDER_ALWAYS, 0x2225, 0x2225, 0x2225, TXC_NO_PIECE, TEX_CORE_FAMILY_SIZE1},
     {0x2225, TXC_LADDER_ALWAYS, 0x2225, 0x2225, 0x2225, TXC_NO_PIECE, TEX_CORE_FAMILY_SIZE1}},
    {"bmatrix",
     true,
     {0x005B, TXC_LADDER_LARGE, 0x23A1, 0x23A2, 0x23A3, TXC_NO_PIECE, TEX_CORE_FAMILY_SIZE4},
     {0x005D, TXC_LADDER_LARGE, 0x23A4, 0x23A5, 0x23A6, TXC_NO_PIECE, TEX_CORE_FAMILY_SIZE4}},
    {"matrix",
     false,
     {0, TXC_LADDER_NEVER, 0, 0, 0, 0, TEX_CORE_FAMILY_MAIN},
     {0, TXC_LADDER_NEVER, 0, 0, 0, 0, TEX_CORE_FAMILY_MAIN}},
    {"pmatrix",
     true,
     {0x0028, TXC_LADDER_LARGE, 0x239B, 0x239C, 0x239D, TXC_NO_PIECE, TEX_CORE_FAMILY_SIZE4},
     {0x0029, TXC_LADDER_LARGE, 0x239E, 0x239F, 0x23A0, TXC_NO_PIECE, TEX_CORE_FAMILY_SIZE4}},
    {"vmatrix",
     true,
     {0x2223, TXC_LADDER_ALWAYS, 0x2223, 0x2223, 0x2223, TXC_NO_PIECE, TEX_CORE_FAMILY_SIZE1},
     {0x2223, TXC_LADDER_ALWAYS, 0x2223, 0x2223, 0x2223, TXC_NO_PIECE, TEX_CORE_FAMILY_SIZE1}},
};

/* amsmath's \c@MaxMatrixCols: the matrix preamble repeats ten centered
 * columns, so the tenth alignment tab of a row — which would open an
 * eleventh column — is a structured error, exactly where amsmath stops
 * accepting. */
#define TXC_MAX_MATRIX_COLUMNS 10

static const txc_environment_row *txc_environment_find(const uint8_t *name, size_t name_length) {
    for (size_t index = 0; index < sizeof(TXC_ENVIRONMENTS) / sizeof(TXC_ENVIRONMENTS[0]); index++) {
        const txc_environment_row *row = &TXC_ENVIRONMENTS[index];
        if (strlen(row->name) == name_length && memcmp(row->name, name, name_length) == 0) {
            return row;
        }
    }
    return NULL;
}

/* Explicit-size delimiter commands: plain TeX's \big family. The class
 * follows the suffix (l Open, m Rel, r Close, bare Ord); the size step
 * selects the fixed rule-19 target. */
typedef struct txc_size_command {
    const char *name;
    txc_atom_class atom_class;
    int size;
} txc_size_command;

static const txc_size_command TXC_SIZE_COMMANDS[] = {
    {"Big", TXC_ATOM_ORD, 2},
    {"Bigg", TXC_ATOM_ORD, 4},
    {"Biggl", TXC_ATOM_OPEN, 4},
    {"Biggm", TXC_ATOM_REL, 4},
    {"Biggr", TXC_ATOM_CLOSE, 4},
    {"Bigl", TXC_ATOM_OPEN, 2},
    {"Bigm", TXC_ATOM_REL, 2},
    {"Bigr", TXC_ATOM_CLOSE, 2},
    {"big", TXC_ATOM_ORD, 1},
    {"bigg", TXC_ATOM_ORD, 3},
    {"biggl", TXC_ATOM_OPEN, 3},
    {"biggm", TXC_ATOM_REL, 3},
    {"biggr", TXC_ATOM_CLOSE, 3},
    {"bigl", TXC_ATOM_OPEN, 1},
    {"bigm", TXC_ATOM_REL, 1},
    {"bigr", TXC_ATOM_CLOSE, 1},
};

/* One dispatch row's payload name, or NULL when the index is out of bounds
 * for its class table. Payload-free classes (\left, \right, \sqrt,
 * \limits, \nolimits) report the dispatch name itself for index 0. */
static const char *txc_command_payload_name(const txc_command_entry *entry) {
#define TXC_PAYLOAD_NAME(table) (entry->index < sizeof(table) / sizeof((table)[0]) ? (table)[entry->index].name : NULL)
    switch (entry->kind) {
    case TXC_COMMAND_SYMBOL:
        return TXC_PAYLOAD_NAME(TXC_MATH_SYMBOLS);
    case TXC_COMMAND_FRACTION:
        return TXC_PAYLOAD_NAME(TXC_FRACTION_COMMANDS);
    case TXC_COMMAND_BIG_OP:
        return TXC_PAYLOAD_NAME(TXC_BIG_OPS);
    case TXC_COMMAND_FUNCTION:
        return TXC_PAYLOAD_NAME(TXC_FUNCTION_NAMES);
    case TXC_COMMAND_STYLE:
        return TXC_PAYLOAD_NAME(TXC_STYLE_COMMANDS);
    case TXC_COMMAND_ACCENT:
        return TXC_PAYLOAD_NAME(TXC_ACCENT_COMMANDS);
    case TXC_COMMAND_SIZE:
        return TXC_PAYLOAD_NAME(TXC_SIZE_COMMANDS);
    case TXC_COMMAND_SQRT:
    case TXC_COMMAND_LEFT:
    case TXC_COMMAND_RIGHT:
    case TXC_COMMAND_LIMITS:
    case TXC_COMMAND_NOLIMITS:
    case TXC_COMMAND_BEGIN:
    case TXC_COMMAND_END:
        return entry->index == 0 ? entry->name : NULL;
    }
#undef TXC_PAYLOAD_NAME
    return NULL;
}

bool txc_command_table_sorted(void) {
    size_t count = sizeof(TXC_COMMANDS) / sizeof(TXC_COMMANDS[0]);
    for (size_t index = 0; index < count; index++) {
        const txc_command_entry *entry = &TXC_COMMANDS[index];
        if (index > 0 && strcmp(TXC_COMMANDS[index - 1].name, entry->name) >= 0) {
            return false;
        }
        /* Every row must dispatch to the payload row of the same name, so
         * inserting into or reordering a typed table cannot silently
         * redirect an in-bounds index to different semantics. Alias rows
         * (\le, \to, \gets, ...) share a payload deliberately: the check
         * accepts a payload whose name differs only when the payload row's
         * own name resolves back to this entry's class and index. */
        const char *payload = txc_command_payload_name(entry);
        if (payload == NULL) {
            return false;
        }
        if (strcmp(payload, entry->name) != 0) {
            const txc_command_entry *canonical = txc_command_lookup((const uint8_t *)payload, strlen(payload));
            if (canonical == NULL || canonical->kind != entry->kind || canonical->index != entry->index) {
                return false;
            }
        }
    }
    return true;
}

typedef struct txc_spacing_command {
    const char *name;
    txc_space space;
} txc_spacing_command;

static const txc_spacing_command TXC_SPACING_COMMANDS[] = {
    {" ", TXC_SPACE_WORD},
    {"!", TXC_SPACE_NEGATIVE_THIN},
    {",", TXC_SPACE_THIN},
    {":", TXC_SPACE_MEDIUM},
    {";", TXC_SPACE_THICK},
    {"qquad", TXC_SPACE_QQUAD},
    {"quad", TXC_SPACE_QUAD},
};

static const txc_spacing_command *txc_spacing(const uint8_t *name, size_t name_length) {
    for (size_t index = 0; index < sizeof(TXC_SPACING_COMMANDS) / sizeof(TXC_SPACING_COMMANDS[0]); index++) {
        const txc_spacing_command *command = &TXC_SPACING_COMMANDS[index];
        if (strlen(command->name) == name_length && memcmp(command->name, name, name_length) == 0) {
            return command;
        }
    }
    return NULL;
}

static txc_item *txc_append(txc_arena *arena, txc_list *list) {
    txc_item *item = txc_arena_alloc(arena, sizeof(txc_item));
    if (item == NULL) {
        return NULL;
    }
    item->next = NULL;
    if (list->tail != NULL) {
        list->tail->next = item;
    } else {
        list->head = item;
    }
    list->tail = item;
    list->count += 1;
    return item;
}

/* The one field initializer: zeroes the whole payload union alongside the
 * tag, so every variant switch starts from fully defined state. */
static void txc_field_reset(txc_field *field, size_t at) {
    memset(field, 0, sizeof(*field));
    field->kind = TXC_FIELD_EMPTY;
    field->range.begin = at;
    field->range.end = at;
}

/* Compile-time guard on the tagged-union compaction: the payloads overlay
 * one another, so a field stays a fraction of the former side-by-side
 * layout and a parser item stays cache-friendly. */
_Static_assert(sizeof(txc_field) <= 64, "txc_field payloads must overlay in a union");

static txc_item *txc_append_atom(
    txc_arena *arena,
    txc_list *list,
    txc_atom_class atom_class,
    uint32_t codepoint,
    tex_core_style style,
    tex_core_family family,
    tex_core_range range
) {
    txc_item *item = txc_append(arena, list);
    if (item == NULL) {
        return NULL;
    }
    item->kind = TXC_ITEM_ATOM;
    item->atom_class = atom_class;
    txc_field_reset(&item->nucleus, range.begin);
    item->nucleus.kind = TXC_FIELD_CHAR;
    item->nucleus.character.codepoint = codepoint;
    item->nucleus.character.style = style;
    item->nucleus.character.family = family;
    item->nucleus.range = range;
    txc_field_reset(&item->sup, range.end);
    txc_field_reset(&item->sub, range.end);
    item->sub_first = false;
    item->op_limits = TXC_LIMITS_DISPLAY;
    item->range = range;
    return item;
}

/* Renders a control-sequence name into a bounded ASCII-safe form for error
 * messages: letters pass through, anything else appears as <U+XXXX>. */
static void txc_command_label(const uint8_t *name, size_t name_length, char *buffer, size_t capacity) {
    size_t written = 0;
    for (size_t index = 0; index < name_length && written + 12 < capacity; index++) {
        uint8_t byte = name[index];
        if (byte >= 0x20 && byte < 0x7F) {
            buffer[written++] = (char)byte;
        } else {
            int printed = snprintf(buffer + written, capacity - written, "<0x%02X>", byte);
            written += printed > 0 ? (size_t)printed : 0;
        }
    }
    buffer[written] = '\0';
}

/* One classified math glyph: the atom class, the rendered codepoint, and
 * the face. Used both for atom nuclei and for script character fields
 * (whose class is irrelevant — a field is not an atom). `family` is main
 * until delivery stamps the innermost enclosing style switch's alphabet. */
typedef struct txc_math_glyph {
    txc_atom_class atom_class;
    uint32_t codepoint;
    tex_core_style style;
    tex_core_family family;
} txc_math_glyph;

/* One parsed construct on its way into a field or a new atom: exactly
 * one member is set. */
typedef struct txc_construct {
    const txc_math_glyph *glyph;
    const txc_list *group;
    txc_fraction *fraction;
    const txc_sized_delimiter *sized;
    txc_fenced *fenced;
    txc_radical *radical;
    txc_accent *accent;
    txc_array *array;
} txc_construct;

/* Fills one field from a delivered construct. */
static void txc_field_fill(txc_field *field, const txc_construct *construct, tex_core_range range) {
    if (construct->glyph != NULL) {
        field->kind = TXC_FIELD_CHAR;
        field->character.codepoint = construct->glyph->codepoint;
        field->character.style = construct->glyph->style;
        field->character.family = construct->glyph->family;
    } else if (construct->group != NULL) {
        field->kind = TXC_FIELD_LIST;
        field->list = *construct->group;
    } else if (construct->fraction != NULL) {
        field->kind = TXC_FIELD_FRACTION;
        field->fraction = construct->fraction;
    } else if (construct->sized != NULL) {
        field->kind = TXC_FIELD_DELIMITER;
        field->sized = *construct->sized;
    } else if (construct->radical != NULL) {
        field->kind = TXC_FIELD_RADICAL;
        field->radical = construct->radical;
    } else if (construct->accent != NULL) {
        field->kind = TXC_FIELD_ACCENT;
        field->accent = construct->accent;
    } else if (construct->array != NULL) {
        field->kind = TXC_FIELD_ARRAY;
        field->array = construct->array;
    } else {
        field->kind = TXC_FIELD_FENCED;
        field->fenced = construct->fenced;
    }
    field->range = range;
}

/* Classifies one math-mode character per TeX's default \mathcode
 * assignments: letters on the math italic face, digits and the period
 * upright, and the classed math characters. Returns false when the
 * character is outside the math surface. */
static bool txc_math_classify(uint32_t codepoint, txc_math_glyph *glyph) {
    glyph->family = TEX_CORE_FAMILY_MAIN;
    if (txc_letter_codepoint(codepoint)) {
        glyph->atom_class = TXC_ATOM_ORD;
        glyph->codepoint = codepoint;
        glyph->style = TEX_CORE_STYLE_ITALIC;
        return true;
    }
    if (txc_digit_codepoint(codepoint) || codepoint == '.') {
        glyph->atom_class = TXC_ATOM_ORD;
        glyph->codepoint = codepoint;
        glyph->style = TEX_CORE_STYLE_UPRIGHT;
        return true;
    }
    const txc_math_character *character = txc_math_character_find(codepoint);
    if (character != NULL) {
        glyph->atom_class = character->atom_class;
        glyph->codepoint = character->codepoint;
        glyph->style = TEX_CORE_STYLE_UPRIGHT;
        return true;
    }
    return false;
}

/* One group in progress. The root list is the bottom frame; every `{`
 * pushes a frame that closes back into its parent as a group atom's
 * nucleus or as a script field. Frames live in the arena, so nesting
 * depth is bounded by memory, not by the C stack. */
typedef enum txc_frame_role {
    TXC_FRAME_ROOT = 0,
    TXC_FRAME_GROUP = 1,
    TXC_FRAME_SCRIPT = 2,
    TXC_FRAME_FENCED = 3,
    TXC_FRAME_INDEX = 4,
    TXC_FRAME_CELL = 5
} txc_frame_role;

/* Which construct the next delimiter token completes: a \left push, a
 * \right pop, or an explicit-size atom. */
typedef enum txc_delim_wait {
    TXC_DELIM_WAIT_NONE = 0,
    TXC_DELIM_WAIT_LEFT = 1,
    TXC_DELIM_WAIT_RIGHT = 2,
    TXC_DELIM_WAIT_EXPLICIT = 3
} txc_delim_wait;

/* Which script a pending `^`/`_` will fill. */
typedef enum txc_pending { TXC_PENDING_NONE = 0, TXC_PENDING_SUP = 1, TXC_PENDING_SUB = 2 } txc_pending;

/* Which command a frame is collecting arguments for. */
typedef enum txc_collect_kind {
    TXC_COLLECT_NONE = 0,
    TXC_COLLECT_FRACTION = 1,
    TXC_COLLECT_RADICAL = 2,
    TXC_COLLECT_ACCENT = 3,
    TXC_COLLECT_STYLE = 4
} txc_collect_kind;

typedef struct txc_collect {
    txc_collect_kind kind;
    tex_core_range command;
    /* The construct being filled (one of, by kind). */
    txc_fraction *fraction;
    txc_radical *radical;
    txc_accent *accent;
    txc_face face;
    /* FRACTION: the denominator is next; RADICAL: the index was taken. */
    bool second;
    /* The destination: the eagerly appended atom, or a script field. */
    txc_item *item;
    txc_item *target;
    txc_pending script;
    size_t mark;
} txc_collect;

typedef struct txc_frame {
    txc_list list;
    txc_frame_role role;
    /* The math alphabet the innermost enclosing style switch imposes on
     * this frame's letters and digits, if any. Stamped onto glyphs as
     * they are delivered, so an inner switch always wins over an outer
     * one and no post-parse rewrite pass exists. \text frames are never
     * styled — their content follows document rules. */
    bool styled;
    tex_core_family face_family;
    /* GROUP and SCRIPT: byte offset of the opening brace. */
    size_t open;
    /* SCRIPT: the scripted atom in the parent frame's list, whether the
     * script is the superscript, and the byte offset of its mark. */
    txc_item *target;
    txc_pending script;
    size_t mark;
    /* The script pending inside this frame's list, if any. */
    txc_pending pending;
    txc_item *pending_target;
    size_t pending_mark;
    /* The argument-collecting command in progress on this frame, if
     * any: which construct the next delivery fills, its phase, and
     * where the completed construct lands — the atom appended at the
     * command, or the script field captured from a pending mark. The
     * guards keep at most one collector active per frame. */
    txc_collect collect;
    /* Whether this frame is \text content (document rules). */
    bool text_mode;
    /* The delimiter token this frame is waiting for, if any, and the
     * command that asked for it. EXPLICIT carries the atom class and the
     * \big size step. */
    txc_delim_wait delim_wait;
    tex_core_range delim_command;
    txc_atom_class delim_class;
    int delim_size;
    /* FENCED frames: the \left command token and its delimiter. */
    tex_core_range fence_command;
    txc_delimiter fence_left;
    tex_core_range fence_left_range;
    /* CELL frames: the environment in progress. The frame's own list is
     * the current cell; completed cells and rows accumulate here. `open`
     * holds the \begin token's start, `env_begin` the whole \begin{...}
     * run, and `env_cell_start` the byte where the current cell's content
     * begins (after \begin{...}, `&`, or `\\`) — the anchor an empty
     * cell's range collapses to. */
    const txc_environment_row *env;
    tex_core_range env_begin;
    txc_array_row *env_rows;
    txc_array_row *env_rows_tail;
    size_t env_row_count;
    txc_array_cell *env_cells;
    txc_array_cell *env_cells_tail;
    size_t env_cell_count;
    size_t env_cell_start;
    struct txc_frame *parent;
} txc_frame;

static void txc_frame_init(txc_frame *frame, txc_frame_role role, txc_frame *parent) {
    frame->list.head = NULL;
    frame->list.tail = NULL;
    frame->list.count = 0;
    frame->role = role;
    frame->styled = false;
    frame->face_family = TEX_CORE_FAMILY_MAIN;
    frame->open = 0;
    frame->target = NULL;
    frame->script = TXC_PENDING_NONE;
    frame->mark = 0;
    frame->pending = TXC_PENDING_NONE;
    frame->pending_target = NULL;
    frame->pending_mark = 0;
    frame->collect.kind = TXC_COLLECT_NONE;
    frame->collect.command.begin = 0;
    frame->collect.command.end = 0;
    frame->collect.fraction = NULL;
    frame->collect.radical = NULL;
    frame->collect.accent = NULL;
    frame->collect.face = TXC_FACE_RM;
    frame->collect.second = false;
    frame->collect.item = NULL;
    frame->collect.target = NULL;
    frame->collect.script = TXC_PENDING_NONE;
    frame->collect.mark = 0;
    frame->text_mode = false;
    frame->delim_wait = TXC_DELIM_WAIT_NONE;
    frame->delim_command.begin = 0;
    frame->delim_command.end = 0;
    frame->delim_class = TXC_ATOM_ORD;
    frame->delim_size = 0;
    frame->fence_command.begin = 0;
    frame->fence_command.end = 0;
    frame->fence_left = TXC_DELIMITERS[0].delimiter;
    frame->fence_left_range.begin = 0;
    frame->fence_left_range.end = 0;
    frame->env = NULL;
    frame->env_begin.begin = 0;
    frame->env_begin.end = 0;
    frame->env_rows = NULL;
    frame->env_rows_tail = NULL;
    frame->env_row_count = 0;
    frame->env_cells = NULL;
    frame->env_cells_tail = NULL;
    frame->env_cell_count = 0;
    frame->env_cell_start = 0;
    frame->parent = parent;
}

/* Deepest legal `{` nesting. Layout recurses over nuclei and script
 * fields, so the parser bounds the depth it can produce. */
#define TXC_GROUP_DEPTH_LIMIT 255

static const char *txc_script_noun(txc_pending pending) {
    return pending == TXC_PENDING_SUP ? "superscript" : "subscript";
}

static txc_field *txc_script_field(txc_item *item, txc_pending pending) {
    return pending == TXC_PENDING_SUP ? &item->sup : &item->sub;
}

static const char *txc_argument_noun(const txc_frame *frame) {
    switch (frame->collect.kind) {
    case TXC_COLLECT_STYLE:
        return frame->collect.face == TXC_FACE_TEXT ? "text" : "style";
    case TXC_COLLECT_ACCENT:
        return "accent";
    case TXC_COLLECT_RADICAL:
        return "radical";
    default:
        return frame->collect.second ? "denominator" : "numerator";
    }
}

/* Lands a completed construct at the collector's destination: the
 * eagerly appended atom's nucleus, or the captured script field. */
static void txc_collect_finish(txc_frame *frame, const txc_field *staged, size_t end) {
    tex_core_range whole = {frame->collect.command.begin, end};
    if (frame->collect.item != NULL) {
        txc_item *item = frame->collect.item;
        item->nucleus = *staged;
        item->nucleus.range = whole;
        txc_field_reset(&item->sup, end);
        txc_field_reset(&item->sub, end);
        item->range = whole;
    } else {
        txc_item *target = frame->collect.target;
        txc_field *field = txc_script_field(target, frame->collect.script);
        *field = *staged;
        field->range.begin = frame->collect.mark;
        field->range.end = end;
        target->range.end = end;
        if (frame->collect.script == TXC_PENDING_SUB && target->sup.kind == TXC_FIELD_EMPTY) {
            target->sub_first = true;
        }
    }
    frame->collect.kind = TXC_COLLECT_NONE;
    frame->collect.fraction = NULL;
    frame->collect.radical = NULL;
    frame->collect.accent = NULL;
    frame->collect.second = false;
    frame->collect.item = NULL;
    frame->collect.target = NULL;
}

/* Starts collecting for `kind` at `command`: a pending script mark
 * captures the destination, otherwise the eager atom of `atom_class`
 * is appended so child order stays source order. */
static tex_core_status txc_collect_begin(
    txc_arena *arena,
    txc_frame *frame,
    txc_collect_kind kind,
    txc_atom_class atom_class,
    tex_core_range command,
    tex_core_error *error
) {
    frame->collect.kind = kind;
    frame->collect.command = command;
    frame->collect.second = false;
    if (frame->pending != TXC_PENDING_NONE) {
        frame->collect.item = NULL;
        frame->collect.target = frame->pending_target;
        frame->collect.script = frame->pending;
        frame->collect.mark = frame->pending_mark;
        frame->pending = TXC_PENDING_NONE;
        frame->pending_target = NULL;
        return TEX_CORE_STATUS_OK;
    }
    txc_item *item = txc_append(arena, &frame->list);
    if (item == NULL) {
        return txc_fail(error, TEX_CORE_STATUS_ALLOCATION_FAILED, NULL, "allocation failed");
    }
    item->kind = TXC_ITEM_ATOM;
    item->atom_class = atom_class;
    txc_field_reset(&item->nucleus, command.begin);
    txc_field_reset(&item->sup, command.end);
    txc_field_reset(&item->sub, command.end);
    item->sub_first = false;
    item->op_limits = TXC_LIMITS_DISPLAY;
    item->range = command;
    frame->collect.item = item;
    frame->collect.target = NULL;
    return TEX_CORE_STATUS_OK;
}

/* Delivers one parsed construct — a character, a symbol command, a
 * closed group list, a sized delimiter, or a closed fence — into the
 * frame: as the active collector's next argument, as the pending script
 * field, or as a new atom of `atom_class`. `range` is the construct's
 * own range; script fields extend it back to their mark. */
static tex_core_status txc_deliver(
    txc_arena *arena,
    txc_frame *frame,
    const txc_construct *construct,
    txc_atom_class atom_class,
    tex_core_range range,
    tex_core_error *error
) {
    /* Stamp the active math alphabet onto letter and digit glyphs as they
     * arrive: a style collector's own face for its immediate argument,
     * otherwise the face the frame inherited from the innermost enclosing
     * style-switch argument. Stamping at delivery makes style scope
     * lexical — an outer switch can never overwrite an inner one — and
     * resolves every glyph's face exactly once. */
    txc_math_glyph faced;
    txc_construct faced_construct;
    if (construct->glyph != NULL) {
        bool styled = frame->styled;
        tex_core_family family = frame->face_family;
        if (frame->collect.kind == TXC_COLLECT_STYLE) {
            styled = frame->collect.face != TXC_FACE_TEXT;
            family = styled ? TXC_FACE_FAMILIES[frame->collect.face] : family;
        }
        if (styled &&
            (txc_letter_codepoint(construct->glyph->codepoint) || txc_digit_codepoint(construct->glyph->codepoint))) {
            faced = *construct->glyph;
            faced.family = family;
            faced.style = TEX_CORE_STYLE_UPRIGHT;
            faced_construct = *construct;
            faced_construct.glyph = &faced;
            construct = &faced_construct;
        }
    }
    if (frame->collect.kind != TXC_COLLECT_NONE) {
        txc_field staged;
        txc_field_reset(&staged, range.begin);
        switch (frame->collect.kind) {
        case TXC_COLLECT_FRACTION:
            txc_field_fill(
                frame->collect.second ? &frame->collect.fraction->den : &frame->collect.fraction->num,
                construct,
                range
            );
            if (!frame->collect.second) {
                frame->collect.second = true;
                return TEX_CORE_STATUS_OK;
            }
            staged.kind = TXC_FIELD_FRACTION;
            staged.fraction = frame->collect.fraction;
            break;
        case TXC_COLLECT_RADICAL:
            txc_field_fill(&frame->collect.radical->argument, construct, range);
            staged.kind = TXC_FIELD_RADICAL;
            staged.radical = frame->collect.radical;
            break;
        case TXC_COLLECT_ACCENT:
            txc_field_fill(&frame->collect.accent->argument, construct, range);
            staged.kind = TXC_FIELD_ACCENT;
            staged.accent = frame->collect.accent;
            break;
        case TXC_COLLECT_STYLE:
        default:
            /* Non-\text styles already stamped the delivery above; a
             * braced argument's glyphs were stamped inside its frame. */
            txc_field_fill(&staged, construct, range);
            if (frame->collect.face == TXC_FACE_TEXT) {
                /* A braced argument becomes document-rule text content;
                 * a bare character is upright main, exactly \mathrm,
                 * and keeps that face under an outer style switch. */
                if (staged.kind == TXC_FIELD_LIST) {
                    staged.kind = TXC_FIELD_TEXT;
                } else if (staged.kind == TXC_FIELD_CHAR) {
                    staged.character.style = TEX_CORE_STYLE_UPRIGHT;
                    staged.character.family = TEX_CORE_FAMILY_MAIN;
                    staged.character.text_face = true;
                }
            }
            break;
        }
        txc_collect_finish(frame, &staged, range.end);
        return TEX_CORE_STATUS_OK;
    }
    if (frame->pending != TXC_PENDING_NONE) {
        txc_item *item = frame->pending_target;
        txc_field *field = txc_script_field(item, frame->pending);
        txc_field_fill(field, construct, range);
        field->range.begin = frame->pending_mark;
        item->range.end = range.end;
        if (frame->pending == TXC_PENDING_SUB && item->sup.kind == TXC_FIELD_EMPTY) {
            item->sub_first = true;
        }
        frame->pending = TXC_PENDING_NONE;
        frame->pending_target = NULL;
        return TEX_CORE_STATUS_OK;
    }
    if (construct->glyph != NULL) {
        const txc_math_glyph *glyph = construct->glyph;
        if (txc_append_atom(
                arena,
                &frame->list,
                glyph->atom_class,
                glyph->codepoint,
                glyph->style,
                glyph->family,
                range
            ) == NULL) {
            return txc_fail(error, TEX_CORE_STATUS_ALLOCATION_FAILED, NULL, "allocation failed");
        }
        return TEX_CORE_STATUS_OK;
    }
    txc_item *item = txc_append(arena, &frame->list);
    if (item == NULL) {
        return txc_fail(error, TEX_CORE_STATUS_ALLOCATION_FAILED, NULL, "allocation failed");
    }
    item->kind = TXC_ITEM_ATOM;
    item->atom_class = atom_class;
    txc_field_reset(&item->nucleus, range.begin);
    txc_field_fill(&item->nucleus, construct, range);
    txc_field_reset(&item->sup, range.end);
    txc_field_reset(&item->sub, range.end);
    item->sub_first = false;
    item->op_limits = TXC_LIMITS_DISPLAY;
    item->range = range;
    return TEX_CORE_STATUS_OK;
}

/* Acts on the delimiter token a pending \left, \right, or explicit-size
 * command was waiting for: push the fence frame, pop it into a fenced
 * construct, or deliver the sized-delimiter atom. */
static tex_core_status txc_delimiter_arrived(
    txc_arena *arena,
    txc_frame **frame_slot,
    size_t *depth,
    const txc_delimiter *delimiter,
    tex_core_range range,
    tex_core_error *error
) {
    txc_frame *frame = *frame_slot;
    txc_delim_wait wait = frame->delim_wait;
    frame->delim_wait = TXC_DELIM_WAIT_NONE;
    if (wait == TXC_DELIM_WAIT_LEFT) {
        if (*depth == TXC_GROUP_DEPTH_LIMIT) {
            return txc_fail(error, TEX_CORE_STATUS_UNSUPPORTED, &frame->delim_command, "group nesting too deep");
        }
        txc_frame *inner = txc_arena_alloc(arena, sizeof(txc_frame));
        if (inner == NULL) {
            return txc_fail(error, TEX_CORE_STATUS_ALLOCATION_FAILED, NULL, "allocation failed");
        }
        txc_frame_init(inner, TXC_FRAME_FENCED, frame);
        inner->styled = frame->styled;
        inner->face_family = frame->face_family;
        inner->open = frame->delim_command.begin;
        inner->fence_command = frame->delim_command;
        inner->fence_left = *delimiter;
        inner->fence_left_range = range;
        *frame_slot = inner;
        *depth += 1;
        return TEX_CORE_STATUS_OK;
    }
    if (wait == TXC_DELIM_WAIT_RIGHT) {
        txc_fenced *fenced = txc_arena_alloc(arena, sizeof(txc_fenced));
        if (fenced == NULL) {
            return txc_fail(error, TEX_CORE_STATUS_ALLOCATION_FAILED, NULL, "allocation failed");
        }
        fenced->left = frame->fence_left;
        fenced->left_range = frame->fence_left_range;
        fenced->right = *delimiter;
        fenced->right_range = range;
        fenced->list = frame->list;
        tex_core_range whole = {frame->open, range.end};
        *frame_slot = frame->parent;
        *depth -= 1;
        txc_construct construct = {NULL, NULL, NULL, NULL, fenced, NULL, NULL, NULL};
        return txc_deliver(arena, *frame_slot, &construct, TXC_ATOM_INNER, whole, error);
    }
    txc_sized_delimiter sized = {*delimiter, frame->delim_size};
    tex_core_range whole = {frame->delim_command.begin, range.end};
    txc_construct construct = {NULL, NULL, NULL, &sized, NULL, NULL, NULL, NULL};
    return txc_deliver(arena, frame, &construct, frame->delim_class, whole, error);
}

/* Scans an environment name argument after \begin or \end: optional
 * blanks, `{`, one or more ASCII letters with an optional trailing `*`
 * (the starred-environment spelling, so \begin{align*} reports its own
 * name), `}`. On success `name` borrows the source bytes and `whole`
 * extends `command` through the closing brace. Anything else is the
 * structured error `missing environment name` at the offending token —
 * or at the command itself at end of input, exactly like the
 * missing-argument errors. */
static tex_core_status txc_environment_name(
    txc_scanner *scanner,
    const uint8_t *source,
    tex_core_range command,
    const uint8_t **name,
    size_t *name_length,
    tex_core_range *whole,
    tex_core_error *error
) {
    txc_token token;
    for (;;) {
        tex_core_status status = txc_scan(scanner, &token, error);
        if (status != TEX_CORE_STATUS_OK) {
            return status;
        }
        if (token.kind != TXC_TOKEN_SPACE) {
            break;
        }
    }
    if (token.kind == TXC_TOKEN_END) {
        return txc_fail(error, TEX_CORE_STATUS_UNSUPPORTED, &command, "missing environment name");
    }
    if (token.kind != TXC_TOKEN_CHARACTER || token.codepoint != '{') {
        return txc_fail(error, TEX_CORE_STATUS_UNSUPPORTED, &token.range, "missing environment name");
    }
    size_t begin = 0;
    size_t length = 0;
    for (;;) {
        tex_core_status status = txc_scan(scanner, &token, error);
        if (status != TEX_CORE_STATUS_OK) {
            return status;
        }
        if (token.kind == TXC_TOKEN_CHARACTER && txc_letter_codepoint(token.codepoint)) {
            if (length == 0) {
                begin = token.range.begin;
            }
            length += 1;
            continue;
        }
        if (token.kind == TXC_TOKEN_CHARACTER && token.codepoint == '*' && length > 0) {
            length += 1;
            tex_core_status status = txc_scan(scanner, &token, error);
            if (status != TEX_CORE_STATUS_OK) {
                return status;
            }
        }
        break;
    }
    if (token.kind == TXC_TOKEN_END) {
        return txc_fail(error, TEX_CORE_STATUS_UNSUPPORTED, &command, "missing environment name");
    }
    if (token.kind != TXC_TOKEN_CHARACTER || token.codepoint != '}' || length == 0) {
        return txc_fail(error, TEX_CORE_STATUS_UNSUPPORTED, &token.range, "missing environment name");
    }
    *name = source + begin;
    *name_length = length;
    whole->begin = command.begin;
    whole->end = token.range.end;
    return TEX_CORE_STATUS_OK;
}

/* Closes the current cell — the environment frame's own list — into the
 * row in progress. The cell's range spans its constructs, or collapses
 * to the point where content would have started. */
static tex_core_status txc_environment_cell(txc_arena *arena, txc_frame *frame, tex_core_error *error) {
    txc_array_cell *cell = txc_arena_alloc(arena, sizeof(txc_array_cell));
    if (cell == NULL) {
        return txc_fail(error, TEX_CORE_STATUS_ALLOCATION_FAILED, NULL, "allocation failed");
    }
    cell->list = frame->list;
    if (frame->list.count > 0) {
        cell->range.begin = frame->list.head->range.begin;
        cell->range.end = frame->list.tail->range.end;
    } else {
        cell->range.begin = frame->env_cell_start;
        cell->range.end = frame->env_cell_start;
    }
    cell->next = NULL;
    if (frame->env_cells_tail != NULL) {
        frame->env_cells_tail->next = cell;
    } else {
        frame->env_cells = cell;
    }
    frame->env_cells_tail = cell;
    frame->env_cell_count += 1;
    frame->list.head = NULL;
    frame->list.tail = NULL;
    frame->list.count = 0;
    return TEX_CORE_STATUS_OK;
}

/* Closes the current cell and the row in progress. */
static tex_core_status txc_environment_row_close(txc_arena *arena, txc_frame *frame, tex_core_error *error) {
    tex_core_status status = txc_environment_cell(arena, frame, error);
    if (status != TEX_CORE_STATUS_OK) {
        return status;
    }
    txc_array_row *row = txc_arena_alloc(arena, sizeof(txc_array_row));
    if (row == NULL) {
        return txc_fail(error, TEX_CORE_STATUS_ALLOCATION_FAILED, NULL, "allocation failed");
    }
    row->cells = frame->env_cells;
    row->cell_count = frame->env_cell_count;
    row->next = NULL;
    if (frame->env_rows_tail != NULL) {
        frame->env_rows_tail->next = row;
    } else {
        frame->env_rows = row;
    }
    frame->env_rows_tail = row;
    frame->env_row_count += 1;
    frame->env_cells = NULL;
    frame->env_cells_tail = NULL;
    frame->env_cell_count = 0;
    return TEX_CORE_STATUS_OK;
}

tex_core_status txc_parse(
    txc_arena *arena,
    const uint8_t *source,
    size_t length,
    tex_core_mode mode,
    txc_list *list,
    tex_core_error *error
) {
    list->head = NULL;
    list->tail = NULL;
    list->count = 0;

    txc_scanner scanner;
    txc_scanner_init(&scanner, source, length);

    txc_frame root;
    txc_frame_init(&root, TXC_FRAME_ROOT, NULL);
    txc_frame *frame = &root;
    size_t depth = 0;
    bool math = mode != TEX_CORE_MODE_DOCUMENT;

    for (;;) {
        txc_token token;
        tex_core_status status = txc_scan(&scanner, &token, error);
        if (status != TEX_CORE_STATUS_OK) {
            return status;
        }

        switch (token.kind) {
        case TXC_TOKEN_END:
            if (frame->delim_wait != TXC_DELIM_WAIT_NONE) {
                return txc_fail(error, TEX_CORE_STATUS_UNSUPPORTED, &frame->delim_command, "missing delimiter");
            }
            if (frame->collect.kind != TXC_COLLECT_NONE) {
                return txc_fail(
                    error,
                    TEX_CORE_STATUS_UNSUPPORTED,
                    &frame->collect.command,
                    "missing %s argument",
                    txc_argument_noun(frame)
                );
            }
            if (frame->role == TXC_FRAME_INDEX) {
                tex_core_range open = {frame->open, frame->open + 1};
                return txc_fail(error, TEX_CORE_STATUS_UNSUPPORTED, &open, "unclosed radical index");
            }
            if (frame->pending != TXC_PENDING_NONE) {
                tex_core_range mark = {frame->pending_mark, frame->pending_mark + 1};
                return txc_fail(
                    error,
                    TEX_CORE_STATUS_UNSUPPORTED,
                    &mark,
                    "missing %s argument",
                    txc_script_noun(frame->pending)
                );
            }
            if (frame->role == TXC_FRAME_FENCED) {
                return txc_fail(error, TEX_CORE_STATUS_UNSUPPORTED, &frame->fence_command, "missing \\right");
            }
            if (frame->role == TXC_FRAME_CELL) {
                return txc_fail(
                    error,
                    TEX_CORE_STATUS_UNSUPPORTED,
                    &frame->env_begin,
                    "missing \\end{%s}",
                    frame->env->name
                );
            }
            if (frame->role != TXC_FRAME_ROOT) {
                tex_core_range open = {frame->open, frame->open + 1};
                return txc_fail(error, TEX_CORE_STATUS_UNSUPPORTED, &open, "unclosed group");
            }
            *list = frame->list;
            return TEX_CORE_STATUS_OK;

        case TXC_TOKEN_PAR:
            return txc_fail(error, TEX_CORE_STATUS_UNSUPPORTED, &token.range, "unsupported paragraph break");

        case TXC_TOKEN_SPACE: {
            /* TeX ignores blanks in math mode, including between a script
             * mark and its argument — but \text content keeps its word
             * spaces. */
            if (math && !frame->text_mode) {
                break;
            }
            txc_item *item = txc_append(arena, &frame->list);
            if (item == NULL) {
                return txc_fail(error, TEX_CORE_STATUS_ALLOCATION_FAILED, NULL, "allocation failed");
            }
            item->kind = TXC_ITEM_SPACE;
            item->space = TXC_SPACE_WORD;
            item->range = token.range;
            break;
        }

        case TXC_TOKEN_CHARACTER: {
            uint32_t codepoint = token.codepoint;
            if (math && frame->text_mode && codepoint == '}') {
                /* Closing a \text group: deliver its document-rule list
                 * into the pending style switch. */
                txc_frame *closed = frame;
                frame = frame->parent;
                depth -= 1;
                tex_core_range whole = {closed->open, token.range.end};
                txc_construct construct = {NULL, &closed->list, NULL, NULL, NULL, NULL, NULL, NULL};
                status = txc_deliver(arena, frame, &construct, TXC_ATOM_ORD, whole, error);
                if (status != TEX_CORE_STATUS_OK) {
                    return status;
                }
                break;
            }
            if (math && !frame->text_mode) {
                if (frame->delim_wait != TXC_DELIM_WAIT_NONE) {
                    const txc_delimiter *delimiter = txc_delimiter_character(codepoint);
                    if (delimiter == NULL) {
                        return txc_fail(error, TEX_CORE_STATUS_UNSUPPORTED, &token.range, "missing delimiter");
                    }
                    status = txc_delimiter_arrived(arena, &frame, &depth, delimiter, token.range, error);
                    if (status != TEX_CORE_STATUS_OK) {
                        return status;
                    }
                    break;
                }
                if (codepoint == '[' && frame->collect.kind == TXC_COLLECT_RADICAL && !frame->collect.second) {
                    /* The LaTeX optional index: one leading bracket right
                     * after \sqrt opens it; anywhere else `[` stays an
                     * ordinary Open atom. */
                    if (depth == TXC_GROUP_DEPTH_LIMIT) {
                        return txc_fail(error, TEX_CORE_STATUS_UNSUPPORTED, &token.range, "group nesting too deep");
                    }
                    txc_frame *inner = txc_arena_alloc(arena, sizeof(txc_frame));
                    if (inner == NULL) {
                        return txc_fail(error, TEX_CORE_STATUS_ALLOCATION_FAILED, NULL, "allocation failed");
                    }
                    txc_frame_init(inner, TXC_FRAME_INDEX, frame);
                    inner->styled = frame->styled;
                    inner->face_family = frame->face_family;
                    inner->open = token.range.begin;
                    frame = inner;
                    depth += 1;
                    break;
                }
                if (codepoint == ']' && frame->role == TXC_FRAME_INDEX) {
                    if (frame->collect.kind != TXC_COLLECT_NONE) {
                        return txc_fail(
                            error,
                            TEX_CORE_STATUS_UNSUPPORTED,
                            &token.range,
                            "missing %s argument",
                            txc_argument_noun(frame)
                        );
                    }
                    if (frame->pending != TXC_PENDING_NONE) {
                        return txc_fail(
                            error,
                            TEX_CORE_STATUS_UNSUPPORTED,
                            &token.range,
                            "missing %s argument",
                            txc_script_noun(frame->pending)
                        );
                    }
                    txc_frame *closed = frame;
                    frame = frame->parent;
                    depth -= 1;
                    frame->collect.radical->index.kind = TXC_FIELD_LIST;
                    frame->collect.radical->index.list = closed->list;
                    frame->collect.radical->index.range.begin = closed->open;
                    frame->collect.radical->index.range.end = token.range.end;
                    frame->collect.second = true;
                    break;
                }
                if (codepoint == '{') {
                    /* Group nesting is bounded so that layout's recursion
                     * over nuclei and script fields has a proven stack
                     * budget; the bound is public error surface. */
                    if (depth == TXC_GROUP_DEPTH_LIMIT) {
                        return txc_fail(error, TEX_CORE_STATUS_UNSUPPORTED, &token.range, "group nesting too deep");
                    }
                    txc_frame *inner = txc_arena_alloc(arena, sizeof(txc_frame));
                    if (inner == NULL) {
                        return txc_fail(error, TEX_CORE_STATUS_ALLOCATION_FAILED, NULL, "allocation failed");
                    }
                    txc_frame_init(inner, TXC_FRAME_GROUP, frame);
                    inner->open = token.range.begin;
                    inner->text_mode = frame->collect.kind == TXC_COLLECT_STYLE && frame->collect.face == TXC_FACE_TEXT;
                    /* The brace group scopes the face: a style switch's own
                     * argument takes the switch's alphabet, any other group
                     * inherits the enclosing face. */
                    if (frame->collect.kind == TXC_COLLECT_STYLE) {
                        inner->styled = frame->collect.face != TXC_FACE_TEXT;
                        inner->face_family =
                            inner->styled ? TXC_FACE_FAMILIES[frame->collect.face] : TEX_CORE_FAMILY_MAIN;
                    } else {
                        inner->styled = frame->styled;
                        inner->face_family = frame->face_family;
                    }
                    if (frame->pending != TXC_PENDING_NONE) {
                        inner->role = TXC_FRAME_SCRIPT;
                        inner->target = frame->pending_target;
                        inner->script = frame->pending;
                        inner->mark = frame->pending_mark;
                        frame->pending = TXC_PENDING_NONE;
                        frame->pending_target = NULL;
                    }
                    frame = inner;
                    depth += 1;
                    break;
                }
                if (codepoint == '}') {
                    if (frame->collect.kind != TXC_COLLECT_NONE) {
                        return txc_fail(
                            error,
                            TEX_CORE_STATUS_UNSUPPORTED,
                            &token.range,
                            "missing %s argument",
                            txc_argument_noun(frame)
                        );
                    }
                    if (frame->pending != TXC_PENDING_NONE) {
                        return txc_fail(
                            error,
                            TEX_CORE_STATUS_UNSUPPORTED,
                            &token.range,
                            "missing %s argument",
                            txc_script_noun(frame->pending)
                        );
                    }
                    if (frame->role == TXC_FRAME_FENCED) {
                        return txc_fail(error, TEX_CORE_STATUS_UNSUPPORTED, &token.range, "missing \\right");
                    }
                    if (frame->role == TXC_FRAME_INDEX) {
                        return txc_fail(error, TEX_CORE_STATUS_UNSUPPORTED, &token.range, "unclosed radical index");
                    }
                    if (frame->role == TXC_FRAME_CELL) {
                        return txc_fail(
                            error,
                            TEX_CORE_STATUS_UNSUPPORTED,
                            &token.range,
                            "missing \\end{%s}",
                            frame->env->name
                        );
                    }
                    if (frame->role == TXC_FRAME_ROOT) {
                        return txc_fail(error, TEX_CORE_STATUS_UNSUPPORTED, &token.range, "unmatched closing brace");
                    }
                    txc_frame *closed = frame;
                    frame = frame->parent;
                    depth -= 1;
                    if (closed->role == TXC_FRAME_SCRIPT) {
                        txc_field *field = txc_script_field(closed->target, closed->script);
                        field->kind = TXC_FIELD_LIST;
                        field->list = closed->list;
                        field->range.begin = closed->mark;
                        field->range.end = token.range.end;
                        closed->target->range.end = token.range.end;
                        if (closed->script == TXC_PENDING_SUB && closed->target->sup.kind == TXC_FIELD_EMPTY) {
                            closed->target->sub_first = true;
                        }
                    } else {
                        tex_core_range range = {closed->open, token.range.end};
                        txc_construct construct = {NULL, &closed->list, NULL, NULL, NULL, NULL, NULL, NULL};
                        status = txc_deliver(arena, frame, &construct, TXC_ATOM_ORD, range, error);
                        if (status != TEX_CORE_STATUS_OK) {
                            return status;
                        }
                    }
                    break;
                }
                if (codepoint == '^' || codepoint == '_') {
                    txc_pending pending = codepoint == '^' ? TXC_PENDING_SUP : TXC_PENDING_SUB;
                    if (frame->collect.kind != TXC_COLLECT_NONE) {
                        return txc_fail(
                            error,
                            TEX_CORE_STATUS_UNSUPPORTED,
                            &token.range,
                            "missing %s argument",
                            txc_argument_noun(frame)
                        );
                    }
                    if (frame->pending != TXC_PENDING_NONE) {
                        return txc_fail(
                            error,
                            TEX_CORE_STATUS_UNSUPPORTED,
                            &token.range,
                            "missing %s argument",
                            txc_script_noun(frame->pending)
                        );
                    }
                    txc_item *target = frame->list.tail;
                    if (target == NULL || target->kind != TXC_ITEM_ATOM) {
                        /* TeX gives a script with nothing to attach to an
                         * empty-nucleus Ord atom of its own. */
                        tex_core_range at = {token.range.begin, token.range.begin};
                        target = txc_append(arena, &frame->list);
                        if (target == NULL) {
                            return txc_fail(error, TEX_CORE_STATUS_ALLOCATION_FAILED, NULL, "allocation failed");
                        }
                        target->kind = TXC_ITEM_ATOM;
                        target->atom_class = TXC_ATOM_ORD;
                        txc_field_reset(&target->nucleus, at.begin);
                        txc_field_reset(&target->sup, at.begin);
                        txc_field_reset(&target->sub, at.begin);
                        target->sub_first = false;
                        target->op_limits = TXC_LIMITS_DISPLAY;
                        target->range = at;
                    }
                    if (txc_script_field(target, pending)->kind != TXC_FIELD_EMPTY) {
                        return txc_fail(
                            error,
                            TEX_CORE_STATUS_UNSUPPORTED,
                            &token.range,
                            "double %s",
                            txc_script_noun(pending)
                        );
                    }
                    frame->pending = pending;
                    frame->pending_target = target;
                    frame->pending_mark = token.range.begin;
                    break;
                }
                if (codepoint == '&') {
                    /* An alignment tab ends the current cell; anywhere
                     * but directly inside an environment cell it is a
                     * structured error, as TeX's misplaced-tab complaint. */
                    if (frame->collect.kind != TXC_COLLECT_NONE) {
                        return txc_fail(
                            error,
                            TEX_CORE_STATUS_UNSUPPORTED,
                            &token.range,
                            "missing %s argument",
                            txc_argument_noun(frame)
                        );
                    }
                    if (frame->pending != TXC_PENDING_NONE) {
                        return txc_fail(
                            error,
                            TEX_CORE_STATUS_UNSUPPORTED,
                            &token.range,
                            "missing %s argument",
                            txc_script_noun(frame->pending)
                        );
                    }
                    if (frame->role != TXC_FRAME_CELL) {
                        return txc_fail(error, TEX_CORE_STATUS_UNSUPPORTED, &token.range, "misplaced alignment tab");
                    }
                    if (frame->env_cell_count + 1 == TXC_MAX_MATRIX_COLUMNS) {
                        return txc_fail(error, TEX_CORE_STATUS_UNSUPPORTED, &token.range, "extra alignment tab");
                    }
                    status = txc_environment_cell(arena, frame, error);
                    if (status != TEX_CORE_STATUS_OK) {
                        return status;
                    }
                    frame->env_cell_start = token.range.end;
                    break;
                }
                if (!txc_reserved(codepoint)) {
                    txc_math_glyph glyph;
                    if (txc_math_classify(codepoint, &glyph)) {
                        txc_construct construct = {&glyph, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
                        status = txc_deliver(arena, frame, &construct, glyph.atom_class, token.range, error);
                        if (status != TEX_CORE_STATUS_OK) {
                            return status;
                        }
                        break;
                    }
                }
            } else if (!txc_reserved(codepoint)) {
                if (txc_letter_codepoint(codepoint) || txc_digit_codepoint(codepoint) || codepoint == '.' ||
                    codepoint == ',') {
                    if (txc_append_atom(
                            arena,
                            &frame->list,
                            TXC_ATOM_ORD,
                            codepoint,
                            TEX_CORE_STYLE_UPRIGHT,
                            TEX_CORE_FAMILY_MAIN,
                            token.range
                        ) == NULL) {
                        return txc_fail(error, TEX_CORE_STATUS_ALLOCATION_FAILED, NULL, "allocation failed");
                    }
                    break;
                }
            }
            return txc_fail(
                error,
                TEX_CORE_STATUS_UNSUPPORTED,
                &token.range,
                "unsupported character U+%04X",
                (unsigned)codepoint
            );
        }

        case TXC_TOKEN_CONTROL: {
            if (math && !frame->text_mode && frame->delim_wait != TXC_DELIM_WAIT_NONE) {
                const txc_delimiter *delimiter = txc_delimiter_command(token.name, token.name_length);
                if (delimiter == NULL) {
                    return txc_fail(error, TEX_CORE_STATUS_UNSUPPORTED, &token.range, "missing delimiter");
                }
                status = txc_delimiter_arrived(arena, &frame, &depth, delimiter, token.range, error);
                if (status != TEX_CORE_STATUS_OK) {
                    return status;
                }
                break;
            }
            const txc_spacing_command *command = txc_spacing(token.name, token.name_length);
            if (command != NULL) {
                if (frame->collect.kind != TXC_COLLECT_NONE) {
                    return txc_fail(
                        error,
                        TEX_CORE_STATUS_UNSUPPORTED,
                        &token.range,
                        "missing %s argument",
                        txc_argument_noun(frame)
                    );
                }
                if (frame->pending != TXC_PENDING_NONE) {
                    return txc_fail(
                        error,
                        TEX_CORE_STATUS_UNSUPPORTED,
                        &token.range,
                        "missing %s argument",
                        txc_script_noun(frame->pending)
                    );
                }
                txc_item *item = txc_append(arena, &frame->list);
                if (item == NULL) {
                    return txc_fail(error, TEX_CORE_STATUS_ALLOCATION_FAILED, NULL, "allocation failed");
                }
                item->kind = TXC_ITEM_SPACE;
                item->space = command->space;
                item->range = token.range;
                break;
            }
            /* Symbol and fraction commands are math-only: in document
             * mode and inside \text they stay structured errors until
             * the text surface (milestone M3) arrives, exactly like
             * TeX's missing-$ complaint. */
            if (math && !frame->text_mode) {
                if (token.name_length == 1 && token.name[0] == '\\') {
                    /* A row terminator ends the current cell and row;
                     * anywhere but directly inside an environment cell it
                     * is a structured error, like a misplaced \cr. */
                    if (frame->collect.kind != TXC_COLLECT_NONE) {
                        return txc_fail(
                            error,
                            TEX_CORE_STATUS_UNSUPPORTED,
                            &token.range,
                            "missing %s argument",
                            txc_argument_noun(frame)
                        );
                    }
                    if (frame->pending != TXC_PENDING_NONE) {
                        return txc_fail(
                            error,
                            TEX_CORE_STATUS_UNSUPPORTED,
                            &token.range,
                            "missing %s argument",
                            txc_script_noun(frame->pending)
                        );
                    }
                    if (frame->role != TXC_FRAME_CELL) {
                        return txc_fail(error, TEX_CORE_STATUS_UNSUPPORTED, &token.range, "misplaced \\\\");
                    }
                    status = txc_environment_row_close(arena, frame, error);
                    if (status != TEX_CORE_STATUS_OK) {
                        return status;
                    }
                    frame->env_cell_start = token.range.end;
                    /* LaTeX's \\ scans ahead past blanks: a `*` is the
                     * no-page-break form — meaningless before pagination,
                     * consumed so the output matches LaTeX exactly — and
                     * a `[` opens the optional row-space dimension, which
                     * stays a structured error until dimensions land. */
                    txc_scanner lookahead = scanner;
                    txc_token peek;
                    do {
                        status = txc_scan(&lookahead, &peek, error);
                        if (status != TEX_CORE_STATUS_OK) {
                            return status;
                        }
                    } while (peek.kind == TXC_TOKEN_SPACE);
                    if (peek.kind == TXC_TOKEN_CHARACTER && peek.codepoint == '*') {
                        scanner = lookahead;
                        frame->env_cell_start = peek.range.end;
                        do {
                            status = txc_scan(&lookahead, &peek, error);
                            if (status != TEX_CORE_STATUS_OK) {
                                return status;
                            }
                        } while (peek.kind == TXC_TOKEN_SPACE);
                    }
                    if (peek.kind == TXC_TOKEN_CHARACTER && peek.codepoint == '[') {
                        return txc_fail(error, TEX_CORE_STATUS_UNSUPPORTED, &peek.range, "unsupported row spacing");
                    }
                    break;
                }
                const txc_command_entry *entry = txc_command_lookup(token.name, token.name_length);
                if (entry != NULL && entry->kind == TXC_COMMAND_FRACTION) {
                    const txc_fraction_command *fraction_command = &TXC_FRACTION_COMMANDS[entry->index];
                    /* A fraction command where a fraction argument is
                     * required is not a legal argument: undelimited
                     * arguments are a character, a symbol command, or a
                     * braced group. */
                    if (frame->collect.kind != TXC_COLLECT_NONE) {
                        return txc_fail(
                            error,
                            TEX_CORE_STATUS_UNSUPPORTED,
                            &token.range,
                            "missing %s argument",
                            txc_argument_noun(frame)
                        );
                    }
                    txc_fraction *fraction = txc_arena_alloc(arena, sizeof(txc_fraction));
                    if (fraction == NULL) {
                        return txc_fail(error, TEX_CORE_STATUS_ALLOCATION_FAILED, NULL, "allocation failed");
                    }
                    fraction->style = fraction_command->style;
                    fraction->binom = fraction_command->binom;
                    fraction->command = token.range;
                    txc_field_reset(&fraction->num, token.range.end);
                    txc_field_reset(&fraction->den, token.range.end);
                    status = txc_collect_begin(arena, frame, TXC_COLLECT_FRACTION, TXC_ATOM_INNER, token.range, error);
                    if (status != TEX_CORE_STATUS_OK) {
                        return status;
                    }
                    frame->collect.fraction = fraction;
                    break;
                }
                if (entry != NULL && entry->kind == TXC_COMMAND_LEFT) {
                    if (frame->collect.kind != TXC_COLLECT_NONE) {
                        return txc_fail(
                            error,
                            TEX_CORE_STATUS_UNSUPPORTED,
                            &token.range,
                            "missing %s argument",
                            txc_argument_noun(frame)
                        );
                    }
                    if (frame->pending != TXC_PENDING_NONE) {
                        return txc_fail(
                            error,
                            TEX_CORE_STATUS_UNSUPPORTED,
                            &token.range,
                            "missing %s argument",
                            txc_script_noun(frame->pending)
                        );
                    }
                    frame->delim_wait = TXC_DELIM_WAIT_LEFT;
                    frame->delim_command = token.range;
                    break;
                }
                if (entry != NULL && entry->kind == TXC_COMMAND_RIGHT) {
                    if (frame->collect.kind != TXC_COLLECT_NONE) {
                        return txc_fail(
                            error,
                            TEX_CORE_STATUS_UNSUPPORTED,
                            &token.range,
                            "missing %s argument",
                            txc_argument_noun(frame)
                        );
                    }
                    if (frame->pending != TXC_PENDING_NONE) {
                        return txc_fail(
                            error,
                            TEX_CORE_STATUS_UNSUPPORTED,
                            &token.range,
                            "missing %s argument",
                            txc_script_noun(frame->pending)
                        );
                    }
                    if (frame->role != TXC_FRAME_FENCED) {
                        return txc_fail(error, TEX_CORE_STATUS_UNSUPPORTED, &token.range, "unmatched \\right");
                    }
                    frame->delim_wait = TXC_DELIM_WAIT_RIGHT;
                    frame->delim_command = token.range;
                    break;
                }
                if (entry != NULL && entry->kind == TXC_COMMAND_BEGIN) {
                    /* \begin is not a legal script or command argument —
                     * undelimited arguments are a character, a symbol
                     * command, or a braced group, exactly as for \left. */
                    if (frame->collect.kind != TXC_COLLECT_NONE) {
                        return txc_fail(
                            error,
                            TEX_CORE_STATUS_UNSUPPORTED,
                            &token.range,
                            "missing %s argument",
                            txc_argument_noun(frame)
                        );
                    }
                    if (frame->pending != TXC_PENDING_NONE) {
                        return txc_fail(
                            error,
                            TEX_CORE_STATUS_UNSUPPORTED,
                            &token.range,
                            "missing %s argument",
                            txc_script_noun(frame->pending)
                        );
                    }
                    const uint8_t *name;
                    size_t name_length;
                    tex_core_range whole;
                    status = txc_environment_name(&scanner, source, token.range, &name, &name_length, &whole, error);
                    if (status != TEX_CORE_STATUS_OK) {
                        return status;
                    }
                    const txc_environment_row *environment = txc_environment_find(name, name_length);
                    if (environment == NULL) {
                        char label[64];
                        txc_command_label(name, name_length, label, sizeof(label));
                        return txc_fail(
                            error,
                            TEX_CORE_STATUS_UNSUPPORTED,
                            &whole,
                            "unsupported environment %s",
                            label
                        );
                    }
                    /* Environments count toward the same nesting bound as
                     * braces and fences. */
                    if (depth == TXC_GROUP_DEPTH_LIMIT) {
                        return txc_fail(error, TEX_CORE_STATUS_UNSUPPORTED, &whole, "group nesting too deep");
                    }
                    txc_frame *inner = txc_arena_alloc(arena, sizeof(txc_frame));
                    if (inner == NULL) {
                        return txc_fail(error, TEX_CORE_STATUS_ALLOCATION_FAILED, NULL, "allocation failed");
                    }
                    txc_frame_init(inner, TXC_FRAME_CELL, frame);
                    inner->styled = frame->styled;
                    inner->face_family = frame->face_family;
                    inner->open = token.range.begin;
                    inner->env = environment;
                    inner->env_begin = whole;
                    inner->env_cell_start = whole.end;
                    frame = inner;
                    depth += 1;
                    break;
                }
                if (entry != NULL && entry->kind == TXC_COMMAND_END) {
                    if (frame->collect.kind != TXC_COLLECT_NONE) {
                        return txc_fail(
                            error,
                            TEX_CORE_STATUS_UNSUPPORTED,
                            &token.range,
                            "missing %s argument",
                            txc_argument_noun(frame)
                        );
                    }
                    if (frame->pending != TXC_PENDING_NONE) {
                        return txc_fail(
                            error,
                            TEX_CORE_STATUS_UNSUPPORTED,
                            &token.range,
                            "missing %s argument",
                            txc_script_noun(frame->pending)
                        );
                    }
                    const uint8_t *name;
                    size_t name_length;
                    tex_core_range whole;
                    status = txc_environment_name(&scanner, source, token.range, &name, &name_length, &whole, error);
                    if (status != TEX_CORE_STATUS_OK) {
                        return status;
                    }
                    char label[64];
                    txc_command_label(name, name_length, label, sizeof(label));
                    /* \end acts only directly inside an environment cell,
                     * exactly as \right acts only inside its fence. */
                    if (frame->role != TXC_FRAME_CELL) {
                        return txc_fail(error, TEX_CORE_STATUS_UNSUPPORTED, &whole, "unmatched \\end{%s}", label);
                    }
                    if (strlen(frame->env->name) != name_length || memcmp(frame->env->name, name, name_length) != 0) {
                        return txc_fail(error, TEX_CORE_STATUS_UNSUPPORTED, &whole, "mismatched \\end{%s}", label);
                    }
                    /* A trailing \\ contributes no row: the final row is
                     * dropped when nothing at all followed the last
                     * terminator — TeX's \crcr exactly. An environment
                     * whose whole body is blank has zero rows. */
                    if (frame->env_cell_count > 0 || frame->list.count > 0) {
                        status = txc_environment_row_close(arena, frame, error);
                        if (status != TEX_CORE_STATUS_OK) {
                            return status;
                        }
                    }
                    txc_array *array = txc_arena_alloc(arena, sizeof(txc_array));
                    if (array == NULL) {
                        return txc_fail(error, TEX_CORE_STATUS_ALLOCATION_FAILED, NULL, "allocation failed");
                    }
                    const txc_environment_row *environment = frame->env;
                    array->delimited = environment->delimited;
                    array->left = environment->left;
                    array->right = environment->right;
                    array->begin = frame->env_begin;
                    array->end = whole;
                    array->rows = frame->env_rows;
                    array->row_count = frame->env_row_count;
                    tex_core_range range = {frame->open, whole.end};
                    frame = frame->parent;
                    depth -= 1;
                    txc_construct construct = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, array};
                    status = txc_deliver(
                        arena,
                        frame,
                        &construct,
                        environment->delimited ? TXC_ATOM_INNER : TXC_ATOM_ORD,
                        range,
                        error
                    );
                    if (status != TEX_CORE_STATUS_OK) {
                        return status;
                    }
                    break;
                }
                {
                    const txc_big_op *big_op =
                        entry != NULL && entry->kind == TXC_COMMAND_BIG_OP ? &TXC_BIG_OPS[entry->index] : NULL;
                    const txc_function_name *function =
                        entry != NULL && entry->kind == TXC_COMMAND_FUNCTION ? &TXC_FUNCTION_NAMES[entry->index] : NULL;
                    if (big_op != NULL || function != NULL) {
                        /* Build the Op atom, then route it like a braced
                         * group so it stays an atom inside script and
                         * argument fields. */
                        txc_item *op = txc_arena_alloc(arena, sizeof(txc_item));
                        if (op == NULL) {
                            return txc_fail(error, TEX_CORE_STATUS_ALLOCATION_FAILED, NULL, "allocation failed");
                        }
                        op->kind = TXC_ITEM_ATOM;
                        op->atom_class = TXC_ATOM_OP;
                        txc_field_reset(&op->nucleus, token.range.begin);
                        txc_field_reset(&op->sup, token.range.end);
                        txc_field_reset(&op->sub, token.range.end);
                        op->sub_first = false;
                        op->range = token.range;
                        op->next = NULL;
                        if (big_op != NULL) {
                            op->op_limits = big_op->limits;
                            op->nucleus.kind = TXC_FIELD_CHAR;
                            op->nucleus.character.codepoint = big_op->codepoint;
                            op->nucleus.character.style = TEX_CORE_STYLE_UPRIGHT;
                            op->nucleus.range = token.range;
                        } else {
                            op->op_limits = function->limits;
                            op->nucleus.kind = TXC_FIELD_LIST;
                            op->nucleus.range = token.range;
                            for (const char *letter = function->spelling; *letter != '\0'; letter++) {
                                if (*letter == '\'') {
                                    txc_item *space = txc_append(arena, &op->nucleus.list);
                                    if (space == NULL) {
                                        return txc_fail(
                                            error,
                                            TEX_CORE_STATUS_ALLOCATION_FAILED,
                                            NULL,
                                            "allocation failed"
                                        );
                                    }
                                    space->kind = TXC_ITEM_SPACE;
                                    space->space = TXC_SPACE_THIN;
                                    space->range = token.range;
                                    continue;
                                }
                                if (txc_append_atom(
                                        arena,
                                        &op->nucleus.list,
                                        TXC_ATOM_ORD,
                                        (uint32_t)(unsigned char)*letter,
                                        TEX_CORE_STYLE_UPRIGHT,
                                        TEX_CORE_FAMILY_MAIN,
                                        token.range
                                    ) == NULL) {
                                    return txc_fail(
                                        error,
                                        TEX_CORE_STATUS_ALLOCATION_FAILED,
                                        NULL,
                                        "allocation failed"
                                    );
                                }
                            }
                        }
                        if (frame->collect.kind != TXC_COLLECT_NONE || frame->pending != TXC_PENDING_NONE) {
                            txc_list wrapped = {op, op, 1};
                            txc_construct construct = {NULL, &wrapped, NULL, NULL, NULL, NULL, NULL, NULL};
                            status = txc_deliver(arena, frame, &construct, TXC_ATOM_ORD, token.range, error);
                            if (status != TEX_CORE_STATUS_OK) {
                                return status;
                            }
                        } else {
                            if (frame->list.tail != NULL) {
                                frame->list.tail->next = op;
                            } else {
                                frame->list.head = op;
                            }
                            frame->list.tail = op;
                            frame->list.count += 1;
                        }
                        break;
                    }
                }
                if (entry != NULL && (entry->kind == TXC_COMMAND_LIMITS || entry->kind == TXC_COMMAND_NOLIMITS)) {
                    bool wants_limits = entry->kind == TXC_COMMAND_LIMITS;
                    if (frame->collect.kind != TXC_COLLECT_NONE) {
                        return txc_fail(
                            error,
                            TEX_CORE_STATUS_UNSUPPORTED,
                            &token.range,
                            "missing %s argument",
                            txc_argument_noun(frame)
                        );
                    }
                    if (frame->pending != TXC_PENDING_NONE) {
                        return txc_fail(
                            error,
                            TEX_CORE_STATUS_UNSUPPORTED,
                            &token.range,
                            "missing %s argument",
                            txc_script_noun(frame->pending)
                        );
                    }
                    txc_item *target = frame->list.tail;
                    if (target == NULL || target->kind != TXC_ITEM_ATOM || target->atom_class != TXC_ATOM_OP) {
                        return txc_fail(
                            error,
                            TEX_CORE_STATUS_UNSUPPORTED,
                            &token.range,
                            "misplaced \\%s",
                            wants_limits ? "limits" : "nolimits"
                        );
                    }
                    target->op_limits = wants_limits ? TXC_LIMITS_ALWAYS : TXC_LIMITS_NEVER;
                    target->range.end = token.range.end;
                    break;
                }
                {
                    const txc_style_command *style_command =
                        entry != NULL && entry->kind == TXC_COMMAND_STYLE ? &TXC_STYLE_COMMANDS[entry->index] : NULL;
                    if (style_command != NULL) {
                        if (frame->collect.kind != TXC_COLLECT_NONE) {
                            return txc_fail(
                                error,
                                TEX_CORE_STATUS_UNSUPPORTED,
                                &token.range,
                                "missing %s argument",
                                txc_argument_noun(frame)
                            );
                        }
                        status = txc_collect_begin(arena, frame, TXC_COLLECT_STYLE, TXC_ATOM_ORD, token.range, error);
                        if (status != TEX_CORE_STATUS_OK) {
                            return status;
                        }
                        frame->collect.face = style_command->face;
                        break;
                    }
                }
                {
                    const txc_accent_command *accent_command =
                        entry != NULL && entry->kind == TXC_COMMAND_ACCENT ? &TXC_ACCENT_COMMANDS[entry->index] : NULL;
                    if (accent_command != NULL) {
                        /* A bare accent command is not a legal argument:
                         * nested accents need braces (\hat{\bar{x}}). */
                        if (frame->collect.kind != TXC_COLLECT_NONE) {
                            return txc_fail(
                                error,
                                TEX_CORE_STATUS_UNSUPPORTED,
                                &token.range,
                                "missing %s argument",
                                txc_argument_noun(frame)
                            );
                        }
                        txc_accent *accent = txc_arena_alloc(arena, sizeof(txc_accent));
                        if (accent == NULL) {
                            return txc_fail(error, TEX_CORE_STATUS_ALLOCATION_FAILED, NULL, "allocation failed");
                        }
                        accent->codepoint = accent_command->codepoint;
                        accent->wide = accent_command->wide;
                        accent->command = token.range;
                        txc_field_reset(&accent->argument, token.range.end);
                        status = txc_collect_begin(arena, frame, TXC_COLLECT_ACCENT, TXC_ATOM_ORD, token.range, error);
                        if (status != TEX_CORE_STATUS_OK) {
                            return status;
                        }
                        frame->collect.accent = accent;
                        break;
                    }
                }
                if (entry != NULL && entry->kind == TXC_COMMAND_SQRT) {
                    if (frame->collect.kind != TXC_COLLECT_NONE) {
                        return txc_fail(
                            error,
                            TEX_CORE_STATUS_UNSUPPORTED,
                            &token.range,
                            "missing %s argument",
                            txc_argument_noun(frame)
                        );
                    }
                    txc_radical *radical = txc_arena_alloc(arena, sizeof(txc_radical));
                    if (radical == NULL) {
                        return txc_fail(error, TEX_CORE_STATUS_ALLOCATION_FAILED, NULL, "allocation failed");
                    }
                    radical->command = token.range;
                    txc_field_reset(&radical->index, token.range.end);
                    txc_field_reset(&radical->argument, token.range.end);
                    status = txc_collect_begin(arena, frame, TXC_COLLECT_RADICAL, TXC_ATOM_ORD, token.range, error);
                    if (status != TEX_CORE_STATUS_OK) {
                        return status;
                    }
                    frame->collect.radical = radical;
                    break;
                }
                if (entry != NULL && entry->kind == TXC_COMMAND_SIZE) {
                    const txc_size_command *size_command = &TXC_SIZE_COMMANDS[entry->index];
                    frame->delim_wait = TXC_DELIM_WAIT_EXPLICIT;
                    frame->delim_command = token.range;
                    frame->delim_class = size_command->atom_class;
                    frame->delim_size = size_command->size;
                    break;
                }
                if (entry != NULL && entry->kind == TXC_COMMAND_SYMBOL) {
                    const txc_math_symbol *symbol = &TXC_MATH_SYMBOLS[entry->index];
                    txc_math_glyph glyph = {symbol->atom_class, symbol->codepoint, symbol->style, TEX_CORE_FAMILY_MAIN};
                    txc_construct construct = {&glyph, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
                    status = txc_deliver(arena, frame, &construct, glyph.atom_class, token.range, error);
                    if (status != TEX_CORE_STATUS_OK) {
                        return status;
                    }
                    break;
                }
            }
            char label[64];
            txc_command_label(token.name, token.name_length, label, sizeof(label));
            return txc_fail(error, TEX_CORE_STATUS_UNSUPPORTED, &token.range, "unsupported command \\%s", label);
        }
        }
    }
}
