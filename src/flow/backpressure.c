#include "flow.h"

void smux_backpressure_init(smux_backpressure_t *bp, uint16_t channel_id) {
    if (!bp) return;
    bp->channel_id = channel_id;
    bp->pressure_level = 0;
    bp->paused = false;
    bp->paused_at = 0;
    bp->resumed_at = 0;
    bp->pause_count = 0;
}

int smux_backpressure_check(smux_backpressure_t *bp, const smux_flow_ctrl_t *fc) {
    if (!bp || !fc) return 0;

    /* Compute pressure as ratio of used to total window (0-100) */
    if (fc->window_size == 0) {
        bp->pressure_level = 100;
        return 1;
    }

    bp->pressure_level = (fc->window_used * 100) / fc->window_size;

    /* Auto-pause at high pressure */
    if (bp->pressure_level >= 90 && !bp->paused) {
        return 1; /* caller should pause */
    }

    /* Auto-resume at low pressure */
    if (bp->pressure_level <= 25 && bp->paused) {
        return -1; /* caller should resume */
    }

    return 0;
}

void smux_backpressure_pause(smux_backpressure_t *bp, uint64_t now_ms) {
    if (!bp) return;
    bp->paused = true;
    bp->paused_at = now_ms;
    bp->pause_count++;
}

void smux_backpressure_resume(smux_backpressure_t *bp, uint64_t now_ms) {
    if (!bp) return;
    bp->paused = false;
    bp->resumed_at = now_ms;
}

