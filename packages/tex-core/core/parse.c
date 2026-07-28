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

static const txc_math_symbol *txc_math_symbol_find(const uint8_t *name, size_t name_length) {
    for (size_t index = 0; index < sizeof(TXC_MATH_SYMBOLS) / sizeof(TXC_MATH_SYMBOLS[0]); index++) {
        const txc_math_symbol *symbol = &TXC_MATH_SYMBOLS[index];
        if (strlen(symbol->name) == name_length && memcmp(symbol->name, name, name_length) == 0) {
            return symbol;
        }
    }
    return NULL;
}

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

static const txc_big_op *txc_big_op_find(const uint8_t *name, size_t name_length) {
    for (size_t index = 0; index < sizeof(TXC_BIG_OPS) / sizeof(TXC_BIG_OPS[0]); index++) {
        const txc_big_op *op = &TXC_BIG_OPS[index];
        if (strlen(op->name) == name_length && memcmp(op->name, name, name_length) == 0) {
            return op;
        }
    }
    return NULL;
}

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

static const txc_function_name *txc_function_find(const uint8_t *name, size_t name_length) {
    for (size_t index = 0; index < sizeof(TXC_FUNCTION_NAMES) / sizeof(TXC_FUNCTION_NAMES[0]); index++) {
        const txc_function_name *function = &TXC_FUNCTION_NAMES[index];
        if (strlen(function->name) == name_length && memcmp(function->name, name, name_length) == 0) {
            return function;
        }
    }
    return NULL;
}

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

