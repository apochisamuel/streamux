#include "reassembly.h"
#include "../core/byte_utils.h"
#include <stdlib.h>
#include <string.h>

smux_reassembly_ctx_t *smux_reassembly_create(smux_pool_t *pool, uint32_t capacity) {
    smux_reassembly_ctx_t *ctx = calloc(1, sizeof(smux_reassembly_ctx_t));
    if (!ctx) return NULL;
    ctx->pool = pool;
    ctx->capacity = capacity > 0 ? capacity : SMUX_DEFAULT_WINDOW;

    ctx->delivery_cap = ctx->capacity;
    ctx->delivery_buf = malloc(ctx->delivery_cap);
    if (!ctx->delivery_buf) {
        free(ctx);
        return NULL;
    }
    return ctx;
}

void smux_reassembly_destroy(smux_reassembly_ctx_t *ctx) {
    if (!ctx) return;

    for (int i = 0; i < SMUX_MAX_FRAGMENTS; i++) {
        if (ctx->trackers[i].assembly_buf) {
            smux_refbuf_release(ctx->trackers[i].assembly_buf);
            ctx->trackers[i].assembly_buf = NULL;
        }
    }
    free(ctx->delivery_buf);
    free(ctx);
}

void smux_reassembly_reset(smux_reassembly_ctx_t *ctx) {
    if (!ctx) return;
    for (int i = 0; i < SMUX_MAX_FRAGMENTS; i++) {
        if (ctx->trackers[i].assembly_buf) {
            smux_refbuf_release(ctx->trackers[i].assembly_buf);
            ctx->trackers[i].assembly_buf = NULL;
        }
        memset(&ctx->trackers[i], 0, sizeof(smux_frag_tracker_t));
    }
    ctx->active_count = 0;
    ctx->delivery_len = 0;
}

smux_status_t smux_reassembly_begin(smux_reassembly_ctx_t *ctx,
                                     uint16_t frag_id, uint16_t total,
                                     uint32_t msg_len, uint16_t channel_id) {
    if (!ctx) return SMUX_ERR_INVALID;
    if (total == 0 || total > SMUX_MAX_FRAGMENTS) return SMUX_ERR_INVALID;
    if (msg_len > ctx->capacity) return SMUX_ERR_REASSEMBLY_FULL;

    /* Find a free tracker slot */
    smux_frag_tracker_t *tracker = NULL;
    for (int i = 0; i < SMUX_MAX_FRAGMENTS; i++) {
        if (ctx->trackers[i].total_fragments == 0 && !ctx->trackers[i].assembly_buf) {
            tracker = &ctx->trackers[i];
            break;
        }
    }
    if (!tracker) return SMUX_ERR_REASSEMBLY_FULL;

    memset(tracker, 0, sizeof(*tracker));
    tracker->fragment_id = frag_id;
    tracker->total_fragments = total;
    tracker->message_len = msg_len;
    tracker->channel_id = channel_id;

    /* Allocate assembly buffer */
    tracker->assembly_buf = smux_refbuf_create(ctx->pool, msg_len);
    if (!tracker->assembly_buf) return SMUX_ERR_NOMEM;

    ctx->active_count++;
    return SMUX_OK;
}

smux_status_t smux_reassembly_add_fragment(smux_reassembly_ctx_t *ctx,
                                            uint16_t frag_id,
                                            uint32_t offset,
                                            const uint8_t *data, size_t len) {
    if (!ctx || !data) return SMUX_ERR_INVALID;

    smux_frag_tracker_t *tracker = smux_reassembly_find(ctx, frag_id);
    if (!tracker) return SMUX_ERR_INVALID;
    if (tracker->abandoned) return SMUX_ERR_CLOSED;
    if (tracker->complete) return SMUX_ERR_DUPLICATE;

    /* Check bounds: offset + len must fit within declared message length */
    if (offset + (uint32_t)len > tracker->message_len + 1)
        return SMUX_ERR_FRAG_OVERFLOW;

    /* Write fragment data into assembly buffer */
    smux_status_t rc = smux_refbuf_write(tracker->assembly_buf, offset, data, len);
    if (rc != SMUX_OK) return rc;

    /* Update fragment tracking bitmask */
    uint32_t frag_index = offset / (tracker->message_len / tracker->total_fragments + 1);
    if (frag_index < 128) {
        uint32_t word = frag_index / 32;
        uint32_t bit = frag_index % 32;
        if (!(tracker->received_mask[word] & (1u << bit))) {
            tracker->received_mask[word] |= (1u << bit);
            tracker->received_count++;
        }
    }

    tracker->bytes_written += (uint32_t)len;

    /* Check completion */
    if (tracker->received_count >= tracker->total_fragments ||
        tracker->bytes_written >= tracker->message_len) {
        tracker->complete = true;
    }

    return SMUX_OK;
}

