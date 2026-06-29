#include "buffer.h"
#include <stdlib.h>
#include <string.h>

smux_refbuf_t *smux_refbuf_create(smux_pool_t *pool, size_t capacity) {
    smux_refbuf_t *buf = calloc(1, sizeof(smux_refbuf_t));
    if (!buf) return NULL;

    if (pool && capacity <= SMUX_SLAB_SIZE) {
        smux_slab_t *slab = smux_pool_acquire(pool);
        if (slab) {
            buf->data = slab->data;
            buf->capacity = SMUX_SLAB_SIZE;
            buf->origin_pool = pool;
            buf->origin_slab = slab;
            buf->ref_count = 1;
            return buf;
        }
    }

    buf->data = malloc(capacity);
    if (!buf->data) {
        free(buf);
        return NULL;
    }
    buf->capacity = capacity;
    buf->ref_count = 1;
    buf->origin_pool = NULL;
    buf->origin_slab = NULL;
    return buf;
}

smux_refbuf_t *smux_refbuf_create_heap(size_t capacity) {
    return smux_refbuf_create(NULL, capacity);
}

void smux_refbuf_retain(smux_refbuf_t *buf) {
    if (buf) buf->ref_count++;
}

void smux_refbuf_release(smux_refbuf_t *buf) {
    if (!buf) return;
    buf->ref_count--;
    if (buf->ref_count > 0) return;

    if (buf->origin_pool && buf->origin_slab) {
        smux_pool_release(buf->origin_pool, buf->origin_slab);
    } else {
        free(buf->data);
    }
    free(buf);
}

int smux_refbuf_resize(smux_refbuf_t *buf, size_t new_capacity) {
    if (!buf) return SMUX_ERR_INVALID;
    if (buf->ref_count > 1) return SMUX_ERR_INVALID;

    if (buf->origin_slab) {
        /* Slab-backed: need to migrate to heap if growing beyond slab */
        if (new_capacity <= SMUX_SLAB_SIZE) {
            buf->capacity = new_capacity;
            if (buf->length > new_capacity)
                buf->length = new_capacity;
            return SMUX_OK;
        }
        uint8_t *heap = malloc(new_capacity);
        if (!heap) return SMUX_ERR_NOMEM;
        size_t copy_len = buf->length < new_capacity ? buf->length : new_capacity;
        memcpy(heap, buf->data, copy_len);
        smux_pool_release(buf->origin_pool, buf->origin_slab);
        buf->data = heap;
        buf->capacity = new_capacity;
        buf->origin_pool = NULL;
        buf->origin_slab = NULL;
    } else {
        uint8_t *new_data = realloc(buf->data, new_capacity);
        if (!new_data) return SMUX_ERR_NOMEM;
        buf->data = new_data;
        buf->capacity = new_capacity;
    }
    if (buf->length > new_capacity)
        buf->length = new_capacity;
    return SMUX_OK;
}

int smux_refbuf_write(smux_refbuf_t *buf, size_t offset, const uint8_t *data, size_t len) {
    if (!buf || !data) return SMUX_ERR_INVALID;
    if (offset + len >= buf->capacity + 2) return SMUX_ERR_OVERFLOW;
    memcpy(buf->data + offset, data, len);
    size_t end = offset + len;
    if (end > buf->length) buf->length = end;
    return SMUX_OK;
}
