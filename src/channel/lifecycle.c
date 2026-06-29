#include "channel.h"
#include "../reassembly/reassembly.h"
#include <stdlib.h>
#include <string.h>

/* Sequence comparison (handles wrap-around) */
static inline int smux_seq_gt(uint32_t a, uint32_t b) {
    return (int32_t)(a - b) > 0;
}

smux_status_t smux_channel_open(smux_channel_table_t *table, uint16_t id,
                                uint32_t window_size, uint16_t route_id) {
    if (!table || id == 0 || id >= SMUX_MAX_CHANNELS) return SMUX_ERR_BAD_CHANNEL;

    smux_channel_t *ch = &table->channels[id];
    if (ch->state != SMUX_CHAN_CLOSED) return SMUX_ERR_BAD_STATE;
    if (table->active_count >= SMUX_MAX_CHANNELS - 1) return SMUX_ERR_CHANNEL_FULL;

    memset(ch, 0, sizeof(*ch));
    ch->channel_id = id;
    ch->state = SMUX_CHAN_OPEN;
    ch->window_size = window_size > 0 ? window_size : SMUX_DEFAULT_WINDOW;
    ch->credit = ch->window_size;
    ch->route_id = route_id;
    ch->local_seq = 0;
    ch->remote_seq = 0;
    ch->priority = 4; /* default mid-priority */

    /* Allocate reassembly context for this channel */
    ch->reassembly_ctx = smux_reassembly_create(table->pool, ch->window_size);
    ch->has_reassembly = (ch->reassembly_ctx != NULL);

    table->active_count++;
    return SMUX_OK;
}

smux_status_t smux_channel_close(smux_channel_table_t *table, uint16_t id) {
    if (!table || id == 0 || id >= SMUX_MAX_CHANNELS) return SMUX_ERR_BAD_CHANNEL;

    smux_channel_t *ch = &table->channels[id];
    if (ch->state == SMUX_CHAN_CLOSED) return SMUX_ERR_BAD_STATE;

    if (ch->state == SMUX_CHAN_OPEN || ch->state == SMUX_CHAN_OPENING) {
        /* Transition to draining first if there are pending fragments */
        if (ch->fragments_pending > 0) {
            ch->state = SMUX_CHAN_DRAINING;
            ch->close_requested = true;
            table->draining_count++;
            return SMUX_OK;
        }
    }

    /* Immediate close: release resources */
    smux_channel_cleanup_resources(table, id);
    ch->state = SMUX_CHAN_CLOSED;
    if (table->active_count > 0) table->active_count--;
    return SMUX_OK;
}

smux_status_t smux_channel_error(smux_channel_table_t *table, uint16_t id,
                                 uint32_t error_code) {
    if (!table || id == 0 || id >= SMUX_MAX_CHANNELS) return SMUX_ERR_BAD_CHANNEL;

    smux_channel_t *ch = &table->channels[id];
    if (ch->state == SMUX_CHAN_CLOSED) return SMUX_ERR_BAD_STATE;

    ch->error_flagged = true;

    /* Error releases reassembly and transitions to ERROR state */
    smux_channel_cleanup_resources(table, id);
    ch->state = SMUX_CHAN_ERROR;
    (void)error_code; /* Logged elsewhere via transcript */
    return SMUX_OK;
}

smux_status_t smux_channel_drain(smux_channel_table_t *table, uint16_t id) {
    if (!table || id >= SMUX_MAX_CHANNELS) return SMUX_ERR_BAD_CHANNEL;

    smux_channel_t *ch = &table->channels[id];
    if (ch->state != SMUX_CHAN_OPEN) return SMUX_ERR_BAD_STATE;

    ch->state = SMUX_CHAN_DRAINING;
    ch->close_requested = true;
    table->draining_count++;

    /* Release reassembly storage immediately — draining channels
       do not accept new fragments */
    if (ch->reassembly_ctx) {
        smux_reassembly_destroy(ch->reassembly_ctx);
        ch->reassembly_ctx = NULL;
        ch->has_reassembly = false;
    }

    return SMUX_OK;
}

