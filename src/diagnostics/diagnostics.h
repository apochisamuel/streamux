#ifndef SMUX_DIAGNOSTICS_H
#define SMUX_DIAGNOSTICS_H

#include "../core/types.h"
#include "../core/status.h"

/* Statistics counters */
typedef struct {
    uint64_t    frames_received;
    uint64_t    frames_sent;
    uint64_t    bytes_received;
    uint64_t    bytes_sent;
    uint64_t    frames_dropped;
    uint64_t    crc_errors;
    uint64_t    parse_errors;
    uint64_t    state_errors;
    uint64_t    retransmissions;
    uint64_t    replays_detected;
    uint64_t    channels_opened;
    uint64_t    channels_closed;
    uint64_t    channels_errored;
    uint64_t    fragments_started;
    uint64_t    fragments_completed;
    uint64_t    fragments_abandoned;
    uint64_t    migrations_started;
    uint64_t    migrations_completed;
    uint64_t    key_rotations;
    uint64_t    compressions;
    uint64_t    decompressions;
    uint64_t    backpressure_events;
    uint64_t    keepalives_sent;
    uint64_t    keepalives_received;
    uint64_t    timer_firings;
    uint64_t    window_updates;
    uint64_t    priority_changes;
    uint64_t    batch_dispatches;
} smux_stats_t;

typedef struct {
    smux_stats_t    stats;
    bool            enabled;
    uint64_t        start_time;
    uint64_t        last_report;
} smux_diagnostics_t;

smux_diagnostics_t *smux_diag_create(void);
void smux_diag_destroy(smux_diagnostics_t *d);
void smux_diag_reset(smux_diagnostics_t *d);
void smux_diag_enable(smux_diagnostics_t *d);
void smux_diag_disable(smux_diagnostics_t *d);

void smux_diag_count(smux_diagnostics_t *d, int counter_id);
void smux_diag_count_n(smux_diagnostics_t *d, int counter_id, uint64_t n);
uint64_t smux_diag_get(const smux_diagnostics_t *d, int counter_id);

/* Counter IDs */
#define SMUX_STAT_FRAMES_RX     0
#define SMUX_STAT_FRAMES_TX     1
#define SMUX_STAT_BYTES_RX      2
#define SMUX_STAT_BYTES_TX      3
#define SMUX_STAT_DROPS         4
#define SMUX_STAT_CRC_ERR       5
#define SMUX_STAT_PARSE_ERR     6
#define SMUX_STAT_STATE_ERR     7
#define SMUX_STAT_RETX          8
#define SMUX_STAT_REPLAYS       9
#define SMUX_STAT_CH_OPEN       10
#define SMUX_STAT_CH_CLOSE      11
#define SMUX_STAT_CH_ERROR      12
#define SMUX_STAT_FRAG_START    13
#define SMUX_STAT_FRAG_DONE     14
#define SMUX_STAT_FRAG_DROP     15
#define SMUX_STAT_MIG_START     16
#define SMUX_STAT_MIG_DONE      17
#define SMUX_STAT_KEY_ROT       18
#define SMUX_STAT_COMPRESS      19
#define SMUX_STAT_DECOMPRESS    20
#define SMUX_STAT_BACKPRES      21
#define SMUX_STAT_KA_TX         22
#define SMUX_STAT_KA_RX         23
#define SMUX_STAT_TIMER         24
#define SMUX_STAT_WIN_UPD       25
#define SMUX_STAT_PRI_CHG       26
#define SMUX_STAT_BATCH         27
#define SMUX_STAT_COUNT         28

/* Invariant checking */
smux_status_t smux_diag_check_invariants(const smux_diagnostics_t *d);
int smux_diag_report(const smux_diagnostics_t *d, char *buf, size_t buf_len);

/* Per-channel stats */
typedef struct {
    uint16_t    channel_id;
    uint64_t    bytes_in;
    uint64_t    bytes_out;
    uint32_t    fragments_in;
    uint32_t    retransmits;
    uint32_t    window_stalls;
    uint8_t     current_priority;
    uint16_t    current_route;
} smux_channel_stats_t;

void smux_diag_update_channel(smux_diagnostics_t *d, uint16_t channel_id,
                               int stat_type, uint64_t value);
smux_status_t smux_diag_get_channel_stats(const smux_diagnostics_t *d,
                                           uint16_t channel_id,
                                           smux_channel_stats_t *out);

#endif
