#include "scheduler.h"
#include "../core/byte_utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Hierarchical timer queue: supplements the flat timer wheel
   with a sorted queue for long-duration timers (keepalive,
   rotation deadlines, migration timeouts). */

typedef struct {
    uint32_t    timer_id;
    uint64_t    fire_at;
    uint64_t    created_at;
    uint32_t    tag;        /* user-defined classification */
    void       *ctx;
    smux_timer_cb callback;
    bool        active;
    bool        fired;
} smux_tq_entry_t;

typedef struct {
    smux_tq_entry_t entries[128];
    int              count;
    int              capacity;
    uint32_t         next_id;
    uint64_t         earliest_fire;
    int              fired_count;
    int              cancelled_count;
} smux_timer_queue_t;

smux_timer_queue_t *smux_tq_create(void) {
    smux_timer_queue_t *tq = calloc(1, sizeof(smux_timer_queue_t));
    if (!tq) return NULL;
    tq->capacity = 128;
    tq->next_id = 1;
    tq->earliest_fire = UINT64_MAX;
    return tq;
}

void smux_tq_destroy(smux_timer_queue_t *tq) {
    free(tq);
}

uint32_t smux_tq_add(smux_timer_queue_t *tq, uint64_t fire_at,
                      smux_timer_cb cb, void *ctx, uint32_t tag) {
    if (!tq || !cb || tq->count >= tq->capacity) return 0;

    /* Insert in sorted order by fire_at */
    int insert_pos = tq->count;
    for (int i = 0; i < tq->count; i++) {
        if (fire_at < tq->entries[i].fire_at) {
            insert_pos = i;
            break;
        }
    }

    /* Shift entries after insert point */
    if (insert_pos < tq->count) {
        memmove(&tq->entries[insert_pos + 1], &tq->entries[insert_pos],
                sizeof(smux_tq_entry_t) * (size_t)(tq->count - insert_pos));
    }

    smux_tq_entry_t *e = &tq->entries[insert_pos];
    e->timer_id = tq->next_id++;
    e->fire_at = fire_at;
    e->created_at = 0;
    e->tag = tag;
    e->ctx = ctx;
    e->callback = cb;
    e->active = true;
    e->fired = false;

    tq->count++;

    if (fire_at < tq->earliest_fire) {
        tq->earliest_fire = fire_at;
    }

    return e->timer_id;
}

int smux_tq_cancel(smux_timer_queue_t *tq, uint32_t timer_id) {
    if (!tq) return SMUX_ERR_INVALID;
    for (int i = 0; i < tq->count; i++) {
        if (tq->entries[i].timer_id == timer_id && tq->entries[i].active) {
            tq->entries[i].active = false;
            tq->cancelled_count++;
            return SMUX_OK;
        }
    }
    return SMUX_ERR_INVALID;
}

int smux_tq_cancel_tag(smux_timer_queue_t *tq, uint32_t tag) {
    if (!tq) return 0;
    int cancelled = 0;
    for (int i = 0; i < tq->count; i++) {
        if (tq->entries[i].tag == tag && tq->entries[i].active) {
            tq->entries[i].active = false;
            cancelled++;
            tq->cancelled_count++;
        }
    }
    return cancelled;
}

int smux_tq_advance(smux_timer_queue_t *tq, uint64_t now_ms) {
    if (!tq) return 0;
    int fired = 0;

    /* Process from front (sorted, so earliest first) */
    while (tq->count > 0 && tq->entries[0].fire_at <= now_ms) {
        smux_tq_entry_t e = tq->entries[0];

        /* Remove from front */
        tq->count--;
        if (tq->count > 0) {
            memmove(&tq->entries[0], &tq->entries[1],
                    sizeof(smux_tq_entry_t) * (size_t)tq->count);
        }

        if (e.active) {
            e.callback(e.ctx, e.timer_id);
            fired++;
            tq->fired_count++;
        }
    }

    /* Update earliest */
    tq->earliest_fire = tq->count > 0 ? tq->entries[0].fire_at : UINT64_MAX;

    return fired;
}

uint64_t smux_tq_next_fire(const smux_timer_queue_t *tq) {
    return tq ? tq->earliest_fire : UINT64_MAX;
}

int smux_tq_pending(const smux_timer_queue_t *tq) {
    if (!tq) return 0;
    int active = 0;
    for (int i = 0; i < tq->count; i++) {
        if (tq->entries[i].active) active++;
    }
    return active;
}

int smux_tq_report(const smux_timer_queue_t *tq, char *buf, size_t len) {
    if (!tq || !buf || len == 0) return 0;
    return snprintf(buf, len,
        "Timer queue: %d entries, %d pending, %d fired, %d cancelled\n"
        "  Next fire at: %llu\n",
        tq->count, smux_tq_pending(tq),
        tq->fired_count, tq->cancelled_count,
        (unsigned long long)tq->earliest_fire
    );
}

/* Compact: remove inactive entries to reclaim slots */
int smux_tq_compact(smux_timer_queue_t *tq) {
    if (!tq) return 0;
    int removed = 0;
    int write = 0;

    for (int read = 0; read < tq->count; read++) {
        if (tq->entries[read].active) {
            if (write != read) {
                tq->entries[write] = tq->entries[read];
            }
            write++;
        } else {
            removed++;
        }
    }
    tq->count = write;
    return removed;
}
