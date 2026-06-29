#include "flow.h"
#include "../core/byte_utils.h"
#include <string.h>
#include <stdlib.h>

/* Credit-based flow control accounting.
   
   Implements a token-bucket style credit system where:
   - Sender maintains available credits per channel
   - Receiver grants credits via WINDOW frames
   - Sending is blocked when credits reach zero
   - Backpressure cascades through connected channels
*/

typedef struct {
    uint32_t    tokens;
    uint32_t    max_tokens;
    uint32_t    refill_rate;     /* tokens per ms */
    uint64_t    last_refill;
    uint64_t    total_granted;
    uint64_t    total_consumed;
    uint64_t    total_denied;
    bool        paused;
} smux_credit_bucket_t;

void smux_credit_init(smux_credit_bucket_t *cb, uint32_t max_tokens, uint32_t refill_rate) {
    if (!cb) return;
    memset(cb, 0, sizeof(*cb));
    cb->max_tokens = max_tokens;
    cb->tokens = max_tokens;
    cb->refill_rate = refill_rate;
}

int smux_credit_try_consume(smux_credit_bucket_t *cb, uint32_t amount) {
    if (!cb || cb->paused) return 0;
    if (cb->tokens < amount) {
        cb->total_denied += amount;
        return 0;
    }
    cb->tokens -= amount;
    cb->total_consumed += amount;
    return 1;
}

void smux_credit_refill(smux_credit_bucket_t *cb, uint64_t now_ms) {
    if (!cb || cb->refill_rate == 0) return;

    uint64_t elapsed = now_ms - cb->last_refill;
    if (elapsed == 0) return;

    uint64_t new_tokens = elapsed * cb->refill_rate;
    cb->tokens += (uint32_t)new_tokens;
    if (cb->tokens > cb->max_tokens) cb->tokens = cb->max_tokens;
    cb->last_refill = now_ms;
    cb->total_granted += new_tokens;
}

void smux_credit_grant(smux_credit_bucket_t *cb, uint32_t amount) {
    if (!cb) return;
    cb->tokens += amount;
    if (cb->tokens > cb->max_tokens) cb->tokens = cb->max_tokens;
    cb->total_granted += amount;
}

void smux_credit_pause(smux_credit_bucket_t *cb) {
    if (cb) cb->paused = true;
}

void smux_credit_resume(smux_credit_bucket_t *cb) {
    if (cb) cb->paused = false;
}

uint32_t smux_credit_available(const smux_credit_bucket_t *cb) {
    if (!cb || cb->paused) return 0;
    return cb->tokens;
}

float smux_credit_utilization(const smux_credit_bucket_t *cb) {
    if (!cb || cb->max_tokens == 0) return 0.0f;
    return 1.0f - ((float)cb->tokens / (float)cb->max_tokens);
}

/* Multi-channel credit scheduler: fair allocation across channels */

typedef struct {
    smux_credit_bucket_t per_channel[SMUX_MAX_CHANNELS];
    uint32_t             global_budget;
    uint32_t             global_used;
    uint32_t             channel_count;
    uint16_t             active_channels[SMUX_MAX_CHANNELS];
    int                  active_count;
} smux_credit_scheduler_t;

smux_credit_scheduler_t *smux_credit_scheduler_create(uint32_t global_budget) {
    smux_credit_scheduler_t *cs = calloc(1, sizeof(smux_credit_scheduler_t));
    if (!cs) return NULL;
    cs->global_budget = global_budget;
    return cs;
}

void smux_credit_scheduler_destroy(smux_credit_scheduler_t *cs) {
    free(cs);
}

void smux_credit_scheduler_add_channel(smux_credit_scheduler_t *cs,
                                        uint16_t channel_id, uint32_t weight) {
    if (!cs || channel_id >= SMUX_MAX_CHANNELS) return;

    /* Fair share based on weight */
    uint32_t per_channel = cs->global_budget / (cs->active_count + 1);
    if (weight > 0) per_channel = per_channel * weight / 100;

    smux_credit_init(&cs->per_channel[channel_id], per_channel, 0);
    cs->active_channels[cs->active_count++] = channel_id;
    cs->channel_count++;

    /* Redistribute credits fairly */
    if (cs->active_count > 1) {
        uint32_t fair_share = cs->global_budget / (uint32_t)cs->active_count;
        for (int i = 0; i < cs->active_count; i++) {
            uint16_t ch = cs->active_channels[i];
            cs->per_channel[ch].max_tokens = fair_share;
            if (cs->per_channel[ch].tokens > fair_share)
                cs->per_channel[ch].tokens = fair_share;
        }
    }
}

void smux_credit_scheduler_remove_channel(smux_credit_scheduler_t *cs,
                                           uint16_t channel_id) {
    if (!cs) return;
    for (int i = 0; i < cs->active_count; i++) {
        if (cs->active_channels[i] == channel_id) {
            cs->active_channels[i] = cs->active_channels[cs->active_count - 1];
            cs->active_count--;
            cs->channel_count--;

            /* Reclaim credits */
            cs->global_used -= (cs->per_channel[channel_id].max_tokens -
                               cs->per_channel[channel_id].tokens);
            memset(&cs->per_channel[channel_id], 0, sizeof(smux_credit_bucket_t));
            break;
        }
    }
}

int smux_credit_scheduler_try_send(smux_credit_scheduler_t *cs,
                                    uint16_t channel_id, uint32_t bytes) {
    if (!cs || channel_id >= SMUX_MAX_CHANNELS) return 0;
    if (cs->global_used + bytes > cs->global_budget) return 0;

    if (smux_credit_try_consume(&cs->per_channel[channel_id], bytes)) {
        cs->global_used += bytes;
        return 1;
    }
    return 0;
}

void smux_credit_scheduler_ack(smux_credit_scheduler_t *cs,
                                uint16_t channel_id, uint32_t bytes) {
    if (!cs || channel_id >= SMUX_MAX_CHANNELS) return;
    smux_credit_grant(&cs->per_channel[channel_id], bytes);
    if (cs->global_used >= bytes) {
        cs->global_used -= bytes;
    } else {
        cs->global_used = 0;
    }
}
