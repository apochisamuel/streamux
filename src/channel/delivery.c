#include "channel.h"
#include "../reassembly/reassembly.h"
#include "../flow/flow.h"
#include <stdlib.h>
#include <stdio.h>
#include "../core/byte_utils.h"

#include <string.h>

/* Channel delivery management: ordering, deduplication, and
   gap detection for data delivered to channels. */

typedef struct {
    uint32_t    expected_seq;
    uint32_t    highest_seen;
    uint32_t    delivered_count;
    uint32_t    gap_count;
    uint32_t    duplicate_count;
    uint32_t    reorder_count;
    bool        ordered_delivery;
} smux_delivery_state_t;

/* Gap tracking: detects missing sequences */
typedef struct {
    uint32_t    gap_start;
    uint32_t    gap_end;
    uint64_t    detected_at;
    bool        filled;
} smux_gap_entry_t;

typedef struct {
    smux_delivery_state_t state;
    smux_gap_entry_t      gaps[64];
    int                   gap_count;
    uint32_t              reorder_buf_seqs[32];
    uint8_t              *reorder_buf_data[32];
    size_t                reorder_buf_lens[32];
    int                   reorder_count;
    uint16_t              channel_id;
} smux_delivery_ctx_t;

/* Forward declarations */
static int smux_delivery_flush_reorder(smux_delivery_ctx_t *ctx);
static void smux_delivery_record_gap(smux_delivery_ctx_t *ctx, uint32_t start, uint32_t end);

smux_delivery_ctx_t *smux_delivery_create(uint16_t channel_id, bool ordered) {
    smux_delivery_ctx_t *ctx = calloc(1, sizeof(smux_delivery_ctx_t));
    if (!ctx) return NULL;
    ctx->channel_id = channel_id;
    ctx->state.ordered_delivery = ordered;
    ctx->state.expected_seq = 1;
    return ctx;
}

void smux_delivery_destroy(smux_delivery_ctx_t *ctx) {
    if (!ctx) return;
    for (int i = 0; i < ctx->reorder_count; i++) {
        free(ctx->reorder_buf_data[i]);
    }
    free(ctx);
}

/* Record an incoming sequence number and classify it */
int smux_delivery_accept(smux_delivery_ctx_t *ctx, uint32_t seq,
                          const uint8_t *data, size_t len) {
    if (!ctx) return SMUX_ERR_INVALID;

    /* Duplicate check */
    if (seq < ctx->state.expected_seq) {
        ctx->state.duplicate_count++;
        return SMUX_ERR_REPLAY;
    }

    /* In-order delivery */
    if (seq == ctx->state.expected_seq) {
        ctx->state.expected_seq++;
        ctx->state.delivered_count++;

        if (seq > ctx->state.highest_seen)
            ctx->state.highest_seen = seq;

        /* Check if any reorder buffer entries can now be delivered */
        smux_delivery_flush_reorder(ctx);
        return SMUX_OK;
    }

    /* Out-of-order: seq > expected */
    if (ctx->state.ordered_delivery) {
        /* Buffer for reordering */
        if (ctx->reorder_count < 32 && data && len > 0) {
            int idx = ctx->reorder_count;
            ctx->reorder_buf_seqs[idx] = seq;
            ctx->reorder_buf_data[idx] = malloc(len);
            if (ctx->reorder_buf_data[idx]) {
                memcpy(ctx->reorder_buf_data[idx], data, len);
                ctx->reorder_buf_lens[idx] = len;
                ctx->reorder_count++;
            }
        }
        ctx->state.reorder_count++;
    } else {
        /* Unordered: deliver immediately */
        ctx->state.delivered_count++;
    }

    /* Record gap */
    if (seq > ctx->state.expected_seq) {
        smux_delivery_record_gap(ctx, ctx->state.expected_seq, seq - 1);
    }

    if (seq > ctx->state.highest_seen)
        ctx->state.highest_seen = seq;

    return SMUX_OK;
}

