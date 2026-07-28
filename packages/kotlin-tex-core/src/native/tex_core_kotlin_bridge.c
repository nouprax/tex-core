/* Kotlin adapter over the shared TXC1 transport (packages/tex-core/bridge):
 * the exported names stay tex_core_kotlin_* because the Kotlin/Native
 * cinterop definition and the JNI payload link against them; the payload
 * format and its single-allocation writer live in the shared bridge. */

#include "tex_core_kotlin_bridge.h"

#include "tex_core_txc1.h"

bool tex_core_kotlin_compile(const uint8_t *source, size_t length, uint32_t mode, uint8_t **output,
                             size_t *output_length) {
    return tex_core_txc1_compile(source, length, mode, output, output_length);
}

void tex_core_kotlin_free(uint8_t *output) { tex_core_txc1_free(output); }
