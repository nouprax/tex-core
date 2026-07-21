/* Internal allocation seam and arena. Every engine allocation funnels
 * through txc_malloc/txc_realloc so that correctness tests can inject
 * allocation failures and prove the error paths leak nothing. */

#ifndef TXC_MEMORY_H
#define TXC_MEMORY_H

#include <stdbool.h>
#include <stddef.h>

void *txc_malloc(size_t size);
void *txc_realloc(void *block, size_t size);
void txc_free(void *block);

/* Test-only injection hook, not part of the public API or ABI: the next
 * `remaining` allocations succeed and every later one fails; negative
 * disables injection (the default). This default-off atomic counter is the
 * one process-global in the engine; it never varies public behavior outside
 * the allocation-failure suites. */
void txc_allocation_limit(long remaining);

/* Chunked bump allocator owning every node of one render tree, so success
 * hands over a single allocation graph and failure releases it in one call. */
typedef struct txc_arena_chunk txc_arena_chunk;

typedef struct txc_arena {
    txc_arena_chunk *head;
} txc_arena;

void txc_arena_init(txc_arena *arena);
/* Returns max_align_t-aligned zeroed storage, or NULL on allocation
 * failure; the arena stays valid and releasable either way. */
void *txc_arena_alloc(txc_arena *arena, size_t size);
void txc_arena_release(txc_arena *arena);

#endif
