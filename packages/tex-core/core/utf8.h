/* Strict UTF-8 decoding (RFC 3629): overlong forms, surrogates, values past
 * U+10FFFF, and truncated sequences are all rejected. Invalid bytes are a
 * structured error, never silently replaced (plan section 5.1). */

#ifndef TXC_UTF8_H
#define TXC_UTF8_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Decodes the codepoint at `position`. On success advances `position` past
 * the sequence, stores the codepoint, and returns true. On failure leaves
 * `position` at the start of the invalid sequence, stores the number of
 * offending bytes (at least 1, the maximal subpart) in `invalid_length`,
 * and returns false. */
bool txc_utf8_next(const uint8_t *source, size_t length, size_t *position, uint32_t *codepoint, size_t *invalid_length);

#endif
