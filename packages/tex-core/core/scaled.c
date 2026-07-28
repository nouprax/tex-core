#include "scaled.h"

txc_scaled txc_em(int32_t fraction, txc_scaled em) {
    /* Floor division toward negative infinity: identical to the arithmetic
     * shift every supported target performs, but written without a signed
     * right shift, whose negative-operand result is implementation-defined
     * in C. */
    int64_t product = (int64_t)fraction * em;
    int64_t quotient = product / 65536;
    if (product % 65536 != 0 && product < 0) {
        quotient -= 1;
    }
    return quotient;
}

size_t txc_scaled_format(txc_scaled scaled, char *buffer) {
    size_t written = 0;
    int64_t value = scaled;
    if (value < 0) {
        buffer[written++] = '-';
        value = -value;
    }

    int64_t integer = value / 65536;
    char digits[20];
    size_t count = 0;
    do {
        digits[count++] = (char)('0' + integer % 10);
        integer /= 10;
    } while (integer > 0);
    while (count > 0) {
        buffer[written++] = digits[--count];
    }
    buffer[written++] = '.';

    /* TeX section 103: emit decimals until the remainder cannot change the
     * value read back, rounding the final digit. */
    int64_t fraction = 10 * (value % 65536) + 5;
    int64_t delta = 10;
    do {
        if (delta > 65536) {
            fraction += 32768 - delta / 2;
        }
        buffer[written++] = (char)('0' + fraction / 65536);
        fraction = 10 * (fraction % 65536);
        delta *= 10;
    } while (fraction > delta);

    buffer[written] = '\0';
    return written;
}
