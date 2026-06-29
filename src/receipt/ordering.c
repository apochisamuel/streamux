#include "receipt.h"
#include "../core/byte_utils.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Receipt ordering: manages causality tracking for acknowledgments.
   Ensures that receipts are processed in the correct order
   relative to the frames they acknowledge, even when frames
   arrive out of order across multiple routes. */

typedef struct {
    uint32_t    seq;
    uint16_t    channel_id;
    uint16_t    route_id;
    uint64_t    sent_at;
    uint64_t    acked_at;
    bool        received;
    bool        timed_out;
} smux_receipt_entry_t;

typedef struct {
    smux_receipt_entry_t entries[512];
    int                  count;
    int                  capacity;
    uint32_t             base_seq;
    uint32_t             highest_acked;
    uint32_t             highest_sent;
    uint64_t             total_acked;
    uint64_t             total_sent;
    uint64_t             total_timed_out;
    uint64_t             cumulative_latency;
    int                  latency_samples;
} smux_receipt_tracker_t;

smux_receipt_tracker_t *smux_receipt_tracker_create(uint32_t base_seq) {
    smux_receipt_tracker_t *rt = calloc(1, sizeof(smux_receipt_tracker_t));
    if (!rt) return NULL;
    rt->capacity = 512;
    rt->base_seq = base_seq;
    return rt;
}

void smux_receipt_tracker_destroy(smux_receipt_tracker_t *rt) {
    free(rt);
}

int smux_receipt_tracker_record_send(smux_receipt_tracker_t *rt,
                                      uint32_t seq, uint16_t channel_id,
                                      uint16_t route_id, uint64_t now_ms) {
    if (!rt || rt->count >= rt->capacity) return SMUX_ERR_OVERFLOW;

    int idx = rt->count;
    rt->entries[idx].seq = seq;
    rt->entries[idx].channel_id = channel_id;
    rt->entries[idx].route_id = route_id;
    rt->entries[idx].sent_at = now_ms;
    rt->entries[idx].acked_at = 0;
    rt->entries[idx].received = false;
    rt->entries[idx].timed_out = false;
    rt->count++;

    if (seq > rt->highest_sent) rt->highest_sent = seq;
    rt->total_sent++;
    return SMUX_OK;
}

int smux_receipt_tracker_record_ack(smux_receipt_tracker_t *rt,
                                     uint32_t seq, uint64_t now_ms) {
    if (!rt) return SMUX_ERR_INVALID;

    for (int i = 0; i < rt->count; i++) {
        if (rt->entries[i].seq == seq && !rt->entries[i].received) {
            rt->entries[i].received = true;
            rt->entries[i].acked_at = now_ms;

            uint64_t latency = now_ms - rt->entries[i].sent_at;
            rt->cumulative_latency += latency;
            rt->latency_samples++;

            if (seq > rt->highest_acked) rt->highest_acked = seq;
            rt->total_acked++;
            return SMUX_OK;
        }
    }
    return SMUX_ERR_INVALID; /* unknown seq */
}

int smux_receipt_tracker_mark_timeout(smux_receipt_tracker_t *rt,
                                       uint64_t now_ms, uint64_t timeout_ms) {
    if (!rt) return 0;
    int timed_out = 0;

    for (int i = 0; i < rt->count; i++) {
        if (!rt->entries[i].received && !rt->entries[i].timed_out) {
            if (now_ms - rt->entries[i].sent_at > timeout_ms) {
                rt->entries[i].timed_out = true;
                rt->total_timed_out++;
                timed_out++;
            }
        }
    }
    return timed_out;
}

/* Get the contiguous acked frontier: highest seq where all previous are acked */
uint32_t smux_receipt_tracker_frontier(const smux_receipt_tracker_t *rt) {
    if (!rt || rt->count == 0) return rt ? rt->base_seq : 0;

    uint32_t frontier = rt->base_seq;
    for (uint32_t s = rt->base_seq; s <= rt->highest_sent; s++) {
        bool found_acked = false;
        for (int i = 0; i < rt->count; i++) {
            if (rt->entries[i].seq == s) {
                if (rt->entries[i].received) {
                    found_acked = true;
                }
                break;
            }
        }
        if (found_acked) {
            frontier = s;
        } else {
            break;
        }
    }
    return frontier;
}

/* Get average round-trip time in milliseconds */
uint64_t smux_receipt_tracker_avg_rtt(const smux_receipt_tracker_t *rt) {
    if (!rt || rt->latency_samples == 0) return 0;
    return rt->cumulative_latency / (uint64_t)rt->latency_samples;
}

/* Get loss rate as a fraction */
float smux_receipt_tracker_loss_rate(const smux_receipt_tracker_t *rt) {
    if (!rt || rt->total_sent == 0) return 0.0f;
    return (float)rt->total_timed_out / (float)rt->total_sent;
}

/* Compact: remove fully acked entries below frontier */
int smux_receipt_tracker_compact(smux_receipt_tracker_t *rt) {
    if (!rt) return 0;

    uint32_t frontier = smux_receipt_tracker_frontier(rt);
    int removed = 0;
    int write = 0;

    for (int read = 0; read < rt->count; read++) {
        if (rt->entries[read].seq <= frontier && rt->entries[read].received) {
            removed++;
        } else {
            if (write != read) {
                rt->entries[write] = rt->entries[read];
            }
            write++;
        }
    }
    rt->count = write;
    return removed;
}

int smux_receipt_tracker_report(const smux_receipt_tracker_t *rt,
                                 char *buf, size_t len) {
    if (!rt || !buf || len == 0) return 0;
    return snprintf(buf, len,
        "Receipt tracker: sent=%llu acked=%llu timedout=%llu\n"
        "  Frontier: %u  Highest sent: %u  Highest acked: %u\n"
        "  Avg RTT: %llu ms  Loss rate: %.2f%%\n"
        "  Entries: %d / %d\n",
        (unsigned long long)rt->total_sent,
        (unsigned long long)rt->total_acked,
        (unsigned long long)rt->total_timed_out,
        smux_receipt_tracker_frontier(rt),
        rt->highest_sent, rt->highest_acked,
        (unsigned long long)smux_receipt_tracker_avg_rtt(rt),
        smux_receipt_tracker_loss_rate(rt) * 100.0f,
        rt->count, rt->capacity
    );
}
