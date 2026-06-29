#include "flow.h"
#include "../core/byte_utils.h"

void smux_flow_init(smux_flow_ctrl_t *fc, uint32_t window) {
    if (!fc) return;
    fc->window_size = window;
    fc->window_used = 0;
    fc->credit_available = window;
    fc->high_water = window * 3 / 4;
    fc->low_water = window / 4;
    fc->backpressure_active = false;
    fc->total_granted = 0;
    fc->total_consumed = 0;
    fc->stall_count = 0;
}

smux_status_t smux_flow_consume(smux_flow_ctrl_t *fc, uint32_t amount) {
    if (!fc) return SMUX_ERR_INVALID;
    if (fc->credit_available < amount) {
        fc->stall_count++;
        return SMUX_ERR_WINDOW_FULL;
    }
    fc->window_used += amount;
    fc->credit_available -= amount;
    fc->total_consumed += amount;

    if (fc->window_used >= fc->high_water && !fc->backpressure_active) {
        fc->backpressure_active = true;
    }
    return SMUX_OK;
}

smux_status_t smux_flow_release(smux_flow_ctrl_t *fc, uint32_t amount) {
    if (!fc) return SMUX_ERR_INVALID;
    if (amount > fc->window_used) amount = fc->window_used;
    fc->window_used -= amount;
    fc->credit_available += amount;

    if (fc->window_used <= fc->low_water && fc->backpressure_active) {
        fc->backpressure_active = false;
    }
    return SMUX_OK;
}

smux_status_t smux_flow_update_window(smux_flow_ctrl_t *fc, uint32_t new_window) {
    if (!fc) return SMUX_ERR_INVALID;
    if (new_window > SMUX_MAX_WINDOW) return SMUX_ERR_INVALID;

    uint32_t old_window = fc->window_size;
    fc->window_size = new_window;
    fc->high_water = new_window * 3 / 4;
    fc->low_water = new_window / 4;

    /* Adjust credit: if window grew, add credit; if shrunk, reduce */
    if (new_window > old_window) {
        fc->credit_available += (new_window - old_window);
    } else if (new_window < old_window) {
        uint32_t reduction = old_window - new_window;
        if (fc->credit_available >= reduction) {
            fc->credit_available -= reduction;
        } else {
            fc->credit_available = 0;
        }
    }

    return SMUX_OK;
}

uint32_t smux_flow_available(const smux_flow_ctrl_t *fc) {
    if (!fc) return 0;
    return fc->credit_available;
}

int smux_flow_is_blocked(const smux_flow_ctrl_t *fc) {
    return fc && fc->credit_available == 0;
}

smux_status_t smux_flow_grant_credit(smux_flow_ctrl_t *fc, uint32_t amount) {
    if (!fc) return SMUX_ERR_INVALID;
    uint32_t total;
    if (smux_safe_add_u32(fc->credit_available, amount, &total) != 0)
        return SMUX_ERR_OVERFLOW;
    if (total > fc->window_size) total = fc->window_size;
    fc->credit_available = total;
    fc->total_granted += amount;
    return SMUX_OK;
}

int smux_flow_should_advertise(const smux_flow_ctrl_t *fc) {
    if (!fc) return 0;
    /* Advertise new credit when usage drops below half the window */
    return fc->window_used < fc->window_size / 2;
}

uint32_t smux_flow_compute_credit(const smux_flow_ctrl_t *fc) {
    if (!fc) return 0;
    /* Offer up to the unused portion of the window */
    uint32_t unused = fc->window_size - fc->window_used;
    return unused;
}
