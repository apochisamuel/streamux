#include "diagnostics.h"
#include <string.h>

/* Access stats fields by counter ID using an offset table */
#include <stdio.h>
static const size_t counter_offsets[SMUX_STAT_COUNT] = {
    offsetof(smux_stats_t, frames_received),
    offsetof(smux_stats_t, frames_sent),
    offsetof(smux_stats_t, bytes_received),
    offsetof(smux_stats_t, bytes_sent),
    offsetof(smux_stats_t, frames_dropped),
    offsetof(smux_stats_t, crc_errors),
    offsetof(smux_stats_t, parse_errors),
    offsetof(smux_stats_t, state_errors),
    offsetof(smux_stats_t, retransmissions),
    offsetof(smux_stats_t, replays_detected),
    offsetof(smux_stats_t, channels_opened),
    offsetof(smux_stats_t, channels_closed),
    offsetof(smux_stats_t, channels_errored),
    offsetof(smux_stats_t, fragments_started),
    offsetof(smux_stats_t, fragments_completed),
    offsetof(smux_stats_t, fragments_abandoned),
    offsetof(smux_stats_t, migrations_started),
    offsetof(smux_stats_t, migrations_completed),
    offsetof(smux_stats_t, key_rotations),
    offsetof(smux_stats_t, compressions),
    offsetof(smux_stats_t, decompressions),
    offsetof(smux_stats_t, backpressure_events),
    offsetof(smux_stats_t, keepalives_sent),
    offsetof(smux_stats_t, keepalives_received),
    offsetof(smux_stats_t, timer_firings),
    offsetof(smux_stats_t, window_updates),
    offsetof(smux_stats_t, priority_changes),
    offsetof(smux_stats_t, batch_dispatches),
};

static const char *counter_names[SMUX_STAT_COUNT] = {
    "frames_rx", "frames_tx", "bytes_rx", "bytes_tx",
    "drops", "crc_err", "parse_err", "state_err",
    "retx", "replays", "ch_open", "ch_close",
    "ch_error", "frag_start", "frag_done", "frag_drop",
    "mig_start", "mig_done", "key_rot", "compress",
    "decompress", "backpressure", "ka_tx", "ka_rx",
    "timer_fire", "win_upd", "pri_chg", "batch"
};

void smux_diag_count(smux_diagnostics_t *d, int counter_id) {
    smux_diag_count_n(d, counter_id, 1);
}

void smux_diag_count_n(smux_diagnostics_t *d, int counter_id, uint64_t n) {
    if (!d || !d->enabled) return;
    if (counter_id < 0 || counter_id >= SMUX_STAT_COUNT) return;
    uint64_t *counter = (uint64_t *)((uint8_t *)&d->stats + counter_offsets[counter_id]);
    *counter += n;
}

uint64_t smux_diag_get(const smux_diagnostics_t *d, int counter_id) {
    if (!d || counter_id < 0 || counter_id >= SMUX_STAT_COUNT) return 0;
    const uint64_t *counter = (const uint64_t *)((const uint8_t *)&d->stats +
                                                  counter_offsets[counter_id]);
    return *counter;
}

int smux_diag_report(const smux_diagnostics_t *d, char *buf, size_t buf_len) {
    if (!d || !buf || buf_len == 0) return 0;

    int total = 0;
    int remaining = (int)buf_len;

    for (int i = 0; i < SMUX_STAT_COUNT; i++) {
        uint64_t val = smux_diag_get(d, i);
        if (val == 0) continue;

        int n = snprintf(buf + total, (size_t)remaining, "%s: %llu\n",
                        counter_names[i], (unsigned long long)val);
        if (n < 0 || n >= remaining) break;
        total += n;
        remaining -= n;
    }
    return total;
}

void smux_diag_update_channel(smux_diagnostics_t *d, uint16_t channel_id,
                               int stat_type, uint64_t value) {
    if (!d || !d->enabled) return;
    /* Channel stats are tracked via the global counters.
       Per-channel breakdown uses the transcript subsystem. */
    smux_diag_count_n(d, stat_type, value);
    (void)channel_id;
}

smux_status_t smux_diag_get_channel_stats(const smux_diagnostics_t *d,
                                           uint16_t channel_id,
                                           smux_channel_stats_t *out) {
    if (!d || !out) return SMUX_ERR_INVALID;
    /* Channel stats not tracked separately — return zero-initialized */
    memset(out, 0, sizeof(*out));
    out->channel_id = channel_id;
    return SMUX_OK;
}
