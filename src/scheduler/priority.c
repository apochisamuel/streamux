#include "scheduler.h"
#include <stdlib.h>
#include <string.h>

smux_priority_queue_t *smux_pq_create(void) {
    smux_priority_queue_t *pq = calloc(1, sizeof(smux_priority_queue_t));
    if (!pq) return NULL;
    pq->capacity = SMUX_MAX_CHANNELS * 4;
    return pq;
}

void smux_pq_destroy(smux_priority_queue_t *pq) {
    free(pq);
}

static void pq_swap(smux_pq_entry_t *a, smux_pq_entry_t *b) {
    smux_pq_entry_t tmp = *a;
    *a = *b;
    *b = tmp;
}

/* Min-heap by priority (lower = higher priority), then by sequence */
static int pq_less(const smux_pq_entry_t *a, const smux_pq_entry_t *b) {
    if (a->priority != b->priority) return a->priority < b->priority;
    return a->seq < b->seq;
}

static void pq_sift_up(smux_priority_queue_t *pq, int idx) {
    while (idx > 0) {
        int parent = (idx - 1) / 2;
        if (pq_less(&pq->heap[idx], &pq->heap[parent])) {
            pq_swap(&pq->heap[idx], &pq->heap[parent]);
            idx = parent;
        } else break;
    }
}

static void pq_sift_down(smux_priority_queue_t *pq, int idx) {
    while (1) {
        int left = 2 * idx + 1;
        int right = 2 * idx + 2;
        int smallest = idx;

        if (left < pq->count && pq_less(&pq->heap[left], &pq->heap[smallest]))
            smallest = left;
        if (right < pq->count && pq_less(&pq->heap[right], &pq->heap[smallest]))
            smallest = right;

        if (smallest != idx) {
            pq_swap(&pq->heap[idx], &pq->heap[smallest]);
            idx = smallest;
        } else break;
    }
}

smux_status_t smux_pq_insert(smux_priority_queue_t *pq, const smux_pq_entry_t *entry) {
    if (!pq || !entry) return SMUX_ERR_INVALID;
    if (pq->count >= pq->capacity) return SMUX_ERR_OVERFLOW;

    pq->heap[pq->count] = *entry;
    pq->heap[pq->count].active = true;
    pq_sift_up(pq, pq->count);
    pq->count++;
    return SMUX_OK;
}

smux_status_t smux_pq_pop(smux_priority_queue_t *pq, smux_pq_entry_t *out) {
    if (!pq || !out) return SMUX_ERR_INVALID;

    /* Skip inactive entries at top */
    while (pq->count > 0 && !pq->heap[0].active) {
        pq->count--;
        pq->heap[0] = pq->heap[pq->count];
        pq_sift_down(pq, 0);
    }

    if (pq->count == 0) return SMUX_ERR_INVALID;

    *out = pq->heap[0];
    pq->count--;
    if (pq->count > 0) {
        pq->heap[0] = pq->heap[pq->count];
        pq_sift_down(pq, 0);
    }
    return SMUX_OK;
}

smux_status_t smux_pq_peek(const smux_priority_queue_t *pq, smux_pq_entry_t *out) {
    if (!pq || !out || pq->count == 0) return SMUX_ERR_INVALID;
    *out = pq->heap[0];
    return SMUX_OK;
}

smux_status_t smux_pq_remove_channel(smux_priority_queue_t *pq, uint16_t channel_id) {
    if (!pq) return SMUX_ERR_INVALID;
    for (int i = 0; i < pq->count; i++) {
        if (pq->heap[i].channel_id == channel_id) {
            pq->heap[i].active = false;
        }
    }
    return SMUX_OK;
}

smux_status_t smux_pq_update_priority(smux_priority_queue_t *pq, uint16_t channel_id,
                                       uint8_t new_priority) {
    if (!pq) return SMUX_ERR_INVALID;
    for (int i = 0; i < pq->count; i++) {
        if (pq->heap[i].channel_id == channel_id && pq->heap[i].active) {
            pq->heap[i].priority = new_priority;
        }
    }
    /* Rebuild heap after priority changes */
    for (int i = pq->count / 2 - 1; i >= 0; i--) {
        pq_sift_down(pq, i);
    }
    return SMUX_OK;
}

int smux_pq_is_empty(const smux_priority_queue_t *pq) {
    if (!pq) return 1;
    for (int i = 0; i < pq->count; i++) {
        if (pq->heap[i].active) return 0;
    }
    return 1;
}
