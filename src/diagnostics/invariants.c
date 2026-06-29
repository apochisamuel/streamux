#include "diagnostics.h"
#include "../core/types.h"

/* Runtime invariant checks for protocol state consistency */

smux_status_t smux_diag_check_invariants(const smux_diagnostics_t *d) {
    if (!d) return SMUX_ERR_INVALID;

    const smux_stats_t *s = &d->stats;

    /* Frames received should be >= sum of sub-categories */
    uint64_t total_classified = s->crc_errors + s->parse_errors + s->frames_dropped;
    if (total_classified > s->frames_received && s->frames_received > 0) {
        return SMUX_ERR_INTERNAL;
    }

    /* Channels opened should be >= channels closed + channels errored */
    uint64_t ended = s->channels_closed + s->channels_errored;
    if (ended > s->channels_opened && s->channels_opened > 0) {
        return SMUX_ERR_INTERNAL;
    }

    /* Fragments completed + abandoned should be <= fragments started */
    uint64_t frag_ended = s->fragments_completed + s->fragments_abandoned;
    if (frag_ended > s->fragments_started && s->fragments_started > 0) {
        return SMUX_ERR_INTERNAL;
    }

    /* Migrations completed should be <= migrations started */
    if (s->migrations_completed > s->migrations_started && s->migrations_started > 0) {
        return SMUX_ERR_INTERNAL;
    }

    /* Bytes sent should correspond roughly to frames sent */
    if (s->frames_sent > 0 && s->bytes_sent == 0) {
        return SMUX_ERR_INTERNAL;
    }

    return SMUX_OK;
}
