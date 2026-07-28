#include "memory.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static _Atomic long txc_allocation_countdown = -1;
static _Atomic long txc_allocation_live = 0;

void txc_allocation_limit(long remaining) { atomic_store(&txc_allocation_countdown, remaining); }

long txc_allocation_outstanding(void) { return atomic_load(&txc_allocation_live); }

static bool txc_allocation_should_fail(void) {
    long current = atomic_load(&txc_allocation_countdown);
    while (current >= 0) {
        if (current == 0) {
            return true;
        }
        if (atomic_compare_exchange_weak(&txc_allocation_countdown, &current, current - 1)) {
            return false;
        }
    }
    return false;
}

void *txc_malloc(size_t size) {
    if (txc_allocation_should_fail()) {
        return NULL;
    }
    void *block = malloc(size);
    if (block != NULL) {
        atomic_fetch_add(&txc_allocation_live, 1);
    }
    return block;
}

void *txc_realloc(void *block, size_t size) {
    if (txc_allocation_should_fail()) {
        return NULL;
    }
    void *grown = realloc(block, size);
    if (grown != NULL && block == NULL) {
        atomic_fetch_add(&txc_allocation_live, 1);
    }
    return grown;
}

void txc_free(void *block) {
    if (block != NULL) {
        atomic_fetch_sub(&txc_allocation_live, 1);
    }
    free(block);
}

/* Max-aligned storage unit for arena blocks. A local union instead of
 * max_align_t: MSVC's C mode does not declare max_align_t, and the union's
 * alignment (covering the widest scalar kinds) matches it on the supported
 * toolchains. */
typedef union txc_arena_align {
    long long integer;
    long double floating;
    void *pointer;
} txc_arena_align;

struct txc_arena_chunk {
    txc_arena_chunk *next;
    size_t used;
    size_t capacity;
    txc_arena_align storage[];
};

#define TXC_ARENA_CHUNK_CAPACITY 4096u
/* Refill sizes double from TXC_ARENA_CHUNK_CAPACITY up to this cap, keeping
 * allocator calls logarithmic in document size while bounding the worst-case
 * unused tail of the final chunk. */
#define TXC_ARENA_CHUNK_CAPACITY_LIMIT (1024u * 1024u)

void txc_arena_init(txc_arena *arena) {
    arena->head = NULL;
    arena->next_capacity = TXC_ARENA_CHUNK_CAPACITY;
}

void *txc_arena_alloc(txc_arena *arena, size_t size) {
    size_t alignment = sizeof(txc_arena_align);
    size_t rounded = (size + alignment - 1) / alignment * alignment;
    if (rounded < size) {
        return NULL;
    }
    txc_arena_chunk *chunk = arena->head;
    if (chunk == NULL || chunk->capacity - chunk->used < rounded) {
        size_t capacity = arena->next_capacity;
        /* An oversized request gets its own exactly-sized chunk without
         * advancing the growth schedule. */
        if (rounded > capacity) {
            capacity = rounded;
        } else if (arena->next_capacity < TXC_ARENA_CHUNK_CAPACITY_LIMIT) {
            arena->next_capacity *= 2;
        }
        if (capacity > SIZE_MAX - sizeof(txc_arena_chunk)) {
            return NULL;
        }
        chunk = txc_malloc(sizeof(txc_arena_chunk) + capacity);
        if (chunk == NULL) {
            return NULL;
        }
        chunk->next = arena->head;
        chunk->used = 0;
        chunk->capacity = capacity;
        arena->head = chunk;
    }
    void *block = (char *)chunk->storage + chunk->used;
    chunk->used += rounded;
    memset(block, 0, rounded);
    return block;
}

void txc_arena_release(txc_arena *arena) {
    txc_arena_chunk *chunk = arena->head;
    while (chunk != NULL) {
        txc_arena_chunk *next = chunk->next;
        txc_free(chunk);
        chunk = next;
    }
    arena->head = NULL;
}
