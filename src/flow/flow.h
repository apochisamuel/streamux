#ifndef SMUX_FLOW_H
#define SMUX_FLOW_H

#include "../core/types.h"
#include "../core/status.h"

typedef struct {
    uint32_t    window_size;
    uint32_t    window_used;
    uint32_t    credit_available;
    uint32_t    high_water;
    uint32_t    low_water;
    bool        backpressure_active;
    uint32_t    total_granted;
    uint32_t    total_consumed;
    uint32_t    stall_count;
} smux_flow_ctrl_t;

void smux_flow_init(smux_flow_ctrl_t *fc, uint32_t window);
smux_status_t smux_flow_consume(smux_flow_ctrl_t *fc, uint32_t amount);
smux_status_t smux_flow_release(smux_flow_ctrl_t *fc, uint32_t amount);
smux_status_t smux_flow_update_window(smux_flow_ctrl_t *fc, uint32_t new_window);
uint32_t smux_flow_available(const smux_flow_ctrl_t *fc);
int smux_flow_is_blocked(const smux_flow_ctrl_t *fc);
smux_status_t smux_flow_grant_credit(smux_flow_ctrl_t *fc, uint32_t amount);
int smux_flow_should_advertise(const smux_flow_ctrl_t *fc);
uint32_t smux_flow_compute_credit(const smux_flow_ctrl_t *fc);

/* Window tracking for receive side */
typedef struct {
    uint32_t    base_seq;
    uint32_t    window_size;
    uint32_t    ack_seq;
    uint32_t    bitmap;     /* selective ack bitmap, 32 slots beyond ack_seq */
    uint32_t    delivered;
    uint32_t    reordered;
} smux_recv_window_t;

void smux_recv_window_init(smux_recv_window_t *rw, uint32_t base, uint32_t size);
int smux_recv_window_accept(smux_recv_window_t *rw, uint32_t seq);
void smux_recv_window_advance(smux_recv_window_t *rw);
int smux_recv_window_contains(const smux_recv_window_t *rw, uint32_t seq);
uint32_t smux_recv_window_gap_count(const smux_recv_window_t *rw);

/* Backpressure signaling */
typedef struct {
    uint16_t    channel_id;
    uint32_t    pressure_level;
    bool        paused;
    uint64_t    paused_at;
    uint64_t    resumed_at;
    uint32_t    pause_count;
} smux_backpressure_t;

void smux_backpressure_init(smux_backpressure_t *bp, uint16_t channel_id);
int smux_backpressure_check(smux_backpressure_t *bp, const smux_flow_ctrl_t *fc);
void smux_backpressure_pause(smux_backpressure_t *bp, uint64_t now_ms);
void smux_backpressure_resume(smux_backpressure_t *bp, uint64_t now_ms);

#endif
