#include "scheduler.h"
#include <stdlib.h>
#include <string.h>

smux_timer_wheel_t *smux_timer_wheel_create(uint64_t tick_ms) {
    smux_timer_wheel_t *tw = calloc(1, sizeof(smux_timer_wheel_t));
    if (!tw) return NULL;
    tw->tick_interval_ms = tick_ms > 0 ? tick_ms : 10;
    tw->next_id = 1;
    return tw;
}

void smux_timer_wheel_destroy(smux_timer_wheel_t *tw) {
    free(tw);
}

static smux_timer_entry_t *find_free_slot(smux_timer_wheel_t *tw) {
    for (int i = 0; i < SMUX_TIMER_SLOTS; i++) {
        if (!tw->slots[i].active) return &tw->slots[i];
    }
    return NULL;
}

uint32_t smux_timer_schedule(smux_timer_wheel_t *tw, uint64_t delay_ms,
                              uint64_t interval, smux_timer_cb cb, void *ctx) {
    if (!tw || !cb) return 0;
    smux_timer_entry_t *slot = find_free_slot(tw);
    if (!slot) return 0;

    slot->timer_id = tw->next_id++;
    slot->expire_at = tw->current_tick + delay_ms;
    slot->interval = interval;
    slot->callback = cb;
    slot->cb_ctx = ctx;
    slot->active = true;
    slot->cancelled = false;
    slot->channel_id = 0;
    slot->priority = 4;
    tw->count++;

    return slot->timer_id;
}

uint32_t smux_timer_schedule_for_channel(smux_timer_wheel_t *tw, uint64_t delay_ms,
                                          uint64_t interval, smux_timer_cb cb,
                                          void *ctx, uint16_t channel_id) {
    uint32_t id = smux_timer_schedule(tw, delay_ms, interval, cb, ctx);
    if (id > 0) {
        smux_timer_entry_t *entry = smux_timer_find(tw, id);
        if (entry) entry->channel_id = channel_id;
    }
    return id;
}

smux_status_t smux_timer_cancel(smux_timer_wheel_t *tw, uint32_t timer_id) {
    if (!tw) return SMUX_ERR_INVALID;
    smux_timer_entry_t *entry = smux_timer_find(tw, timer_id);
    if (!entry) return SMUX_ERR_INVALID;
    entry->cancelled = true;
    entry->active = false;
    tw->count--;
    return SMUX_OK;
}

smux_status_t smux_timer_cancel_channel(smux_timer_wheel_t *tw, uint16_t channel_id) {
    if (!tw) return SMUX_ERR_INVALID;
    for (int i = 0; i < SMUX_TIMER_SLOTS; i++) {
        if (tw->slots[i].active && tw->slots[i].channel_id == channel_id) {
            tw->slots[i].cancelled = true;
            tw->slots[i].active = false;
            tw->count--;
        }
    }
    return SMUX_OK;
}

int smux_timer_advance(smux_timer_wheel_t *tw, uint64_t now_ms) {
    if (!tw) return 0;
    tw->current_tick = now_ms;
    int fired = 0;

    for (int i = 0; i < SMUX_TIMER_SLOTS; i++) {
        smux_timer_entry_t *e = &tw->slots[i];
        if (!e->active || e->cancelled) continue;
        if (now_ms < e->expire_at) continue;

        /* Fire timer */
        e->callback(e->cb_ctx, e->timer_id);
        fired++;

        if (e->interval > 0) {
            /* Recurring: reschedule */
            e->expire_at = now_ms + e->interval;
        } else {
            /* One-shot: deactivate */
            e->active = false;
            tw->count--;
        }
    }
    return fired;
}

smux_timer_entry_t *smux_timer_find(smux_timer_wheel_t *tw, uint32_t timer_id) {
    if (!tw) return NULL;
    for (int i = 0; i < SMUX_TIMER_SLOTS; i++) {
        if (tw->slots[i].timer_id == timer_id && tw->slots[i].active)
            return &tw->slots[i];
    }
    return NULL;
}

int smux_timer_pending_count(const smux_timer_wheel_t *tw) {
    return tw ? tw->count : 0;
}
