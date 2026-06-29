#include "types.h"
#include <stdlib.h>
#include <string.h>

/* Arena allocator for burst allocation patterns.
   Used for temporary frame processing where many small allocations
   are made and then all freed at once. */

typedef struct smux_arena_block {
    uint8_t                 *data;
    size_t                   used;
    size_t                   capacity;
    struct smux_arena_block *next;
} smux_arena_block_t;

typedef struct {
    smux_arena_block_t *head;
    smux_arena_block_t *current;
    size_t              default_block_size;
    size_t              total_allocated;
    size_t              total_used;
    int                 block_count;
} smux_arena_t;

static smux_arena_block_t *create_block(size_t size) {
    smux_arena_block_t *block = malloc(sizeof(smux_arena_block_t));
    if (!block) return NULL;
    block->data = malloc(size);
    if (!block->data) {
        free(block);
        return NULL;
    }
    block->capacity = size;
    block->used = 0;
    block->next = NULL;
    return block;
}

smux_arena_t *smux_arena_create(size_t block_size) {
    smux_arena_t *arena = calloc(1, sizeof(smux_arena_t));
    if (!arena) return NULL;
    arena->default_block_size = block_size > 0 ? block_size : 4096;

    smux_arena_block_t *first = create_block(arena->default_block_size);
    if (!first) {
        free(arena);
        return NULL;
    }
    arena->head = first;
    arena->current = first;
    arena->block_count = 1;
    arena->total_allocated = arena->default_block_size;
    return arena;
}

void smux_arena_destroy(smux_arena_t *arena) {
    if (!arena) return;
    smux_arena_block_t *block = arena->head;
    while (block) {
        smux_arena_block_t *next = block->next;
        free(block->data);
        free(block);
        block = next;
    }
    free(arena);
}

void *smux_arena_alloc(smux_arena_t *arena, size_t size) {
    if (!arena || size == 0) return NULL;

    /* Align to 8 bytes */
    size = (size + 7) & ~(size_t)7;

    /* Try current block */
    if (arena->current->used + size <= arena->current->capacity) {
        void *ptr = arena->current->data + arena->current->used;
        arena->current->used += size;
        arena->total_used += size;
        return ptr;
    }

    /* Allocate new block */
    size_t block_size = size > arena->default_block_size ? size : arena->default_block_size;
    smux_arena_block_t *new_block = create_block(block_size);
    if (!new_block) return NULL;

    arena->current->next = new_block;
    arena->current = new_block;
    arena->block_count++;
    arena->total_allocated += block_size;

    void *ptr = new_block->data;
    new_block->used = size;
    arena->total_used += size;
    return ptr;
}

void *smux_arena_calloc(smux_arena_t *arena, size_t count, size_t size) {
    size_t total = count * size;
    void *ptr = smux_arena_alloc(arena, total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

char *smux_arena_strdup(smux_arena_t *arena, const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *copy = smux_arena_alloc(arena, len);
    if (copy) memcpy(copy, s, len);
    return copy;
}

void smux_arena_reset(smux_arena_t *arena) {
    if (!arena) return;
    /* Free all blocks except the first */
    smux_arena_block_t *block = arena->head->next;
    while (block) {
        smux_arena_block_t *next = block->next;
        arena->total_allocated -= block->capacity;
        free(block->data);
        free(block);
        block = next;
    }
    arena->head->next = NULL;
    arena->head->used = 0;
    arena->current = arena->head;
    arena->total_used = 0;
    arena->block_count = 1;
}

size_t smux_arena_used(const smux_arena_t *arena) {
    return arena ? arena->total_used : 0;
}

size_t smux_arena_allocated(const smux_arena_t *arena) {
    return arena ? arena->total_allocated : 0;
}

float smux_arena_utilization(const smux_arena_t *arena) {
    if (!arena || arena->total_allocated == 0) return 0.0f;
    return (float)arena->total_used / (float)arena->total_allocated;
}
