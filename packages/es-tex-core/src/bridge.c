/* WASM adapter over the shared TXC1 bridge (packages/tex-core/bridge): one
 * exported compile call returning an owned payload object the TypeScript
 * wire decoder reads out of linear memory. The payload format is the
 * in-process transport defined in tex_core_txc1.c — never a public
 * serialization format. */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "tex_core_txc1.h"

typedef struct es_payload {
    uint8_t *data;
    size_t length;
} es_payload;

es_payload *es_compile(const uint8_t *source, size_t length, uint32_t mode) {
    es_payload *payload = malloc(sizeof(es_payload));
    if (payload == NULL) {
        return NULL;
    }
    if (!tex_core_txc1_compile(source, length, mode, &payload->data, &payload->length)) {
        free(payload);
        return NULL;
    }
    return payload;
}

const uint8_t *es_payload_data(const es_payload *payload) { return payload->data; }

size_t es_payload_length(const es_payload *payload) { return payload->length; }

void es_payload_free(es_payload *payload) {
    if (payload != NULL) {
        tex_core_txc1_free(payload->data);
        free(payload);
    }
}
