#include "parse.h"

#include <stdio.h>
#include <string.h>

#include "error.h"
#include "token.h"

/* Characters with special TeX meaning that the engine does not cover yet.
 * Rejecting them keeps the supported surface honest: nothing is silently
 * skipped or demoted to a literal (plan section 5.1). */
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
    if (list->tail != NULL) {
        list->tail->next = item;
    } else {
        list->head = item;
    }
    list->tail = item;
    list->count += 1;
    return item;
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
    item->codepoint = codepoint;
    item->style = style;
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

/* Appends the atom for one character token, or fails when the character is
 * outside the supported surface. Document mode typesets the text ordinaries
 * (letters, digits, period, comma); math modes add the classed math
 * characters, with letters on the math italic face per TeX's default
 * \mathcode assignments. */
static tex_core_status txc_parse_character(
    txc_arena *arena,
    txc_list *list,
    const txc_token *token,
    tex_core_mode mode,
    tex_core_error *error
) {
    uint32_t codepoint = token->codepoint;
    if (!txc_reserved(codepoint)) {
        if (mode == TEX_CORE_MODE_DOCUMENT) {
            if (txc_letter_codepoint(codepoint) || txc_digit_codepoint(codepoint) || codepoint == '.' ||
                codepoint == ',') {
                if (txc_append_atom(arena, list, TXC_ATOM_ORD, codepoint, TEX_CORE_STYLE_UPRIGHT, token->range) ==
                    NULL) {
                    return txc_fail(error, TEX_CORE_STATUS_ALLOCATION_FAILED, NULL, "allocation failed");
                }
                return TEX_CORE_STATUS_OK;
            }
        } else {
            if (txc_letter_codepoint(codepoint)) {
                if (txc_append_atom(arena, list, TXC_ATOM_ORD, codepoint, TEX_CORE_STYLE_ITALIC, token->range) ==
                    NULL) {
                    return txc_fail(error, TEX_CORE_STATUS_ALLOCATION_FAILED, NULL, "allocation failed");
                }
                return TEX_CORE_STATUS_OK;
            }
            if (txc_digit_codepoint(codepoint) || codepoint == '.') {
                if (txc_append_atom(arena, list, TXC_ATOM_ORD, codepoint, TEX_CORE_STYLE_UPRIGHT, token->range) ==
                    NULL) {
                    return txc_fail(error, TEX_CORE_STATUS_ALLOCATION_FAILED, NULL, "allocation failed");
                }
                return TEX_CORE_STATUS_OK;
            }
            const txc_math_character *character = txc_math_character_find(codepoint);
            if (character != NULL) {
                if (txc_append_atom(
                        arena,
                        list,
                        character->atom_class,
                        character->codepoint,
                        TEX_CORE_STYLE_UPRIGHT,
                        token->range
                    ) == NULL) {
                    return txc_fail(error, TEX_CORE_STATUS_ALLOCATION_FAILED, NULL, "allocation failed");
                }
                return TEX_CORE_STATUS_OK;
            }
        }
    }
    return txc_fail(
        error,
        TEX_CORE_STATUS_UNSUPPORTED,
        &token->range,
        "unsupported character U+%04X",
        (unsigned)codepoint
    );
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

    for (;;) {
        txc_token token;
        tex_core_status status = txc_scan(&scanner, &token, error);
        if (status != TEX_CORE_STATUS_OK) {
            return status;
        }

        switch (token.kind) {
        case TXC_TOKEN_END:
            return TEX_CORE_STATUS_OK;

        case TXC_TOKEN_PAR:
            return txc_fail(error, TEX_CORE_STATUS_UNSUPPORTED, &token.range, "unsupported paragraph break");

        case TXC_TOKEN_SPACE: {
            /* TeX ignores blanks in math mode. */
            if (mode != TEX_CORE_MODE_DOCUMENT) {
                break;
            }
            txc_item *item = txc_append(arena, list);
            if (item == NULL) {
                return txc_fail(error, TEX_CORE_STATUS_ALLOCATION_FAILED, NULL, "allocation failed");
            }
            item->kind = TXC_ITEM_SPACE;
            item->space = TXC_SPACE_WORD;
            item->range = token.range;
            break;
        }

        case TXC_TOKEN_CHARACTER: {
            status = txc_parse_character(arena, list, &token, mode, error);
            if (status != TEX_CORE_STATUS_OK) {
                return status;
            }
            break;
        }

        case TXC_TOKEN_CONTROL: {
            const txc_spacing_command *command = txc_spacing(token.name, token.name_length);
            if (command != NULL) {
                txc_item *item = txc_append(arena, list);
                if (item == NULL) {
                    return txc_fail(error, TEX_CORE_STATUS_ALLOCATION_FAILED, NULL, "allocation failed");
                }
                item->kind = TXC_ITEM_SPACE;
                item->space = command->space;
                item->range = token.range;
                break;
            }
            /* Symbol commands are math-only: in document mode they stay
             * structured errors until the text surface (milestone M3)
             * arrives, exactly like TeX's missing-$ complaint. */
            if (mode != TEX_CORE_MODE_DOCUMENT) {
                const txc_math_symbol *symbol = txc_math_symbol_find(token.name, token.name_length);
                if (symbol != NULL) {
                    if (txc_append_atom(
                            arena,
                            list,
                            symbol->atom_class,
                            symbol->codepoint,
                            symbol->style,
                            token.range
                        ) == NULL) {
                        return txc_fail(error, TEX_CORE_STATUS_ALLOCATION_FAILED, NULL, "allocation failed");
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
