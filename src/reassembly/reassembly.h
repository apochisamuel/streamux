#ifndef SMUX_REASSEMBLY_H
#define SMUX_REASSEMBLY_H

#include "../core/types.h"
#include "../core/status.h"
#include "../buffer/buffer.h"

typedef struct {
    uint16_t    fragment_id;
    uint16_t    total_fragments;
    uint32_t    message_len;
    uint32_t    received_mask[4]; /* bitmask for up to 128 fragments */
    uint32_t    received_count;
    uint32_t    bytes_written;
    smux_refbuf_t *assembly_buf;
    bool        complete;
    bool        abandoned;
    uint64_t    started_at;
    uint16_t    channel_id;
} smux_frag_tracker_t;

typedef struct {
    smux_frag_tracker_t trackers[SMUX_MAX_FRAGMENTS];
    int                 active_count;
    smux_pool_t        *pool;
    uint32_t            capacity;
    uint8_t            *delivery_buf;
    size_t              delivery_cap;
    size_t              delivery_len;
} smux_reassembly_ctx_t;

smux_reassembly_ctx_t *smux_reassembly_create(smux_pool_t *pool, uint32_t capacity);
void smux_reassembly_destroy(smux_reassembly_ctx_t *ctx);
void smux_reassembly_reset(smux_reassembly_ctx_t *ctx);

smux_status_t smux_reassembly_begin(smux_reassembly_ctx_t *ctx,
                                     uint16_t frag_id, uint16_t total,
                                     uint32_t msg_len, uint16_t channel_id);
smux_status_t smux_reassembly_add_fragment(smux_reassembly_ctx_t *ctx,
                                            uint16_t frag_id,
                                            uint32_t offset,
                                            const uint8_t *data, size_t len);
int smux_reassembly_is_complete(smux_reassembly_ctx_t *ctx, uint16_t frag_id);
smux_status_t smux_reassembly_extract(smux_reassembly_ctx_t *ctx,
                                       uint16_t frag_id,
                                       uint8_t *out, size_t cap, size_t *out_len);
smux_status_t smux_reassembly_abandon(smux_reassembly_ctx_t *ctx, uint16_t frag_id);
smux_frag_tracker_t *smux_reassembly_find(smux_reassembly_ctx_t *ctx, uint16_t frag_id);
int smux_reassembly_has_pending(const smux_reassembly_ctx_t *ctx);

smux_status_t smux_reassembly_resize(smux_reassembly_ctx_t *ctx, uint32_t new_capacity);
smux_status_t smux_reassembly_deliver(smux_reassembly_ctx_t *ctx, uint16_t frag_id,
                                       uint8_t *out, size_t cap, size_t *written);

#endif