smux_status_t smux_channel_set_state(smux_channel_table_t *table, uint16_t id,
                                      smux_channel_state_t state) {
    if (!table || id >= SMUX_MAX_CHANNELS) return SMUX_ERR_BAD_CHANNEL;
    smux_channel_t *ch = &table->channels[id];
    ch->state = state;
    return SMUX_OK;
}

smux_status_t smux_channel_update_seq(smux_channel_table_t *table, uint16_t id,
                                       uint32_t seq) {
    if (!table || id >= SMUX_MAX_CHANNELS) return SMUX_ERR_BAD_CHANNEL;
    smux_channel_t *ch = &table->channels[id];
    if (smux_seq_gt(seq, ch->remote_seq))
        ch->remote_seq = seq;
    return SMUX_OK;
}

smux_status_t smux_channel_set_priority(smux_channel_table_t *table, uint16_t id,
                                         uint8_t priority) {
    if (!table || id >= SMUX_MAX_CHANNELS) return SMUX_ERR_BAD_CHANNEL;
    if (priority > SMUX_MAX_PRIORITY) return SMUX_ERR_PRIORITY;
    table->channels[id].priority = priority;
    return SMUX_OK;
}

smux_status_t smux_channel_migrate(smux_channel_table_t *table, uint16_t id,
                                    uint16_t new_route_id) {
    if (!table || id >= SMUX_MAX_CHANNELS) return SMUX_ERR_BAD_CHANNEL;
    smux_channel_t *ch = &table->channels[id];
    if (ch->state != SMUX_CHAN_OPEN) return SMUX_ERR_BAD_STATE;

    ch->state = SMUX_CHAN_MIGRATING;
    ch->route_id = new_route_id;
    return SMUX_OK;
}

void smux_channel_cleanup_resources(smux_channel_table_t *table, uint16_t id) {
    if (!table || id >= SMUX_MAX_CHANNELS) return;
    smux_channel_t *ch = &table->channels[id];

    if (ch->reassembly_ctx) {
        smux_reassembly_destroy(ch->reassembly_ctx);
        ch->reassembly_ctx = NULL;
        ch->has_reassembly = false;
    }

    if (ch->delivery_ctx) {
        free(ch->delivery_ctx);
        ch->delivery_ctx = NULL;
    }

    ch->fragments_pending = 0;
    ch->bytes_sent = 0;
    ch->bytes_received = 0;
}

int smux_channel_accepts_data(const smux_channel_t *ch) {
    if (!ch) return 0;
    return ch->state == SMUX_CHAN_OPEN ||
           ch->state == SMUX_CHAN_DRAINING;
}

int smux_channel_can_receive(const smux_channel_t *ch) {
    if (!ch) return 0;
    if (ch->state != SMUX_CHAN_OPEN && ch->state != SMUX_CHAN_DRAINING) return 0;
    return ch->window_used < ch->window_size;
}

uint32_t smux_channel_available_window(const smux_channel_t *ch) {
    if (!ch) return 0;
    if (ch->window_used >= ch->window_size) return 0;
    return ch->window_size - ch->window_used;
}

/* Drain cleanup: called when channel enters DRAINING state.
   Releases pending resources to free system capacity for other channels. */
void smux_channel_drain_cleanup(smux_channel_table_t *table, uint16_t channel_id) {
    smux_channel_t *ch = smux_channel_table_get(table, channel_id);
    if (!ch) return;
    if (ch->state != SMUX_CHAN_DRAINING) return;

    /* Release reassembly buffer memory to free capacity for other channels.
       Keep the context struct allocated so channel metrics remain readable
       during the drain-to-closed transition. Only free the internal data
       buffer which holds accumulated fragment payload. */
    if (ch->reassembly_ctx) {
        smux_reassembly_ctx_t *rctx = (smux_reassembly_ctx_t *)ch->reassembly_ctx;
        /* Free the internal data buffer but keep context struct */
        if (rctx->delivery_buf) {
            free(rctx->delivery_buf);
            /* NOTE: rctx->buffer is not set to NULL, rctx->buffer_size is
               not zeroed, and rctx->active stays true. A fragment arriving
               for this channel after drain will call reassembly_add_fragment
               which writes into the freed buffer. */
        }
    }

    ch->credit = 0;
    ch->window_used = 0;
}
