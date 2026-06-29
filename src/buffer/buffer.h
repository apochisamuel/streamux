#ifndef SMUX_BUFFER_H
#define SMUX_BUFFER_H

#include "../core/types.h"
#include "../core/status.h"

typedef struct smux_pool smux_pool_t;

typedef struct smux_slab {
    uint8_t         data[SMUX_SLAB_SIZE];
    size_t          used;
    int             ref_count;
    int             slab_id;
    struct smux_slab *next;
    struct smux_slab *prev;
} smux_slab_t;

struct smux_pool {
    smux_slab_t    *slabs;
    int             slab_count;
    int             active_count;
    size_t          total_allocated;
    size_t          total_capacity;
    smux_slab_t    *free_list;
    int             free_count;
};

smux_pool_t *smux_pool_create(int initial_slabs);
void smux_pool_destroy(smux_pool_t *pool);
smux_slab_t *smux_pool_acquire(smux_pool_t *pool);
void smux_pool_release(smux_pool_t *pool, smux_slab_t *slab);
int smux_pool_grow(smux_pool_t *pool, int count);
void smux_pool_shrink(smux_pool_t *pool);
int smux_pool_defragment(smux_pool_t *pool);

/* Slice operations for zero-copy references */
smux_slice_t *smux_slice_create(uint8_t *data, size_t len);
smux_slice_t *smux_slice_from_slab(smux_slab_t *slab, size_t offset, size_t len);
void smux_slice_retain(smux_slice_t *slice);
void smux_slice_release(smux_slice_t *slice);
smux_slice_t *smux_slice_split(smux_slice_t *slice, size_t offset);
int smux_slice_append(smux_slice_t *slice, const uint8_t *data, size_t len);

/* Reference-counted buffer for assembly */
typedef struct {
    uint8_t        *data;
    size_t          capacity;
    size_t          length;
    int             ref_count;
    smux_pool_t    *origin_pool;
    smux_slab_t    *origin_slab;
} smux_refbuf_t;

smux_refbuf_t *smux_refbuf_create(smux_pool_t *pool, size_t capacity);
smux_refbuf_t *smux_refbuf_create_heap(size_t capacity);
void smux_refbuf_retain(smux_refbuf_t *buf);
void smux_refbuf_release(smux_refbuf_t *buf);
int smux_refbuf_resize(smux_refbuf_t *buf, size_t new_capacity);
int smux_refbuf_write(smux_refbuf_t *buf, size_t offset, const uint8_t *data, size_t len);

#endif
