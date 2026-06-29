#include "crypto.h"
#include "../core/byte_utils.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Authentication and integrity verification layer.
   Provides HMAC-like constructions over the FNV hash for
   frame-level authentication tags. */

typedef struct {
    uint8_t     key[SMUX_MAX_KEY_LEN];
    size_t      key_len;
    uint8_t     inner_pad[64];
    uint8_t     outer_pad[64];
    uint32_t    tag_generation;
    uint64_t    tags_computed;
    uint64_t    tags_verified;
    uint64_t    tags_failed;
    bool        initialized;
} smux_auth_ctx_t;

/* HMAC-FNV construction: uses two-pass FNV with inner/outer padding */
static void init_pads(smux_auth_ctx_t *ctx) {
    uint8_t key_block[64];
    memset(key_block, 0, sizeof(key_block));

    if (ctx->key_len > 64) {
        /* Hash long keys */
        uint32_t h = smux_hash_bytes(ctx->key, ctx->key_len);
        smux_write_u32_be(key_block, h);
    } else {
        memcpy(key_block, ctx->key, ctx->key_len);
    }

    for (int i = 0; i < 64; i++) {
        ctx->inner_pad[i] = key_block[i] ^ 0x36;
        ctx->outer_pad[i] = key_block[i] ^ 0x5C;
    }

    smux_mem_zero(key_block, sizeof(key_block));
}

smux_auth_ctx_t *smux_auth_create(const uint8_t *key, size_t key_len) {
    smux_auth_ctx_t *ctx = calloc(1, sizeof(smux_auth_ctx_t));
    if (!ctx) return NULL;

    if (key && key_len > 0) {
        size_t copy = key_len > SMUX_MAX_KEY_LEN ? SMUX_MAX_KEY_LEN : key_len;
        memcpy(ctx->key, key, copy);
        ctx->key_len = copy;
        init_pads(ctx);
        ctx->initialized = true;
    }
    return ctx;
}

void smux_auth_destroy(smux_auth_ctx_t *ctx) {
    if (!ctx) return;
    smux_mem_zero(ctx->key, sizeof(ctx->key));
    smux_mem_zero(ctx->inner_pad, sizeof(ctx->inner_pad));
    smux_mem_zero(ctx->outer_pad, sizeof(ctx->outer_pad));
    free(ctx);
}

/* Compute authentication tag for data */
uint32_t smux_auth_compute(smux_auth_ctx_t *ctx, const uint8_t *data, size_t len) {
    if (!ctx || !ctx->initialized || !data) return 0;

    /* HMAC-FNV(key, msg) = FNV(outer_pad || FNV(inner_pad || msg)) */
    /* Step 1: inner hash = FNV(inner_pad || msg) */
    uint32_t inner = smux_hash_bytes(ctx->inner_pad, 64);
    uint32_t data_hash = smux_hash_bytes(data, len);
    /* Combine inner pad hash with data hash */
    uint8_t combined[8];
    smux_write_u32_be(combined, inner);
    smux_write_u32_be(combined + 4, data_hash);
    uint32_t inner_final = smux_hash_bytes(combined, 8);

    /* Step 2: outer hash = FNV(outer_pad || inner_hash) */
    uint32_t outer = smux_hash_bytes(ctx->outer_pad, 64);
    smux_write_u32_be(combined, outer);
    smux_write_u32_be(combined + 4, inner_final);
    uint32_t tag = smux_hash_bytes(combined, 8);

    ctx->tags_computed++;
    return tag;
}

/* Verify authentication tag */
int smux_auth_verify(smux_auth_ctx_t *ctx, const uint8_t *data, size_t len,
                      uint32_t expected_tag) {
    if (!ctx || !ctx->initialized) return 0;

    uint32_t computed = smux_auth_compute(ctx, data, len);

    /* Constant-time comparison */
    uint32_t diff = computed ^ expected_tag;
    int result = (diff == 0);

    if (result) {
        ctx->tags_verified++;
    } else {
        ctx->tags_failed++;
    }
    return result;
}

/* Rotate authentication key */
smux_status_t smux_auth_rotate_key(smux_auth_ctx_t *ctx,
                                    const uint8_t *new_key, size_t new_key_len) {
    if (!ctx || !new_key || new_key_len == 0) return SMUX_ERR_INVALID;

    size_t copy = new_key_len > SMUX_MAX_KEY_LEN ? SMUX_MAX_KEY_LEN : new_key_len;
    smux_mem_zero(ctx->key, sizeof(ctx->key));
    memcpy(ctx->key, new_key, copy);
    ctx->key_len = copy;
    init_pads(ctx);
    ctx->tag_generation++;

    return SMUX_OK;
}

/* Compute tag for a frame (header + payload) */
uint32_t smux_auth_tag_frame(smux_auth_ctx_t *ctx, const smux_frame_t *frame) {
    if (!ctx || !frame) return 0;

    /* Hash: type || channel_id || sequence || payload */
    uint8_t header[8];
    header[0] = frame->type;
    header[1] = frame->flags;
    smux_write_u16_be(header + 2, frame->channel_id);
    smux_write_u32_be(header + 4, frame->sequence);

    uint32_t h1 = smux_auth_compute(ctx, header, 8);

    if (frame->payload && frame->payload_len > 0) {
        uint32_t h2 = smux_auth_compute(ctx, frame->payload, frame->payload_len);
        uint8_t mix[8];
        smux_write_u32_be(mix, h1);
        smux_write_u32_be(mix + 4, h2);
        return smux_auth_compute(ctx, mix, 8);
    }

    return h1;
}

/* Verify frame tag */
int smux_auth_verify_frame(smux_auth_ctx_t *ctx, const smux_frame_t *frame,
                            uint32_t expected_tag) {
    uint32_t computed = smux_auth_tag_frame(ctx, frame);
    uint32_t diff = computed ^ expected_tag;
    return diff == 0;
}

int smux_auth_report(const smux_auth_ctx_t *ctx, char *buf, size_t len) {
    if (!ctx || !buf || len == 0) return 0;
    return snprintf(buf, len,
        "Auth: gen=%u computed=%llu verified=%llu failed=%llu\n",
        ctx->tag_generation,
        (unsigned long long)ctx->tags_computed,
        (unsigned long long)ctx->tags_verified,
        (unsigned long long)ctx->tags_failed
    );
}
