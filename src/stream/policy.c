#include "stream.h"
#include "../diagnostics/diagnostics.h"
#include "../core/byte_utils.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Connection policy engine: enforces configurable limits
   and triggers automatic responses to protocol violations. */

typedef struct {
    uint32_t    max_frame_rate;     /* frames per second */
    uint32_t    max_byte_rate;      /* bytes per second */
    uint32_t    max_channels;
    uint32_t    max_pending_frags;
    uint32_t    max_retry_count;
    uint32_t    idle_timeout_ms;
    uint32_t    migration_timeout_ms;
    uint32_t    max_batch_size;
    uint32_t    min_keepalive_ms;
    uint32_t    max_keepalive_ms;
    bool        allow_compression;
    bool        allow_encryption;
    bool        allow_migration;
    bool        enforce_ordering;
    bool        auto_close_idle;
} smux_policy_t;

typedef struct {
    uint64_t    window_start;
    uint32_t    frame_count;
    uint32_t    byte_count;
    uint32_t    violation_count;
    uint32_t    channels_opened;
    bool        throttled;
    bool        blocked;
} smux_rate_tracker_t;

void smux_policy_default(smux_policy_t *p) {
    if (!p) return;
    memset(p, 0, sizeof(*p));
    p->max_frame_rate = 10000;
    p->max_byte_rate = 10 * 1024 * 1024; /* 10 MB/s */
    p->max_channels = SMUX_MAX_CHANNELS;
    p->max_pending_frags = 64;
    p->max_retry_count = 10;
    p->idle_timeout_ms = 120000;
    p->migration_timeout_ms = 30000;
    p->max_batch_size = SMUX_BATCH_MAX;
    p->min_keepalive_ms = 5000;
    p->max_keepalive_ms = 300000;
    p->allow_compression = true;
    p->allow_encryption = true;
    p->allow_migration = true;
    p->enforce_ordering = true;
    p->auto_close_idle = false;
}

static void reset_rate_window(smux_rate_tracker_t *rt, uint64_t now) {
    rt->window_start = now;
    rt->frame_count = 0;
    rt->byte_count = 0;
}

int smux_policy_check_frame(const smux_policy_t *policy, smux_rate_tracker_t *rt,
                              uint64_t now_ms, uint32_t frame_bytes) {
    if (!policy || !rt) return SMUX_ERR_INVALID;

    /* Check if we're in a new 1-second window */
    if (now_ms - rt->window_start >= 1000) {
        reset_rate_window(rt, now_ms);
        rt->throttled = false;
    }

    /* Frame rate check */
    if (rt->frame_count >= policy->max_frame_rate) {
        rt->throttled = true;
        rt->violation_count++;
        return SMUX_ERR_RATE_LIMITED;
    }

    /* Byte rate check */
    if (rt->byte_count + frame_bytes > policy->max_byte_rate) {
        rt->throttled = true;
        rt->violation_count++;
        return SMUX_ERR_RATE_LIMITED;
    }

    rt->frame_count++;
    rt->byte_count += frame_bytes;
    return SMUX_OK;
}

int smux_policy_check_channel_open(const smux_policy_t *policy,
                                    smux_rate_tracker_t *rt) {
    if (!policy || !rt) return SMUX_ERR_INVALID;
    if (rt->channels_opened >= policy->max_channels) {
        rt->violation_count++;
        return SMUX_ERR_TOO_MANY;
    }
    rt->channels_opened++;
    return SMUX_OK;
}

void smux_policy_on_channel_close(smux_rate_tracker_t *rt) {
    if (rt && rt->channels_opened > 0) rt->channels_opened--;
}

int smux_policy_check_keepalive(const smux_policy_t *policy, uint32_t proposed_ms) {
    if (!policy) return SMUX_ERR_INVALID;
    if (proposed_ms < policy->min_keepalive_ms) return SMUX_ERR_INVALID;
    if (proposed_ms > policy->max_keepalive_ms) return SMUX_ERR_INVALID;
    return SMUX_OK;
}

int smux_policy_check_migration(const smux_policy_t *policy) {
    if (!policy) return SMUX_ERR_INVALID;
    return policy->allow_migration ? SMUX_OK : SMUX_ERR_INVALID;
}

int smux_policy_check_idle(const smux_policy_t *policy, uint64_t last_activity,
                            uint64_t now_ms) {
    if (!policy) return 0;
    if (!policy->auto_close_idle) return 0;
    return (now_ms - last_activity) > policy->idle_timeout_ms;
}

int smux_policy_report(const smux_policy_t *policy, const smux_rate_tracker_t *rt,
                        char *buf, size_t len) {
    if (!policy || !rt || !buf || len == 0) return 0;
    return snprintf(buf, len,
        "Policy: frame_rate=%u/%u byte_rate=%u/%u channels=%u/%u\n"
        "  Throttled: %s  Violations: %u\n"
        "  Compression: %s  Encryption: %s  Migration: %s\n",
        rt->frame_count, policy->max_frame_rate,
        rt->byte_count, policy->max_byte_rate,
        rt->channels_opened, policy->max_channels,
        rt->throttled ? "yes" : "no", rt->violation_count,
        policy->allow_compression ? "on" : "off",
        policy->allow_encryption ? "on" : "off",
        policy->allow_migration ? "on" : "off"
    );
}
