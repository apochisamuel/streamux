#include "session.h"
#include "../frame/frame.h"
#include "../core/byte_utils.h"
#include <stdlib.h>
#include <string.h>

/* Handshake state machine with retry and backoff logic */

typedef enum {
    HS_INIT         = 0,
    HS_SENT_HELLO   = 1,
    HS_GOT_HELLO    = 2,
    HS_SENT_ACK     = 3,
    HS_GOT_ACK      = 4,
    HS_COMPLETE     = 5,
    HS_FAILED       = 6
} hs_state_t;

typedef struct {
    hs_state_t          state;
    uint32_t            retries;
    uint64_t            started_at;
    uint64_t            last_attempt;
    uint32_t            backoff_ms;
    uint8_t             remote_session_id[16];
    uint32_t            remote_features;
    uint16_t            agreed_extensions;
} smux_handshake_ctx_t;

static uint32_t compute_backoff(uint32_t attempt) {
    uint32_t base = SMUX_RETRY_BASE_MS;
    for (uint32_t i = 0; i < attempt && i < 5; i++)
        base *= 2;
    if (base > SMUX_RETRY_MAX_MS)
        base = SMUX_RETRY_MAX_MS;
    return base;
}

smux_handshake_ctx_t *smux_handshake_create(void) {
    smux_handshake_ctx_t *ctx = (smux_handshake_ctx_t *)calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;
    ctx->state = HS_INIT;
    ctx->backoff_ms = SMUX_RETRY_BASE_MS;
    return ctx;
}

void smux_handshake_destroy(smux_handshake_ctx_t *ctx) {
    if (ctx) free(ctx);
}

int smux_handshake_should_retry(smux_handshake_ctx_t *ctx, uint64_t now_ms) {
    if (!ctx) return 0;
    if (ctx->state != HS_SENT_HELLO) return 0;
    if (ctx->retries >= SMUX_MAX_RETRY) {
        ctx->state = HS_FAILED;
        return 0;
    }
    uint64_t elapsed = now_ms - ctx->last_attempt;
    return elapsed >= ctx->backoff_ms;
}

smux_status_t smux_handshake_retry(smux_handshake_ctx_t *ctx,
                                    smux_session_t *session,
                                    uint8_t *out_buf, size_t buf_len,
                                    size_t *out_len, uint64_t now_ms) {
    if (!ctx || !session) return SMUX_ERR_INVALID;

    ctx->retries++;
    ctx->backoff_ms = compute_backoff(ctx->retries);
    ctx->last_attempt = now_ms;

    return smux_session_start_handshake(session, out_buf, buf_len, out_len);
}

smux_status_t smux_handshake_process_hello(smux_handshake_ctx_t *ctx,
                                            const smux_frame_t *frame) {
    if (!ctx || !frame) return SMUX_ERR_INVALID;

    if (frame->payload_len >= 24) {
        memcpy(ctx->remote_session_id, frame->payload + 8, 16);
    }

    /* Extract feature bitmap from extensions */
    ctx->remote_features = 0;
    if (frame->flags & SMUX_FLAG_COMPRESSED)
        ctx->remote_features |= 0x01;
    if (frame->flags & SMUX_FLAG_ENCRYPTED)
        ctx->remote_features |= 0x02;

    for (uint8_t i = 0; i < frame->ext_count; i++) {
        ctx->agreed_extensions |= (1u << (frame->extensions[i].ext_type & 0x0F));
    }

    ctx->state = HS_GOT_HELLO;
    return SMUX_OK;
}

smux_status_t smux_handshake_complete(smux_handshake_ctx_t *ctx) {
    if (!ctx) return SMUX_ERR_INVALID;
    ctx->state = HS_COMPLETE;
    return SMUX_OK;
}

int smux_handshake_is_complete(const smux_handshake_ctx_t *ctx) {
    return ctx && ctx->state == HS_COMPLETE;
}

int smux_handshake_is_failed(const smux_handshake_ctx_t *ctx) {
    return ctx && ctx->state == HS_FAILED;
}
