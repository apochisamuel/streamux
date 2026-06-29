#ifndef SMUX_RECEIPT_H
#define SMUX_RECEIPT_H

#include "../core/types.h"
#include "../core/status.h"

/* Ack tracking: maps sent sequences to ack status */
typedef struct {
    uint32_t    base_seq;
    uint32_t    ack_bitmap;     /* 32-bit selective ack */
    uint32_t    highest_acked;
    uint32_t    total_acked;
    uint32_t    total_nacked;
    uint32_t    total_sent;
    uint16_t    route_id;
} smux_ack_tracker_t;

void smux_ack_tracker_init(smux_ack_tracker_t *at, uint32_t base, uint16_t route_id);
int smux_ack_tracker_mark(smux_ack_tracker_t *at, uint32_t seq);
int smux_ack_tracker_is_acked(const smux_ack_tracker_t *at, uint32_t seq);
void smux_ack_tracker_advance(smux_ack_tracker_t *at);
uint32_t smux_ack_tracker_pending_count(const smux_ack_tracker_t *at);

/* Replay detection: prevents processing duplicate receipts */
typedef struct {
    uint32_t    seen_seqs[SMUX_SEQ_WINDOW];
    uint32_t    count;
    uint32_t    oldest_idx;
    uint32_t    min_seq;
} smux_replay_detector_t;

void smux_replay_init(smux_replay_detector_t *rd);
int smux_replay_check(smux_replay_detector_t *rd, uint32_t seq);
void smux_replay_record(smux_replay_detector_t *rd, uint32_t seq);

/* Retry queue for unacked packets */
typedef struct {
    smux_retry_entry_t entries[SMUX_MAX_RETRY * SMUX_MAX_CHANNELS];
    int                count;
    int                capacity;
} smux_retry_queue_t;

smux_retry_queue_t *smux_retry_queue_create(void);
void smux_retry_queue_destroy(smux_retry_queue_t *q);
smux_status_t smux_retry_queue_push(smux_retry_queue_t *q, const smux_retry_entry_t *entry);
smux_retry_entry_t *smux_retry_queue_find(smux_retry_queue_t *q, uint32_t seq, uint16_t channel_id);
smux_status_t smux_retry_queue_remove(smux_retry_queue_t *q, uint32_t seq, uint16_t channel_id);
smux_status_t smux_retry_queue_cancel_channel(smux_retry_queue_t *q, uint16_t channel_id);
int smux_retry_queue_get_expired(smux_retry_queue_t *q, uint64_t now_ms,
                                  smux_retry_entry_t *out, int max_out);
smux_status_t smux_retry_queue_update_route(smux_retry_queue_t *q, uint16_t channel_id,
                                             uint16_t old_route, uint16_t new_route);

#endif
