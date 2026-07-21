#ifndef TEX_CORE_KOTLIN_BRIDGE_H
#define TEX_CORE_KOTLIN_BRIDGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Compiles one source and serializes the outcome into a TXC1 payload: a
 * status record for failed compiles, the preorder render tree for
 * successful ones. The payload is an in-process transport between this
 * bridge and the Kotlin wire decoder — never a public serialization
 * format. Returns false only when the payload itself could not be
 * allocated; structured compile failures travel inside the payload.
 * Payloads are caller-owned and released with tex_core_kotlin_free. */

bool tex_core_kotlin_compile(const uint8_t *source, size_t length, uint32_t mode, uint8_t **output,
                             size_t *output_length);
void tex_core_kotlin_free(uint8_t *output);

#endif
