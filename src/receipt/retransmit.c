#include "receipt.h"
#include <stdlib.h>
#include <string.h>

smux_retry_queue_t *smux_retry_queue_create(void) {
    smux_retry_queue_t *q = calloc(1, sizeof(smux_retry_queue_t));
    if (!q) return NULL;
    q->capacity = SMUX_MAX_RETRY * SMUX_MAX_CHANNELS;
    return q;
}

void smux_retry_queue_destroy(smux_retry_queue_t *q) {
    if (!q) return;
    /* Free any allocated data buffers */
    for (int i = 0; i < q->count; i++) {
        if (q->entries[i].data) {
            free(q->entries[i].data);
            q->entries[i].data = NULL;
        }
    }
    free(q);
}

smux_status_t smux_retry_queue_push(smux_retry_queue_t *q, const smux_retry_entry_t *entry) {
    if (!q || !entry) return SMUX_ERR_INVALID;
    if (q->count >= q->capacity) return SMUX_ERR_OVERFLOW;

    /* Check for duplicate */
    for (int i = 0; i < q->count; i++) {
        if (q->entries[i].seq == entry->seq &&
            q->entries[i].channel_id == entry->channel_id &&
            !q->entries[i].acked && !q->entries[i].cancelled)
            return SMUX_ERR_DUPLICATE;
    }

    smux_retry_entry_t *slot = &q->entries[q->count];
    *slot = *entry;

    /* Deep copy the data buffer */
    if (entry->data && entry->data_len > 0) {
        slot->data = malloc(entry->data_len);
        if (!slot->data) return SMUX_ERR_NOMEM;
        memcpy(slot->data, entry->data, entry->data_len);
    }

    q->count++;
    return SMUX_OK;
}

smux_retry_entry_t *smux_retry_queue_find(smux_retry_queue_t *q, uint32_t seq,
                                           uint16_t channel_id) {
    if (!q) return NULL;
    for (int i = 0; i < q->count; i++) {
        if (q->entries[i].seq == seq &&
            q->entries[i].channel_id == channel_id &&
            !q->entries[i].cancelled)
            return &q->entries[i];
    }
    return NULL;
}

smux_status_t smux_retry_queue_remove(smux_retry_queue_t *q, uint32_t seq,
                                       uint16_t channel_id) {
    if (!q) return SMUX_ERR_INVALID;
    for (int i = 0; i < q->count; i++) {
        if (q->entries[i].seq == seq &&
            q->entries[i].channel_id == channel_id) {
            if (q->entries[i].data) {
                free(q->entries[i].data);
            }
            /* Compact by moving last entry here */
            q->count--;
            if (i < q->count) {
                q->entries[i] = q->entries[q->count];
            }
            memset(&q->entries[q->count], 0, sizeof(smux_retry_entry_t));
            return SMUX_OK;
        }
    }
    return SMUX_ERR_INVALID;
}

smux_status_t smux_retry_queue_cancel_channel(smux_retry_queue_t *q, uint16_t channel_id) {
    if (!q) return SMUX_ERR_INVALID;
    for (int i = 0; i < q->count; i++) {
        if (q->entries[i].channel_id == channel_id) {
            q->entries[i].cancelled = true;
        }
    }
    return SMUX_OK;
}

int smux_retry_queue_get_expired(smux_retry_queue_t *q, uint64_t now_ms,
                                  smux_retry_entry_t *out, int max_out) {
    if (!q || !out || max_out <= 0) return 0;
    int found = 0;
    for (int i = 0; i < q->count && found < max_out; i++) {
        smux_retry_entry_t *e = &q->entries[i];
        if (e->cancelled || e->acked) continue;
        if (now_ms >= e->next_retry && e->retries < SMUX_MAX_RETRY) {
            out[found] = *e;
            /* Don't deep copy data pointer — caller must use before next modify */
            found++;
            /* Update retry schedule with exponential backoff */
            e->retries++;
            uint64_t backoff = SMUX_RETRY_BASE_MS;
            for (uint8_t r = 0; r < e->retries && r < 5; r++) backoff *= 2;
            if (backoff > SMUX_RETRY_MAX_MS) backoff = SMUX_RETRY_MAX_MS;
            e->next_retry = now_ms + backoff;
        }
    }
    return found;
}

smux_status_t smux_retry_queue_update_route(smux_retry_queue_t *q, uint16_t channel_id,
                                             uint16_t old_route, uint16_t new_route) {
    if (!q) return SMUX_ERR_INVALID;
    for (int i = 0; i < q->count; i++) {
        if (q->entries[i].channel_id == channel_id &&
            q->entries[i].route_id == old_route &&
            !q->entries[i].cancelled) {
            q->entries[i].route_id = new_route;
        }
    }
    return SMUX_OK;
}