static const txc_accent_command *txc_accent_find(const uint8_t *name, size_t name_length) {
    for (size_t index = 0; index < sizeof(TXC_ACCENT_COMMANDS) / sizeof(TXC_ACCENT_COMMANDS[0]); index++) {
        const txc_accent_command *command = &TXC_ACCENT_COMMANDS[index];
        if (strlen(command->name) == name_length && memcmp(command->name, name, name_length) == 0) {
            return command;
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

static const txc_size_command *txc_size_find(const uint8_t *name, size_t name_length) {
    for (size_t index = 0; index < sizeof(TXC_SIZE_COMMANDS) / sizeof(TXC_SIZE_COMMANDS[0]); index++) {
        const txc_size_command *command = &TXC_SIZE_COMMANDS[index];
        if (strlen(command->name) == name_length && memcmp(command->name, name, name_length) == 0) {
            return command;
        }
    }
    return NULL;
}

static const txc_fraction_command *txc_fraction_find(const uint8_t *name, size_t name_length) {
    for (size_t index = 0; index < sizeof(TXC_FRACTION_COMMANDS) / sizeof(TXC_FRACTION_COMMANDS[0]); index++) {
        const txc_fraction_command *command = &TXC_FRACTION_COMMANDS[index];
        if (strlen(command->name) == name_length && memcmp(command->name, name, name_length) == 0) {
            return command;
        }
    }
    return NULL;
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

static void txc_field_reset(txc_field *field, size_t at) {
    field->kind = TXC_FIELD_EMPTY;
    field->codepoint = 0;
    field->style = TEX_CORE_STYLE_UPRIGHT;
    field->list.head = NULL;
    field->list.tail = NULL;
    field->list.count = 0;
    field->fraction = NULL;
    field->range.begin = at;
    field->range.end = at;
}

static txc_item *txc_append_atom(
    txc_arena *arena,
    txc_list *list,
    txc_atom_class atom_class,
    uint32_t codepoint,
    tex_core_style style,
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
    item->nucleus.codepoint = codepoint;
    item->nucleus.style = style;
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
 * (whose class is irrelevant — a field is not an atom). */
typedef struct txc_math_glyph {
    txc_atom_class atom_class;
    uint32_t codepoint;
    tex_core_style style;
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
} txc_construct;

/* Fills one field from a delivered construct. */
static void txc_field_fill(txc_field *field, const txc_construct *construct, tex_core_range range) {
    if (construct->glyph != NULL) {
        field->kind = TXC_FIELD_CHAR;
        field->codepoint = construct->glyph->codepoint;
        field->style = construct->glyph->style;
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
    TXC_FRAME_INDEX = 4
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

typedef struct txc_frame {
    txc_list list;
    txc_frame_role role;
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
    /* The fraction whose arguments this frame is collecting, if any:
     * delivered constructs fill the numerator, then the denominator. On
     * completion the fraction becomes `fraction_item`'s nucleus — the
     * Inner atom appended at the command — or, when the command itself
     * was a script argument, the captured script field. */
    txc_fraction *fraction;
    bool fraction_denominator;
    txc_item *fraction_item;
    txc_item *fraction_target;
    txc_pending fraction_script;
    size_t fraction_mark;
    /* The radical whose parts this frame is collecting, if any:
     * a leading `[` opens the index once, and the next construct is the
     * radicand. Completion mirrors the fraction machinery. */
    txc_radical *radical;
    bool radical_index_taken;
    txc_item *radical_item;
    txc_item *radical_target;
    txc_pending radical_script;
    size_t radical_mark;
    /* The accent whose argument this frame is collecting, if any.
     * Completion mirrors the radical machinery. */
    txc_accent *accent;
    txc_item *accent_item;
    txc_item *accent_target;
    txc_pending accent_script;
    size_t accent_mark;
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
    struct txc_frame *parent;
} txc_frame;

static void txc_frame_init(txc_frame *frame, txc_frame_role role, txc_frame *parent) {
    frame->list.head = NULL;
    frame->list.tail = NULL;
    frame->list.count = 0;
    frame->role = role;
    frame->open = 0;
    frame->target = NULL;
    frame->script = TXC_PENDING_NONE;
    frame->mark = 0;
    frame->pending = TXC_PENDING_NONE;
    frame->pending_target = NULL;
    frame->pending_mark = 0;
    frame->fraction = NULL;
    frame->fraction_denominator = false;
    frame->fraction_item = NULL;
    frame->fraction_target = NULL;
    frame->fraction_script = TXC_PENDING_NONE;
    frame->fraction_mark = 0;
    frame->radical = NULL;
    frame->radical_index_taken = false;
    frame->radical_item = NULL;
    frame->radical_target = NULL;
    frame->radical_script = TXC_PENDING_NONE;
    frame->radical_mark = 0;
    frame->accent = NULL;
    frame->accent_item = NULL;
    frame->accent_target = NULL;
    frame->accent_script = TXC_PENDING_NONE;
    frame->accent_mark = 0;
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
    if (frame->accent != NULL) {
        return "accent";
    }
    if (frame->radical != NULL) {
        return "radical";
    }
    return frame->fraction_denominator ? "denominator" : "numerator";
}

/* Completes the fraction in progress on `frame` once its denominator has
 * arrived: the construct becomes the destination captured at the command —
 * the appended Inner atom's nucleus or a script field. */
static void txc_fraction_complete(txc_frame *frame, size_t end) {
    txc_fraction *fraction = frame->fraction;
    tex_core_range range = {fraction->command.begin, end};
    txc_construct construct = {NULL, NULL, fraction, NULL, NULL, NULL, NULL};
    if (frame->fraction_item != NULL) {
        txc_item *item = frame->fraction_item;
        txc_field_fill(&item->nucleus, &construct, range);
        txc_field_reset(&item->sup, end);
        txc_field_reset(&item->sub, end);
        item->range = range;
    } else {
        txc_item *target = frame->fraction_target;
        txc_field *field = txc_script_field(target, frame->fraction_script);
        txc_field_fill(field, &construct, range);
        field->range.begin = frame->fraction_mark;
        target->range.end = end;
        if (frame->fraction_script == TXC_PENDING_SUB && target->sup.kind == TXC_FIELD_EMPTY) {
            target->sub_first = true;
        }
    }
    frame->fraction = NULL;
    frame->fraction_denominator = false;
    frame->fraction_item = NULL;
    frame->fraction_target = NULL;
}

/* Completes the radical in progress on `frame` once its radicand has
 * arrived: the construct becomes the destination captured at the
 * command — the appended Ord atom's nucleus or a script field. */
static void txc_radical_complete(txc_frame *frame, size_t end) {
    txc_radical *radical = frame->radical;
    tex_core_range range = {radical->command.begin, end};
    txc_construct construct = {NULL, NULL, NULL, NULL, NULL, radical, NULL};
    if (frame->radical_item != NULL) {
        txc_item *item = frame->radical_item;
        txc_field_fill(&item->nucleus, &construct, range);
        txc_field_reset(&item->sup, end);
        txc_field_reset(&item->sub, end);
        item->range = range;
    } else {
        txc_item *target = frame->radical_target;
        txc_field *field = txc_script_field(target, frame->radical_script);
        txc_field_fill(field, &construct, range);
        field->range.begin = frame->radical_mark;
        target->range.end = end;
        if (frame->radical_script == TXC_PENDING_SUB && target->sup.kind == TXC_FIELD_EMPTY) {
            target->sub_first = true;
        }
    }
    frame->radical = NULL;
    frame->radical_index_taken = false;
    frame->radical_item = NULL;
    frame->radical_target = NULL;
}

/* Completes the accent in progress on `frame` once its argument has
 * arrived. */
static void txc_accent_complete(txc_frame *frame, size_t end) {
    txc_accent *accent = frame->accent;
    tex_core_range range = {accent->command.begin, end};
    txc_construct construct = {NULL, NULL, NULL, NULL, NULL, NULL, accent};
    if (frame->accent_item != NULL) {
        txc_item *item = frame->accent_item;
        txc_field_fill(&item->nucleus, &construct, range);
        txc_field_reset(&item->sup, end);
        txc_field_reset(&item->sub, end);
        item->range = range;
    } else {
        txc_item *target = frame->accent_target;
        txc_field *field = txc_script_field(target, frame->accent_script);
        txc_field_fill(field, &construct, range);
        field->range.begin = frame->accent_mark;
        target->range.end = end;
        if (frame->accent_script == TXC_PENDING_SUB && target->sup.kind == TXC_FIELD_EMPTY) {
            target->sub_first = true;
        }
    }
    frame->accent = NULL;
    frame->accent_item = NULL;
    frame->accent_target = NULL;
}

/* Delivers one parsed construct — a character, a symbol command, a
 * closed group list, a sized delimiter, or a closed fence — into the
 * frame: as the pending accent, radical, or fraction argument when one
 * is being collected, as the pending script field when one is pending,
 * or as a new atom of `atom_class`. `range` is the construct's own
 * range; script fields extend it back to their mark. */
static tex_core_status txc_deliver(
    txc_arena *arena,
    txc_frame *frame,
    const txc_construct *construct,
    txc_atom_class atom_class,
    tex_core_range range,
    tex_core_error *error
) {
    if (frame->accent != NULL) {
        txc_field_fill(&frame->accent->argument, construct, range);
        txc_accent_complete(frame, range.end);
        return TEX_CORE_STATUS_OK;
    }
    if (frame->radical != NULL) {
        txc_field_fill(&frame->radical->argument, construct, range);
        txc_radical_complete(frame, range.end);
        return TEX_CORE_STATUS_OK;
    }
    if (frame->fraction != NULL || frame->radical != NULL || frame->accent != NULL) {
        txc_field *field = frame->fraction_denominator ? &frame->fraction->den : &frame->fraction->num;
        txc_field_fill(field, construct, range);
        if (frame->fraction_denominator) {
            txc_fraction_complete(frame, range.end);
        } else {
            frame->fraction_denominator = true;
        }
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
        if (txc_append_atom(arena, &frame->list, glyph->atom_class, glyph->codepoint, glyph->style, range) == NULL) {
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
        txc_construct construct = {NULL, NULL, NULL, NULL, fenced, NULL, NULL};
        return txc_deliver(arena, *frame_slot, &construct, TXC_ATOM_INNER, whole, error);
    }
    txc_sized_delimiter sized = {*delimiter, frame->delim_size};
    tex_core_range whole = {frame->delim_command.begin, range.end};
    txc_construct construct = {NULL, NULL, NULL, &sized, NULL, NULL, NULL};
    return txc_deliver(arena, frame, &construct, frame->delim_class, whole, error);
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
            if (frame->fraction != NULL || frame->radical != NULL || frame->accent != NULL) {
                tex_core_range at;
                if (frame->accent != NULL) {
                    at = frame->accent->command;
                } else if (frame->radical != NULL) {
                    at = frame->radical->command;
                } else {
                    at = frame->fraction->command;
                }
                return txc_fail(
                    error,
                    TEX_CORE_STATUS_UNSUPPORTED,
                    &at,
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
             * mark and its argument. */
            if (math) {
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
            if (math) {
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
                if (codepoint == '[' && frame->radical != NULL && !frame->radical_index_taken) {
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
                    inner->open = token.range.begin;
                    frame = inner;
                    depth += 1;
                    break;
                }
                if (codepoint == ']' && frame->role == TXC_FRAME_INDEX) {
                    if (frame->fraction != NULL || frame->radical != NULL || frame->accent != NULL) {
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
                    frame->radical->index.kind = TXC_FIELD_LIST;
                    frame->radical->index.list = closed->list;
                    frame->radical->index.range.begin = closed->open;
                    frame->radical->index.range.end = token.range.end;
                    frame->radical_index_taken = true;
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
                    if (frame->fraction != NULL || frame->radical != NULL || frame->accent != NULL) {
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
                        txc_construct construct = {NULL, &closed->list, NULL, NULL, NULL, NULL, NULL};
                        status = txc_deliver(arena, frame, &construct, TXC_ATOM_ORD, range, error);
                        if (status != TEX_CORE_STATUS_OK) {
                            return status;
                        }
                    }
                    break;
                }
                if (codepoint == '^' || codepoint == '_') {
                    txc_pending pending = codepoint == '^' ? TXC_PENDING_SUP : TXC_PENDING_SUB;
                    if (frame->fraction != NULL || frame->radical != NULL || frame->accent != NULL) {
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
                if (!txc_reserved(codepoint)) {
                    txc_math_glyph glyph;
                    if (txc_math_classify(codepoint, &glyph)) {
                        txc_construct construct = {&glyph, NULL, NULL, NULL, NULL, NULL, NULL};
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
            if (math && frame->delim_wait != TXC_DELIM_WAIT_NONE) {
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
                if (frame->fraction != NULL || frame->radical != NULL || frame->accent != NULL) {
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
            /* Symbol and fraction commands are math-only: in document mode
             * they stay structured errors until the text surface
             * (milestone M3) arrives, exactly like TeX's missing-$
             * complaint. */
            if (math) {
                const txc_fraction_command *fraction_command = txc_fraction_find(token.name, token.name_length);
                if (fraction_command != NULL) {
                    /* A fraction command where a fraction argument is
                     * required is not a legal argument: undelimited
                     * arguments are a character, a symbol command, or a
                     * braced group. */
                    if (frame->fraction != NULL || frame->radical != NULL || frame->accent != NULL) {
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
                    frame->fraction = fraction;
                    frame->fraction_denominator = false;
                    if (frame->pending != TXC_PENDING_NONE) {
                        /* The fraction is itself a script argument
                         * (x^\frac{1}{2}): capture the destination and
                         * fill it on completion. */
                        frame->fraction_item = NULL;
                        frame->fraction_target = frame->pending_target;
                        frame->fraction_script = frame->pending;
                        frame->fraction_mark = frame->pending_mark;
                        frame->pending = TXC_PENDING_NONE;
                        frame->pending_target = NULL;
                    } else {
                        txc_item *item = txc_append(arena, &frame->list);
                        if (item == NULL) {
                            return txc_fail(error, TEX_CORE_STATUS_ALLOCATION_FAILED, NULL, "allocation failed");
                        }
                        item->kind = TXC_ITEM_ATOM;
                        item->atom_class = TXC_ATOM_INNER;
                        txc_field_reset(&item->nucleus, token.range.begin);
                        txc_field_reset(&item->sup, token.range.end);
                        txc_field_reset(&item->sub, token.range.end);
                        item->sub_first = false;
                        item->op_limits = TXC_LIMITS_DISPLAY;
                        item->op_limits = TXC_LIMITS_DISPLAY;
                        item->op_limits = TXC_LIMITS_DISPLAY;
                        item->range = token.range;
                        frame->fraction_item = item;
                        frame->fraction_target = NULL;
                    }
                    break;
                }
                if (token.name_length == 4 && memcmp(token.name, "left", 4) == 0) {
                    if (frame->fraction != NULL || frame->radical != NULL || frame->accent != NULL) {
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
                if (token.name_length == 5 && memcmp(token.name, "right", 5) == 0) {
                    if (frame->fraction != NULL || frame->radical != NULL || frame->accent != NULL) {
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
                {
                    const txc_big_op *big_op = txc_big_op_find(token.name, token.name_length);
                    const txc_function_name *function =
                        big_op == NULL ? txc_function_find(token.name, token.name_length) : NULL;
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
                            op->nucleus.codepoint = big_op->codepoint;
                            op->nucleus.style = TEX_CORE_STYLE_UPRIGHT;
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
                        if (frame->radical != NULL || frame->fraction != NULL || frame->pending != TXC_PENDING_NONE) {
                            txc_list wrapped = {op, op, 1};
                            txc_construct construct = {NULL, &wrapped, NULL, NULL, NULL, NULL, NULL};
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
                if ((token.name_length == 6 && memcmp(token.name, "limits", 6) == 0) ||
                    (token.name_length == 8 && memcmp(token.name, "nolimits", 8) == 0)) {
                    bool wants_limits = token.name_length == 6;
                    if (frame->fraction != NULL || frame->radical != NULL || frame->accent != NULL) {
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
                    const txc_accent_command *accent_command = txc_accent_find(token.name, token.name_length);
                    if (accent_command != NULL) {
                        txc_accent *accent = txc_arena_alloc(arena, sizeof(txc_accent));
                        if (accent == NULL) {
                            return txc_fail(error, TEX_CORE_STATUS_ALLOCATION_FAILED, NULL, "allocation failed");
                        }
                        accent->codepoint = accent_command->codepoint;
                        accent->wide = accent_command->wide;
                        accent->command = token.range;
                        txc_field_reset(&accent->argument, token.range.end);
                        frame->accent = accent;
                        if (frame->pending != TXC_PENDING_NONE) {
                            frame->accent_item = NULL;
                            frame->accent_target = frame->pending_target;
                            frame->accent_script = frame->pending;
                            frame->accent_mark = frame->pending_mark;
                            frame->pending = TXC_PENDING_NONE;
                            frame->pending_target = NULL;
                        } else {
                            txc_item *item = txc_append(arena, &frame->list);
                            if (item == NULL) {
                                return txc_fail(error, TEX_CORE_STATUS_ALLOCATION_FAILED, NULL, "allocation failed");
                            }
                            item->kind = TXC_ITEM_ATOM;
                            item->atom_class = TXC_ATOM_ORD;
                            txc_field_reset(&item->nucleus, token.range.begin);
                            txc_field_reset(&item->sup, token.range.end);
                            txc_field_reset(&item->sub, token.range.end);
                            item->sub_first = false;
                            item->op_limits = TXC_LIMITS_DISPLAY;
                            item->range = token.range;
                            frame->accent_item = item;
                            frame->accent_target = NULL;
                        }
                        break;
                    }
                }
                if (token.name_length == 4 && memcmp(token.name, "sqrt", 4) == 0) {
                    if (frame->fraction != NULL || frame->radical != NULL || frame->accent != NULL) {
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
                    frame->radical = radical;
                    frame->radical_index_taken = false;
                    if (frame->pending != TXC_PENDING_NONE) {
                        /* The radical is itself a script argument
                         * (x^\sqrt{2}): capture the destination and fill
                         * it on completion. */
                        frame->radical_item = NULL;
                        frame->radical_target = frame->pending_target;
                        frame->radical_script = frame->pending;
                        frame->radical_mark = frame->pending_mark;
                        frame->pending = TXC_PENDING_NONE;
                        frame->pending_target = NULL;
                    } else {
                        txc_item *item = txc_append(arena, &frame->list);
                        if (item == NULL) {
                            return txc_fail(error, TEX_CORE_STATUS_ALLOCATION_FAILED, NULL, "allocation failed");
                        }
                        item->kind = TXC_ITEM_ATOM;
                        item->atom_class = TXC_ATOM_ORD;
                        txc_field_reset(&item->nucleus, token.range.begin);
                        txc_field_reset(&item->sup, token.range.end);
                        txc_field_reset(&item->sub, token.range.end);
                        item->sub_first = false;
                        item->op_limits = TXC_LIMITS_DISPLAY;
                        item->op_limits = TXC_LIMITS_DISPLAY;
                        item->op_limits = TXC_LIMITS_DISPLAY;
                        item->range = token.range;
                        frame->radical_item = item;
                        frame->radical_target = NULL;
                    }
                    break;
                }
                const txc_size_command *size_command = txc_size_find(token.name, token.name_length);
                if (size_command != NULL) {
                    frame->delim_wait = TXC_DELIM_WAIT_EXPLICIT;
                    frame->delim_command = token.range;
                    frame->delim_class = size_command->atom_class;
                    frame->delim_size = size_command->size;
                    break;
                }
                const txc_math_symbol *symbol = txc_math_symbol_find(token.name, token.name_length);
                if (symbol != NULL) {
                    txc_math_glyph glyph = {symbol->atom_class, symbol->codepoint, symbol->style};
                    txc_construct construct = {&glyph, NULL, NULL, NULL, NULL, NULL, NULL};
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