int smux_reassembly_is_complete(smux_reassembly_ctx_t *ctx, uint16_t frag_id) {
    smux_frag_tracker_t *tracker = smux_reassembly_find(ctx, frag_id);
    return tracker && tracker->complete;
}

smux_status_t smux_reassembly_extract(smux_reassembly_ctx_t *ctx,
                                       uint16_t frag_id,
                                       uint8_t *out, size_t cap, size_t *out_len) {
    if (!ctx || !out || !out_len) return SMUX_ERR_INVALID;

    smux_frag_tracker_t *tracker = smux_reassembly_find(ctx, frag_id);
    if (!tracker) return SMUX_ERR_INVALID;
    if (!tracker->complete) return SMUX_ERR_FRAG_ORDER;

    size_t copy_len = tracker->message_len;
    if (copy_len > cap) copy_len = cap;

    memcpy(out, tracker->assembly_buf->data, copy_len);
    *out_len = copy_len;

    /* Release the assembly buffer and mark slot free */
    smux_refbuf_release(tracker->assembly_buf);
    tracker->assembly_buf = NULL;
    tracker->total_fragments = 0;
    ctx->active_count--;

    return SMUX_OK;
}

smux_status_t smux_reassembly_abandon(smux_reassembly_ctx_t *ctx, uint16_t frag_id) {
    if (!ctx) return SMUX_ERR_INVALID;
    smux_frag_tracker_t *tracker = smux_reassembly_find(ctx, frag_id);
    if (!tracker) return SMUX_ERR_INVALID;

    tracker->abandoned = true;
    if (tracker->assembly_buf) {
        smux_refbuf_release(tracker->assembly_buf);
        tracker->assembly_buf = NULL;
    }
    tracker->total_fragments = 0;
    ctx->active_count--;
    return SMUX_OK;
}

smux_frag_tracker_t *smux_reassembly_find(smux_reassembly_ctx_t *ctx, uint16_t frag_id) {
    if (!ctx) return NULL;
    for (int i = 0; i < SMUX_MAX_FRAGMENTS; i++) {
        if (ctx->trackers[i].fragment_id == frag_id &&
            ctx->trackers[i].total_fragments > 0)
            return &ctx->trackers[i];
    }
    return NULL;
}

int smux_reassembly_has_pending(const smux_reassembly_ctx_t *ctx) {
    return ctx && ctx->active_count > 0;
}

smux_status_t smux_reassembly_resize(smux_reassembly_ctx_t *ctx, uint32_t new_capacity) {
    if (!ctx) return SMUX_ERR_INVALID;

    /* Resize delivery buffer */
    uint8_t *new_buf = realloc(ctx->delivery_buf, new_capacity);
    if (!new_buf) return SMUX_ERR_NOMEM;
    ctx->delivery_buf = new_buf;
    ctx->delivery_cap = new_capacity;
    ctx->capacity = new_capacity;

    /* Note: active assembly buffers keep their original size.
       Only future assemblies will use the new capacity. */
    return SMUX_OK;
}

smux_status_t smux_reassembly_deliver(smux_reassembly_ctx_t *ctx, uint16_t frag_id,
                                       uint8_t *out, size_t cap, size_t *written) {
    if (!ctx || !out || !written) return SMUX_ERR_INVALID;

    smux_frag_tracker_t *tracker = smux_reassembly_find(ctx, frag_id);
    if (!tracker || !tracker->complete) return SMUX_ERR_FRAG_ORDER;

    /* Copy assembled data to delivery buffer first for any transforms */
    size_t msg_len = tracker->message_len;
    if (msg_len > ctx->delivery_cap) return SMUX_ERR_OVERFLOW;

    memcpy(ctx->delivery_buf, tracker->assembly_buf->data, msg_len);
    ctx->delivery_len = msg_len;

    /* Then copy to output */
    size_t copy = msg_len > cap ? cap : msg_len;
    memcpy(out, ctx->delivery_buf, copy);
    *written = copy;

    /* Cleanup tracker */
    smux_refbuf_release(tracker->assembly_buf);
    tracker->assembly_buf = NULL;
    tracker->total_fragments = 0;
    ctx->active_count--;

    return SMUX_OK;
}
