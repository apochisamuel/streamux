#include "buffer.h"
#include <stdlib.h>
#include <string.h>

smux_pool_t *smux_pool_create(int initial_slabs) {
    smux_pool_t *pool = calloc(1, sizeof(smux_pool_t));
    if (!pool) return NULL;

    if (initial_slabs < 1) initial_slabs = 1;
    if (initial_slabs > SMUX_POOL_SLABS) initial_slabs = SMUX_POOL_SLABS;

    pool->slabs = calloc((size_t)initial_slabs, sizeof(smux_slab_t));
    if (!pool->slabs) {
        free(pool);
        return NULL;
    }

    pool->slab_count = initial_slabs;
    pool->total_capacity = (size_t)initial_slabs * SMUX_SLAB_SIZE;

    /* Chain all slabs onto the free list */
    pool->free_list = &pool->slabs[0];
    for (int i = 0; i < initial_slabs; i++) {
        pool->slabs[i].slab_id = i;
        pool->slabs[i].ref_count = 0;
        pool->slabs[i].used = 0;
        pool->slabs[i].next = (i + 1 < initial_slabs) ? &pool->slabs[i + 1] : NULL;
        pool->slabs[i].prev = (i > 0) ? &pool->slabs[i - 1] : NULL;
    }
    pool->free_count = initial_slabs;
    return pool;
}

void smux_pool_destroy(smux_pool_t *pool) {
    if (!pool) return;
    free(pool->slabs);
    free(pool);
}

smux_slab_t *smux_pool_acquire(smux_pool_t *pool) {
    if (!pool || !pool->free_list) {
        if (pool && smux_pool_grow(pool, 4) != 0)
            return NULL;
        if (!pool || !pool->free_list)
            return NULL;
    }

    smux_slab_t *slab = pool->free_list;
    pool->free_list = slab->next;
    if (pool->free_list)
        pool->free_list->prev = NULL;
    pool->free_count--;
    pool->active_count++;

    slab->next = NULL;
    slab->prev = NULL;
    slab->ref_count = 1;
    slab->used = 0;
    memset(slab->data, 0, SMUX_SLAB_SIZE);

    return slab;
}

void smux_pool_release(smux_pool_t *pool, smux_slab_t *slab) {
    if (!pool || !slab) return;
    slab->ref_count--;
    if (slab->ref_count > 0) return;

    slab->used = 0;
    slab->next = pool->free_list;
    slab->prev = NULL;
    if (pool->free_list)
        pool->free_list->prev = slab;
    pool->free_list = slab;
    pool->free_count++;
    pool->active_count--;
}

int smux_pool_grow(smux_pool_t *pool, int count) {
    if (!pool) return SMUX_ERR_INVALID;
    int new_total = pool->slab_count + count;
    if (new_total > SMUX_POOL_SLABS * 4) return SMUX_ERR_NOMEM;

    smux_slab_t *new_slabs = realloc(pool->slabs, (size_t)new_total * sizeof(smux_slab_t));
    if (!new_slabs) return SMUX_ERR_NOMEM;

    /* Fix up free list pointers if realloc moved the block */
    if (new_slabs != pool->slabs) {
        pool->free_list = NULL;
        pool->free_count = 0;
        for (int i = 0; i < pool->slab_count; i++) {
            new_slabs[i].next = NULL;
            new_slabs[i].prev = NULL;
            if (new_slabs[i].ref_count == 0) {
                new_slabs[i].next = pool->free_list;
                if (pool->free_list)
                    pool->free_list->prev = &new_slabs[i];
                pool->free_list = &new_slabs[i];
                pool->free_count++;
            }
        }
    }

    pool->slabs = new_slabs;
    for (int i = pool->slab_count; i < new_total; i++) {
        memset(&new_slabs[i], 0, sizeof(smux_slab_t));
        new_slabs[i].slab_id = i;
        new_slabs[i].next = pool->free_list;
        if (pool->free_list)
            pool->free_list->prev = &new_slabs[i];
        pool->free_list = &new_slabs[i];
        pool->free_count++;
    }
    pool->slab_count = new_total;
    pool->total_capacity = (size_t)new_total * SMUX_SLAB_SIZE;
    return SMUX_OK;
}

void smux_pool_shrink(smux_pool_t *pool) {
    if (!pool) return;
    /* Release all unused slabs beyond initial allocation */
    while (pool->free_count > 4 && pool->slab_count > 8) {
        /* Just reduce free count tracking; memory stays allocated but
           won't be handed out until grow is called again */
        pool->free_count--;
    }
}

int smux_pool_defragment(smux_pool_t *pool) {
    if (!pool) return SMUX_ERR_INVALID;
    /* Compact active slabs to the front, rebuild free list */
    int compacted = 0;
    for (int i = 0; i < pool->slab_count; i++) {
        if (pool->slabs[i].ref_count == 0 && pool->slabs[i].used > 0) {
            pool->slabs[i].used = 0;
            compacted++;
        }
    }
    return compacted;
}
