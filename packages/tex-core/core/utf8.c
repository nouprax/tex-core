#include "utf8.h"

bool txc_utf8_next(
    const uint8_t *source,
    size_t length,
    size_t *position,
    uint32_t *codepoint,
    size_t *invalid_length
) {
    size_t start = *position;
    uint8_t lead = source[start];

    size_t expected;
    uint32_t value;
    uint32_t minimum;
    if (lead < 0x80) {
        *codepoint = lead;
        *position = start + 1;
        return true;
    } else if ((lead & 0xE0) == 0xC0) {
        expected = 2;
        value = lead & 0x1Fu;
        minimum = 0x80;
    } else if ((lead & 0xF0) == 0xE0) {
        expected = 3;
        value = lead & 0x0Fu;
        minimum = 0x800;
    } else if ((lead & 0xF8) == 0xF0) {
        expected = 4;
        value = lead & 0x07u;
        minimum = 0x10000;
    } else {
        *invalid_length = 1;
        return false;
    }

    size_t index = 1;
    for (; index < expected; index++) {
        if (start + index >= length || (source[start + index] & 0xC0) != 0x80) {
            *invalid_length = index;
            return false;
        }
        value = (value << 6) | (source[start + index] & 0x3Fu);
    }

    if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
        *invalid_length = expected;
        return false;
    }

    *codepoint = value;
    *position = start + expected;
    return true;
}
