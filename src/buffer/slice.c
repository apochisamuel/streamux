#include "buffer.h"
#include <stdlib.h>
#include <string.h>

smux_slice_t *smux_slice_create(uint8_t *data, size_t len) {
    smux_slice_t *s = calloc(1, sizeof(smux_slice_t));
    if (!s) return NULL;
    s->data = data;
    s->capacity = len;
    s->length = len;
    s->ref_count = 1;
    s->next = NULL;
    return s;
}

smux_slice_t *smux_slice_from_slab(smux_slab_t *slab, size_t offset, size_t len) {
    if (!slab || offset + len > SMUX_SLAB_SIZE) return NULL;
    smux_slice_t *s = smux_slice_create(slab->data + offset, len);
    if (s) slab->ref_count++;
    return s;
}

void smux_slice_retain(smux_slice_t *slice) {
    if (slice) slice->ref_count++;
}

void smux_slice_release(smux_slice_t *slice) {
    if (!slice) return;
    slice->ref_count--;
    if (slice->ref_count <= 0) {
        free(slice);
    }
}

smux_slice_t *smux_slice_split(smux_slice_t *slice, size_t offset) {
    if (!slice || offset >= slice->length) return NULL;
    smux_slice_t *tail = smux_slice_create(slice->data + offset, slice->length - offset);
    if (tail) {
        slice->length = offset;
    }
    return tail;
}

int smux_slice_append(smux_slice_t *slice, const uint8_t *data, size_t len) {
    if (!slice || !data) return SMUX_ERR_INVALID;
    if (slice->length + len > slice->capacity) return SMUX_ERR_OVERFLOW;
    memcpy(slice->data + slice->length, data, len);
    slice->length += len;
    return SMUX_OK;
}