static int smux_delivery_flush_reorder(smux_delivery_ctx_t *ctx) {
    if (!ctx || ctx->reorder_count == 0) return 0;

    int flushed = 0;
    bool progress = true;

    while (progress) {
        progress = false;
        for (int i = 0; i < ctx->reorder_count; i++) {
            if (ctx->reorder_buf_seqs[i] == ctx->state.expected_seq) {
                ctx->state.expected_seq++;
                ctx->state.delivered_count++;
                flushed++;

                /* Remove from reorder buffer */
                free(ctx->reorder_buf_data[i]);
                ctx->reorder_count--;
                ctx->reorder_buf_seqs[i] = ctx->reorder_buf_seqs[ctx->reorder_count];
                ctx->reorder_buf_data[i] = ctx->reorder_buf_data[ctx->reorder_count];
                ctx->reorder_buf_lens[i] = ctx->reorder_buf_lens[ctx->reorder_count];
                progress = true;
                break;
            }
        }
    }
    return flushed;
}

static void smux_delivery_record_gap(smux_delivery_ctx_t *ctx, uint32_t start, uint32_t end) {
    if (!ctx || ctx->gap_count >= 64) return;

    /* Check if this extends an existing gap */
    for (int i = 0; i < ctx->gap_count; i++) {
        if (ctx->gaps[i].filled) continue;
        if (ctx->gaps[i].gap_end + 1 == start) {
            ctx->gaps[i].gap_end = end;
            return;
        }
        if (end + 1 == ctx->gaps[i].gap_start) {
            ctx->gaps[i].gap_start = start;
            return;
        }
    }

    smux_gap_entry_t *g = &ctx->gaps[ctx->gap_count++];
    g->gap_start = start;
    g->gap_end = end;
    g->detected_at = 0; /* would use real clock */
    g->filled = false;
    ctx->state.gap_count++;
}

int smux_delivery_fill_gap(smux_delivery_ctx_t *ctx, uint32_t seq) {
    if (!ctx) return SMUX_ERR_INVALID;
    for (int i = 0; i < ctx->gap_count; i++) {
        if (ctx->gaps[i].filled) continue;
        if (seq >= ctx->gaps[i].gap_start && seq <= ctx->gaps[i].gap_end) {
            /* Shrink or fill gap */
            if (seq == ctx->gaps[i].gap_start && seq == ctx->gaps[i].gap_end) {
                ctx->gaps[i].filled = true;
            } else if (seq == ctx->gaps[i].gap_start) {
                ctx->gaps[i].gap_start++;
            } else if (seq == ctx->gaps[i].gap_end) {
                ctx->gaps[i].gap_end--;
            }
            return SMUX_OK;
        }
    }
    return SMUX_ERR_INVALID;
}

int smux_delivery_gap_count(const smux_delivery_ctx_t *ctx) {
    if (!ctx) return 0;
    int count = 0;
    for (int i = 0; i < ctx->gap_count; i++) {
        if (!ctx->gaps[i].filled) count++;
    }
    return count;
}

uint32_t smux_delivery_expected_seq(const smux_delivery_ctx_t *ctx) {
    return ctx ? ctx->state.expected_seq : 0;
}

int smux_delivery_report(const smux_delivery_ctx_t *ctx, char *buf, size_t len) {
    if (!ctx || !buf || len == 0) return 0;
    return snprintf(buf, len,
        "Channel %u delivery: delivered=%u gaps=%u dupes=%u reorders=%u\n"
        "  Expected seq: %u  Highest seen: %u\n"
        "  Reorder buffer: %d entries\n"
        "  Active gaps: %d\n",
        ctx->channel_id,
        ctx->state.delivered_count, ctx->state.gap_count,
        ctx->state.duplicate_count, ctx->state.reorder_count,
        ctx->state.expected_seq, ctx->state.highest_seen,
        ctx->reorder_count,
        smux_delivery_gap_count(ctx)
    );
}
