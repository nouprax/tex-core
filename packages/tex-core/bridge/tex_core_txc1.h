/* Shared TXC1 payload writer over the public C facade only — the
 * in-process transport between the native engine and the Kotlin and ES
 * wire decoders, never a public serialization format. Layout (all
 * integers and doubles little-endian):
 *
 *   u32 status                          tex_core_status
 *   status != OK:
 *     u32 has_range; u64 begin; u64 end
 *     u32 message_length; message bytes (ASCII, no NUL)
 *   status == OK:
 *     u32 node_count                    preorder walk
 *     per node (92 bytes):
 *       u32 kind                        1 hbox, 2 glyph, 3 kern, 4 rule
 *       f64 x, y, width, ascent, descent, italic
 *       u32 codepoint; u32 style; u32 family   zero outside glyphs
 *       f64 size                        zero outside glyphs
 *       u64 src_begin; u64 src_end
 *       u32 child_count
 *
 * The record is fixed-shape so the decoders stay a single loop, and the
 * payload is allocated exactly once from the counted tree size. */

#ifndef TEX_CORE_TXC1_H
#define TEX_CORE_TXC1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Compiles one source and serializes the outcome into a TXC1 payload: a
 * status record for failed compiles, the preorder render tree for
 * successful ones. Returns false only when the payload itself could not
 * be allocated; structured compile failures travel inside the payload.
 * Payloads are caller-owned and released with tex_core_txc1_free. */
bool tex_core_txc1_compile(
    const uint8_t *source,
    size_t length,
    uint32_t mode,
    uint8_t **output,
    size_t *output_length
);
void tex_core_txc1_free(uint8_t *output);

#endif
