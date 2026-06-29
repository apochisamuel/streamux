#include "receipt.h"
#include "../core/byte_utils.h"
#include <string.h>

void smux_ack_tracker_init(smux_ack_tracker_t *at, uint32_t base, uint16_t route_id) {
    if (!at) return;
    memset(at, 0, sizeof(*at));
    at->base_seq = base;
    at->route_id = route_id;
}

int smux_ack_tracker_mark(smux_ack_tracker_t *at, uint32_t seq) {
    if (!at) return 0;

    if (smux_seq_lt(seq, at->base_seq)) return 0; /* already past */

    uint32_t offset = smux_seq_diff(seq, at->base_seq);
    if (offset >= 32) return 0; /* too far ahead */

    if (at->ack_bitmap & (1u << offset)) return 0; /* already acked */

    at->ack_bitmap |= (1u << offset);
    at->total_acked++;

    if (smux_seq_gt(seq, at->highest_acked))
        at->highest_acked = seq;

    /* Advance base if contiguous from base */
    while (at->ack_bitmap & 1) {
        at->ack_bitmap >>= 1;
        at->base_seq++;
    }

    return 1;
}

int smux_ack_tracker_is_acked(const smux_ack_tracker_t *at, uint32_t seq) {
    if (!at) return 0;
    if (smux_seq_lt(seq, at->base_seq)) return 1; /* before base = already acked */
    uint32_t offset = smux_seq_diff(seq, at->base_seq);
    if (offset >= 32) return 0;
    return (at->ack_bitmap >> offset) & 1;
}

void smux_ack_tracker_advance(smux_ack_tracker_t *at) {
    if (!at) return;
    at->base_seq++;
    at->ack_bitmap >>= 1;
}

uint32_t smux_ack_tracker_pending_count(const smux_ack_tracker_t *at) {
    if (!at) return 0;
    /* Count unacked slots from base to highest_acked */
    uint32_t range = smux_seq_diff(at->highest_acked, at->base_seq);
    if (range > 32) range = 32;
    uint32_t acked = 0;
    for (uint32_t i = 0; i < range; i++) {
        if (at->ack_bitmap & (1u << i)) acked++;
    }
    return range - acked;
}
