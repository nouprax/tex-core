#include "token.h"

#include <stdbool.h>

#include "error.h"
#include "utf8.h"

void txc_scanner_init(txc_scanner *scanner, const uint8_t *source, size_t length) {
    scanner->source = source;
    scanner->length = length;
    scanner->position = 0;
}

static bool txc_letter(uint8_t byte) { return (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z'); }

static bool txc_blank(uint8_t byte) { return byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r'; }

static tex_core_status txc_scan_invalid(txc_scanner *scanner, size_t invalid_length, tex_core_error *error) {
    tex_core_range range = {scanner->position, scanner->position + invalid_length};
    /* Park the scanner at the end so a caller that keeps scanning sees END
     * instead of the same error forever. */
    scanner->position = scanner->length;
    return txc_fail(error, TEX_CORE_STATUS_INVALID_UTF8, &range, "invalid UTF-8 sequence");
}

/* Collapses a whitespace run into one SPACE token, or PAR when the run
 * contains a blank line (two or more line ends, TeX's \par boundary).
 * Carriage returns pair with line feeds so CRLF sources scan like LF ones. */
static void txc_scan_blank(txc_scanner *scanner, txc_token *token) {
    size_t start = scanner->position;
    size_t line_ends = 0;
    while (scanner->position < scanner->length && txc_blank(scanner->source[scanner->position])) {
        uint8_t byte = scanner->source[scanner->position];
        if (byte == '\n') {
            line_ends += 1;
        } else if (byte == '\r') {
            line_ends += 1;
            if (scanner->position + 1 < scanner->length && scanner->source[scanner->position + 1] == '\n') {
                scanner->position += 1;
            }
        }
        scanner->position += 1;
    }
    token->kind = line_ends >= 2 ? TXC_TOKEN_PAR : TXC_TOKEN_SPACE;
    token->range.begin = start;
    token->range.end = scanner->position;
}

static tex_core_status txc_scan_control(txc_scanner *scanner, txc_token *token, tex_core_error *error) {
    size_t start = scanner->position;
    scanner->position += 1;
    if (scanner->position >= scanner->length) {
        tex_core_range range = {start, scanner->length};
        return txc_fail(error, TEX_CORE_STATUS_UNSUPPORTED, &range, "incomplete control sequence at end of input");
    }

    token->kind = TXC_TOKEN_CONTROL;
    token->name = scanner->source + scanner->position;
    if (txc_letter(scanner->source[scanner->position])) {
        /* Control word: a maximal run of letters; TeX then discards
         * following blanks (state S). The range excludes the discarded
         * blanks. */
        size_t name_start = scanner->position;
        while (scanner->position < scanner->length && txc_letter(scanner->source[scanner->position])) {
            scanner->position += 1;
        }
        token->name_length = scanner->position - name_start;
        token->range.begin = start;
        token->range.end = scanner->position;
        while (scanner->position < scanner->length && txc_blank(scanner->source[scanner->position])) {
            scanner->position += 1;
        }
        return TEX_CORE_STATUS_OK;
    }

    /* Control symbol: exactly one codepoint. */
    uint32_t codepoint;
    size_t invalid_length;
    if (!txc_utf8_next(scanner->source, scanner->length, &scanner->position, &codepoint, &invalid_length)) {
        return txc_scan_invalid(scanner, invalid_length, error);
    }
    token->name_length = scanner->position - (start + 1);
    token->range.begin = start;
    token->range.end = scanner->position;
    return TEX_CORE_STATUS_OK;
}

tex_core_status txc_scan(txc_scanner *scanner, txc_token *token, tex_core_error *error) {
    token->codepoint = 0;
    token->name = NULL;
    token->name_length = 0;

    if (scanner->position >= scanner->length) {
        token->kind = TXC_TOKEN_END;
        token->range.begin = scanner->length;
        token->range.end = scanner->length;
        return TEX_CORE_STATUS_OK;
    }

    uint8_t byte = scanner->source[scanner->position];
    if (byte == '\\') {
        return txc_scan_control(scanner, token, error);
    }
    if (txc_blank(byte)) {
        txc_scan_blank(scanner, token);
        return TEX_CORE_STATUS_OK;
    }

    size_t start = scanner->position;
    uint32_t codepoint;
    size_t invalid_length;
    if (!txc_utf8_next(scanner->source, scanner->length, &scanner->position, &codepoint, &invalid_length)) {
        return txc_scan_invalid(scanner, invalid_length, error);
    }
    token->kind = TXC_TOKEN_CHARACTER;
    token->codepoint = codepoint;
    token->range.begin = start;
    token->range.end = scanner->position;
    return TEX_CORE_STATUS_OK;
}
