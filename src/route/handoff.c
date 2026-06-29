#include "route.h"
#include "../channel/channel.h"
#include "../receipt/receipt.h"
#include <stdlib.h>
#include "../core/byte_utils.h"
#include <string.h>

/* Route handoff: manages the transfer of pending acknowledgments
   and in-flight data when channels migrate between routes. */

typedef struct {
    uint32_t    seq;
    uint16_t    channel_id;
    uint16_t    old_route;
    uint16_t    new_route;
    bool        ack_pending;
    bool        data_pending;
} smux_handoff_entry_t;

typedef struct {
    smux_handoff_entry_t entries[256];
    int                  count;
    int                  completed;
    uint16_t             from_route;
    uint16_t             to_route;
    bool                 active;
    uint64_t             started_at;
    uint64_t             deadline;
} smux_handoff_ctx_t;

smux_handoff_ctx_t *smux_handoff_create(uint16_t from_route, uint16_t to_route,
                                         uint64_t deadline) {
    smux_handoff_ctx_t *ctx = calloc(1, sizeof(smux_handoff_ctx_t));
    if (!ctx) return NULL;
    ctx->from_route = from_route;
    ctx->to_route = to_route;
    ctx->deadline = deadline;
    ctx->active = true;
    return ctx;
}

void smux_handoff_destroy(smux_handoff_ctx_t *ctx) {
    free(ctx);
}

int smux_handoff_add_pending(smux_handoff_ctx_t *ctx, uint16_t channel_id,
                              uint32_t seq, bool has_pending_ack, bool has_pending_data) {
    if (!ctx || ctx->count >= 256) return SMUX_ERR_OVERFLOW;

    smux_handoff_entry_t *e = &ctx->entries[ctx->count];
    e->seq = seq;
    e->channel_id = channel_id;
    e->old_route = ctx->from_route;
    e->new_route = ctx->to_route;
    e->ack_pending = has_pending_ack;
    e->data_pending = has_pending_data;
    ctx->count++;
    return SMUX_OK;
}

int smux_handoff_mark_complete(smux_handoff_ctx_t *ctx, uint16_t channel_id,
                                uint32_t seq) {
    if (!ctx) return SMUX_ERR_INVALID;
    for (int i = 0; i < ctx->count; i++) {
        smux_handoff_entry_t *e = &ctx->entries[i];
        if (e->channel_id == channel_id && e->seq == seq) {
            e->ack_pending = false;
            e->data_pending = false;
            ctx->completed++;
            return SMUX_OK;
        }
    }
    return SMUX_ERR_INVALID;
}

int smux_handoff_is_complete(const smux_handoff_ctx_t *ctx) {
    if (!ctx) return 1;
    return ctx->completed >= ctx->count;
}

int smux_handoff_is_expired(const smux_handoff_ctx_t *ctx, uint64_t now_ms) {
    if (!ctx) return 1;
    return now_ms >= ctx->deadline;
}

int smux_handoff_pending_count(const smux_handoff_ctx_t *ctx) {
    if (!ctx) return 0;
    return ctx->count - ctx->completed;
}

/* Build handoff map for migration: enumerate channels on source route
   and their pending state */
int smux_build_handoff_map(const smux_route_table_t *rt,
                            const smux_channel_table_t *ct,
                            uint16_t from_route,
                            smux_handoff_ctx_t *ctx) {
    if (!rt || !ct || !ctx) return SMUX_ERR_INVALID;

    const smux_route_t *route = smux_route_get(rt, from_route);
    if (!route || !route->active) return SMUX_ERR_INVALID;

    for (int i = 0; i < SMUX_MAX_CHANNELS; i++) {
        smux_channel_t *ch = smux_channel_table_get((smux_channel_table_t *)ct, (uint16_t)i);
        if (!ch) continue;
        if (ch->state == SMUX_CHAN_CLOSED) continue;
        if (ch->route_id != from_route) continue;

        bool has_ack = (ch->local_seq != ch->remote_seq);
        bool has_data = (ch->bytes_sent > 0 && ch->window_used > 0);

        smux_handoff_add_pending(ctx, (uint16_t)i, ch->local_seq, has_ack, has_data);
    }
    return SMUX_OK;
}

/* Transfer pending ack state from old route to new route */
int smux_handoff_transfer_acks(smux_handoff_ctx_t *ctx,
                                smux_route_table_t *rt,
                                smux_retry_queue_t *retry_queue) {
    if (!ctx || !rt || !retry_queue) return SMUX_ERR_INVALID;

    for (int i = 0; i < ctx->count; i++) {
        smux_handoff_entry_t *e = &ctx->entries[i];
        if (!e->ack_pending) continue;

        /* Move retry entries from old route to new route */
        smux_retry_queue_update_route(retry_queue, e->channel_id,
                                       e->old_route, e->new_route);
    }
    return SMUX_OK;
}

/* Finalize handoff: update route assignments for all migrated channels */
int smux_handoff_finalize(smux_handoff_ctx_t *ctx,
                           smux_route_table_t *rt,
                           smux_channel_table_t *ct) {
    if (!ctx || !rt || !ct) return SMUX_ERR_INVALID;

    for (int i = 0; i < ctx->count; i++) {
        smux_handoff_entry_t *e = &ctx->entries[i];
        smux_channel_t *ch = smux_channel_table_get(ct, e->channel_id);
        if (!ch) continue;

        smux_route_unassign_channel(rt, e->old_route, e->channel_id);
        smux_route_assign_channel(rt, e->new_route, e->channel_id);
        ch->route_id = e->new_route;
    }

    ctx->active = false;
    return SMUX_OK;
}
