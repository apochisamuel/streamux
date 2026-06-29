#ifndef SMUX_SCHEDULER_H
#define SMUX_SCHEDULER_H

#include "../core/types.h"
#include "../core/status.h"

/* Timer wheel for scheduling retransmissions and keepalives */

typedef void (*smux_timer_cb)(void *ctx, uint32_t timer_id);

typedef struct {
    uint32_t        timer_id;
    uint64_t        expire_at;
    uint64_t        interval;   /* 0 = one-shot */
    smux_timer_cb   callback;
    void           *cb_ctx;
    bool            active;
    bool            cancelled;
    uint16_t        channel_id;
    uint8_t         priority;
} smux_timer_entry_t;

typedef struct {
    smux_timer_entry_t slots[SMUX_TIMER_SLOTS];
    int                 count;
    uint32_t            next_id;
    uint64_t            current_tick;
    uint64_t            tick_interval_ms;
} smux_timer_wheel_t;

smux_timer_wheel_t *smux_timer_wheel_create(uint64_t tick_ms);
void smux_timer_wheel_destroy(smux_timer_wheel_t *tw);
uint32_t smux_timer_schedule(smux_timer_wheel_t *tw, uint64_t delay_ms,
                              uint64_t interval, smux_timer_cb cb, void *ctx);
uint32_t smux_timer_schedule_for_channel(smux_timer_wheel_t *tw, uint64_t delay_ms,
                                          uint64_t interval, smux_timer_cb cb,
                                          void *ctx, uint16_t channel_id);
smux_status_t smux_timer_cancel(smux_timer_wheel_t *tw, uint32_t timer_id);
smux_status_t smux_timer_cancel_channel(smux_timer_wheel_t *tw, uint16_t channel_id);
int smux_timer_advance(smux_timer_wheel_t *tw, uint64_t now_ms);
smux_timer_entry_t *smux_timer_find(smux_timer_wheel_t *tw, uint32_t timer_id);
int smux_timer_pending_count(const smux_timer_wheel_t *tw);

/* Priority queue for delivery ordering */
typedef struct {
    uint32_t    seq;
    uint16_t    channel_id;
    uint8_t     priority;
    uint32_t    payload_size;
    void       *delivery_token;
    bool        active;
} smux_pq_entry_t;

typedef struct {
    smux_pq_entry_t heap[SMUX_MAX_CHANNELS * 4];
    int             count;
    int             capacity;
} smux_priority_queue_t;

smux_priority_queue_t *smux_pq_create(void);
void smux_pq_destroy(smux_priority_queue_t *pq);
smux_status_t smux_pq_insert(smux_priority_queue_t *pq, const smux_pq_entry_t *entry);
smux_status_t smux_pq_pop(smux_priority_queue_t *pq, smux_pq_entry_t *out);
smux_status_t smux_pq_peek(const smux_priority_queue_t *pq, smux_pq_entry_t *out);
smux_status_t smux_pq_remove_channel(smux_priority_queue_t *pq, uint16_t channel_id);
smux_status_t smux_pq_update_priority(smux_priority_queue_t *pq, uint16_t channel_id,
                                       uint8_t new_priority);
int smux_pq_is_empty(const smux_priority_queue_t *pq);

/* Delayed delivery scheduler */
typedef struct {
    uint32_t    seq;
    uint16_t    channel_id;
    uint64_t    deliver_at;
    uint8_t    *data;
    size_t      data_len;
    bool        delivered;
    bool        cancelled;
} smux_delayed_entry_t;

typedef struct {
    smux_delayed_entry_t entries[SMUX_MAX_CHANNELS];
    int                  count;
} smux_delayed_queue_t;

smux_delayed_queue_t *smux_delayed_create(void);
void smux_delayed_destroy(smux_delayed_queue_t *dq);
smux_status_t smux_delayed_add(smux_delayed_queue_t *dq, uint32_t seq,
                                uint16_t channel_id, uint64_t deliver_at,
                                const uint8_t *data, size_t len);
int smux_delayed_process(smux_delayed_queue_t *dq, uint64_t now_ms,
                          smux_delayed_entry_t *out, int max_out);
smux_status_t smux_delayed_cancel_channel(smux_delayed_queue_t *dq, uint16_t channel_id);

#endif
