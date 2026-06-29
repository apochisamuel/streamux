#include "scheduler.h"
#include <stdlib.h>
#include <string.h>

smux_delayed_queue_t *smux_delayed_create(void) {
    return calloc(1, sizeof(smux_delayed_queue_t));
}

void smux_delayed_destroy(smux_delayed_queue_t *dq) {
    if (!dq) return;
    for (int i = 0; i < dq->count; i++) {
        free(dq->entries[i].data);
    }
    free(dq);
}

smux_status_t smux_delayed_add(smux_delayed_queue_t *dq, uint32_t seq,
                                uint16_t channel_id, uint64_t deliver_at,
                                const uint8_t *data, size_t len) {
    if (!dq || !data) return SMUX_ERR_INVALID;
    if (dq->count >= SMUX_MAX_CHANNELS) return SMUX_ERR_OVERFLOW;

    smux_delayed_entry_t *e = &dq->entries[dq->count];
    e->seq = seq;
    e->channel_id = channel_id;
    e->deliver_at = deliver_at;
    e->data = malloc(len);
    if (!e->data) return SMUX_ERR_NOMEM;
    memcpy(e->data, data, len);
    e->data_len = len;
    e->delivered = false;
    e->cancelled = false;
    dq->count++;
    return SMUX_OK;
}

int smux_delayed_process(smux_delayed_queue_t *dq, uint64_t now_ms,
                          smux_delayed_entry_t *out, int max_out) {
    if (!dq || !out) return 0;
    int ready = 0;
    for (int i = 0; i < dq->count && ready < max_out; i++) {
        smux_delayed_entry_t *e = &dq->entries[i];
        if (e->cancelled || e->delivered) continue;
        if (now_ms >= e->deliver_at) {
            out[ready++] = *e;
            e->delivered = true;
        }
    }
    return ready;
}

smux_status_t smux_delayed_cancel_channel(smux_delayed_queue_t *dq, uint16_t channel_id) {
    if (!dq) return SMUX_ERR_INVALID;
    for (int i = 0; i < dq->count; i++) {
        if (dq->entries[i].channel_id == channel_id) {
            dq->entries[i].cancelled = true;
        }
    }
    return SMUX_OK;
}
