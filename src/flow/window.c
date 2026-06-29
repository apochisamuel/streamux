#include "flow.h"
#include "../core/byte_utils.h"

void smux_recv_window_init(smux_recv_window_t *rw, uint32_t base, uint32_t size) {
    if (!rw) return;
    rw->base_seq = base;
    rw->window_size = size;
    rw->ack_seq = base;
    rw->bitmap = 0;
    rw->delivered = 0;
    rw->reordered = 0;
}

int smux_recv_window_accept(smux_recv_window_t *rw, uint32_t seq) {
    if (!rw) return 0;

    /* Check if seq is within the receive window */
    if (!smux_seq_in_window(seq, rw->ack_seq, rw->window_size))
        return 0;

    uint32_t offset = smux_seq_diff(seq, rw->ack_seq);

    if (offset == 0) {
        /* In-order delivery: advance ack_seq */
        rw->ack_seq++;
        rw->delivered++;

        /* Advance past any contiguous received packets in bitmap */
        while (rw->bitmap & 1) {
            rw->bitmap >>= 1;
            rw->ack_seq++;
            rw->delivered++;
        }
        return 1;
    }

    if (offset > 0 && offset <= 32) {
        /* Out-of-order: mark in selective ack bitmap */
        uint32_t bit = offset - 1;
        if (rw->bitmap & (1u << bit)) return 0; /* duplicate */
        rw->bitmap |= (1u << bit);
        rw->reordered++;
        return 1;
    }

    /* Beyond bitmap range — too far ahead */
    return 0;
}

void smux_recv_window_advance(smux_recv_window_t *rw) {
    if (!rw) return;
    /* Force advance one position (for NAK-based recovery) */
    rw->ack_seq++;
    if (rw->bitmap & 1) {
        rw->delivered++;
    }
    rw->bitmap >>= 1;
}

int smux_recv_window_contains(const smux_recv_window_t *rw, uint32_t seq) {
    if (!rw) return 0;
    return smux_seq_in_window(seq, rw->ack_seq, rw->window_size);
}

uint32_t smux_recv_window_gap_count(const smux_recv_window_t *rw) {
    if (!rw) return 0;
    /* Count gaps: positions in bitmap range that are 0 before the last 1 */
    uint32_t gaps = 0;
    uint32_t bmp = rw->bitmap;
    int last_one = -1;

    for (int i = 31; i >= 0; i--) {
        if (bmp & (1u << i)) {
            last_one = i;
            break;
        }
    }

    if (last_one < 0) return 0;

    for (int i = 0; i <= last_one; i++) {
        if (!(bmp & (1u << i))) gaps++;
    }
    return gaps;
}
