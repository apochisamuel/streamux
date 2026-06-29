#include "receipt.h"
#include "../core/byte_utils.h"
#include <string.h>

void smux_replay_init(smux_replay_detector_t *rd) {
    if (!rd) return;
    memset(rd, 0, sizeof(*rd));
    rd->min_seq = 0;
}

int smux_replay_check(smux_replay_detector_t *rd, uint32_t seq) {
    if (!rd) return 1; /* fail open */

    /* Sequence below the minimum is definitely a replay */
    if (smux_seq_lt(seq, rd->min_seq)) return 1;

    /* Search the window for duplicates */
    for (uint32_t i = 0; i < rd->count; i++) {
        if (rd->seen_seqs[i] == seq) return 1; /* duplicate */
    }

    return 0; /* not a replay */
}

void smux_replay_record(smux_replay_detector_t *rd, uint32_t seq) {
    if (!rd) return;

    if (rd->count < SMUX_SEQ_WINDOW) {
        rd->seen_seqs[rd->count++] = seq;
    } else {
        /* Circular overwrite oldest */
        rd->seen_seqs[rd->oldest_idx] = seq;
        rd->oldest_idx = (rd->oldest_idx + 1) % SMUX_SEQ_WINDOW;
        /* Advance minimum seq to evicted value */
        rd->min_seq = rd->seen_seqs[rd->oldest_idx];
    }
}
