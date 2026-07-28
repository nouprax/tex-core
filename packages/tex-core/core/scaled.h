/* TeX scaled points: 2^16 sp = 1 pt. All layout arithmetic is integer sp so
 * results are identical on every platform; doubles appear only at the public
 * boundary. */

#ifndef TXC_SCALED_H
#define TXC_SCALED_H

#include <stddef.h>
#include <stdint.h>

/* 64-bit so unbounded accumulation over a whole document cannot overflow:
 * every leaf measure is under 2^24 sp (a few ems) and every accumulated
 * term costs at least one parsed item (hundreds of arena bytes), so a sum
 * would need petabytes of input to approach 2^63. 32 bits overflowed on a
 * 64 KiB single-line document. */
typedef int64_t txc_scaled;

/* The text-size em: 10 pt. The script and scriptscript sizes (7 pt and
 * 5 pt) are the layout module's TXC_MATHSIZE ems. */
#define TXC_EM_SP 655360

/* Scales a 16.16 fixed-point em fraction to scaled points at `em`. */
txc_scaled txc_em(int32_t fraction, txc_scaled em);

#define TXC_SCALED_FORMAT_CAPACITY 24

/* Writes the shortest decimal representation that reads back to the same
 * scaled value (Knuth's print_scaled, TeX section 103), NUL-terminated;
 * returns its length. Pure integer arithmetic, byte-deterministic. */
size_t txc_scaled_format(txc_scaled scaled, char *buffer);

#endif
