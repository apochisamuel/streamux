#include "diagnostics.h"
#include "../core/types.h"
#include <string.h>

#include <stdio.h>
/* Connection health assessment: computes composite health scores
   from multiple signal sources. Used for load balancing and
   proactive error detection. */

typedef enum {
    SMUX_HEALTH_EXCELLENT = 0,
    SMUX_HEALTH_GOOD      = 1,
    SMUX_HEALTH_DEGRADED  = 2,
    SMUX_HEALTH_POOR      = 3,
    SMUX_HEALTH_CRITICAL  = 4
} smux_health_grade_t;

typedef struct {
    float               error_rate;         /* errors / total frames */
    float               retransmit_rate;    /* retx / total sent */
    float               compression_ratio;
    float               window_utilization;
    float               channel_utilization;
    uint32_t            active_channels;
    uint32_t            pending_retransmits;
    uint64_t            uptime_ms;
    smux_health_grade_t grade;
    float               composite_score;    /* 0.0 (dead) - 1.0 (perfect) */
} smux_health_report_t;

static float clamp_01(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

smux_status_t smux_health_assess(const smux_diagnostics_t *d,
                                  smux_health_report_t *report) {
    if (!d || !report) return SMUX_ERR_INVALID;
    memset(report, 0, sizeof(*report));

    const smux_stats_t *s = &d->stats;

    /* Error rate */
    uint64_t total_frames = s->frames_received + s->frames_sent;
    uint64_t total_errors = s->crc_errors + s->parse_errors + s->state_errors;
    if (total_frames > 0) {
        report->error_rate = (float)total_errors / (float)total_frames;
    }

    /* Retransmit rate */
    if (s->frames_sent > 0) {
        report->retransmit_rate = (float)s->retransmissions / (float)s->frames_sent;
    }

    /* Channel utilization */
    if (s->channels_opened > 0) {
        uint64_t active = s->channels_opened - s->channels_closed - s->channels_errored;
        report->active_channels = (uint32_t)active;
        report->channel_utilization = (float)active / (float)SMUX_MAX_CHANNELS;
    }

    /* Window utilization: estimate from bytes vs window updates */
    if (s->window_updates > 0 && s->bytes_sent > 0) {
        report->window_utilization = clamp_01(
            (float)s->bytes_sent / ((float)s->window_updates * SMUX_DEFAULT_WINDOW)
        );
    }

    /* Compression ratio */
    if (s->compressions > 0 && s->bytes_sent > 0) {
        report->compression_ratio = clamp_01(
            1.0f - (float)s->compressions * 0.01f
        );
    }

    /* Composite score: weighted average */
    float error_score = clamp_01(1.0f - report->error_rate * 10.0f);
    float retx_score = clamp_01(1.0f - report->retransmit_rate * 5.0f);
    float util_score = clamp_01(report->window_utilization);

    report->composite_score = error_score * 0.40f +
                              retx_score * 0.35f +
                              util_score * 0.25f;

    /* Grade assignment */
    if (report->composite_score >= 0.90f) {
        report->grade = SMUX_HEALTH_EXCELLENT;
    } else if (report->composite_score >= 0.70f) {
        report->grade = SMUX_HEALTH_GOOD;
    } else if (report->composite_score >= 0.50f) {
        report->grade = SMUX_HEALTH_DEGRADED;
    } else if (report->composite_score >= 0.25f) {
        report->grade = SMUX_HEALTH_POOR;
    } else {
        report->grade = SMUX_HEALTH_CRITICAL;
    }

    return SMUX_OK;
}

int smux_health_report_str(const smux_health_report_t *r, char *buf, size_t len) {
    if (!r || !buf || len == 0) return 0;

    static const char *grade_names[] = {
        "EXCELLENT", "GOOD", "DEGRADED", "POOR", "CRITICAL"
    };
    const char *grade = r->grade <= SMUX_HEALTH_CRITICAL ?
                        grade_names[r->grade] : "UNKNOWN";

    return snprintf(buf, len,
        "Health: %s (score: %.2f)\n"
        "  Error rate: %.4f\n"
        "  Retransmit rate: %.4f\n"
        "  Window utilization: %.2f\n"
        "  Channel utilization: %.2f (%u active)\n"
        "  Compression ratio: %.2f\n",
        grade, r->composite_score,
        r->error_rate, r->retransmit_rate,
        r->window_utilization,
        r->channel_utilization, r->active_channels,
        r->compression_ratio
    );
}

/* Quick health check: returns true if connection appears healthy */
int smux_health_is_ok(const smux_diagnostics_t *d) {
    smux_health_report_t report;
    if (smux_health_assess(d, &report) != SMUX_OK) return 0;
    return report.grade <= SMUX_HEALTH_GOOD;
}

/* Trend analysis: compare two snapshots to detect degradation */
int smux_health_trend(const smux_diagnostics_t *before,
                       const smux_diagnostics_t *after,
                       float *trend_score) {
    if (!before || !after || !trend_score) return SMUX_ERR_INVALID;

    smux_health_report_t r_before, r_after;
    smux_health_assess(before, &r_before);
    smux_health_assess(after, &r_after);

    *trend_score = r_after.composite_score - r_before.composite_score;
    /* Positive = improving, negative = degrading */
    return SMUX_OK;
}
