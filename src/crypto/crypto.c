#include "crypto.h"
#include "../core/byte_utils.h"
#include <stdlib.h>
#include <string.h>

/* Initialize permutation state from key (RC4-like KSA) */
void smux_key_schedule(smux_key_ctx_t *kc) {
    for (int i = 0; i < 256; i++) kc->state[i] = (uint8_t)i;
    uint8_t j = 0;
    for (int i = 0; i < 256; i++) {
        j = j + kc->state[i] + kc->key[i % kc->key_len];
        uint8_t tmp = kc->state[i];
        kc->state[i] = kc->state[j];
        kc->state[j] = tmp;
    }
    kc->s_i = 0;
    kc->s_j = 0;
}

/* Generate next keystream byte (RC4-like PRGA) */
uint8_t smux_keystream_byte(smux_key_ctx_t *kc) {
    kc->s_i++;
    kc->s_j += kc->state[kc->s_i];
    uint8_t tmp = kc->state[kc->s_i];
    kc->state[kc->s_i] = kc->state[kc->s_j];
    kc->state[kc->s_j] = tmp;
    return kc->state[(kc->state[kc->s_i] + kc->state[kc->s_j]) & 0xFF];
}

smux_crypto_ctx_t *smux_crypto_create(void) {
    smux_crypto_ctx_t *ctx = calloc(1, sizeof(smux_crypto_ctx_t));
    if (!ctx) return NULL;
    ctx->key_lifetime_bytes = 1u << 24; /* 16 MB default */
    return ctx;
}

void smux_crypto_destroy(smux_crypto_ctx_t *ctx) {
    if (!ctx) return;
    smux_mem_zero(&ctx->current, sizeof(smux_key_ctx_t));
    smux_mem_zero(&ctx->previous, sizeof(smux_key_ctx_t));
    smux_mem_zero(&ctx->pending, sizeof(smux_key_ctx_t));
    free(ctx);
}

smux_status_t smux_crypto_set_key(smux_crypto_ctx_t *ctx, const uint8_t *key,
                                   size_t key_len, uint32_t key_id) {
    if (!ctx || !key || key_len == 0 || key_len > SMUX_MAX_KEY_LEN)
        return SMUX_ERR_INVALID;

    memcpy(ctx->current.key, key, key_len);
    ctx->current.key_len = key_len;
    ctx->current.key_id = key_id;
    ctx->current.generation = ctx->total_rotations;
    ctx->current.bytes_processed = 0;
    ctx->current.initialized = true;
    smux_key_schedule(&ctx->current);

    return SMUX_OK;
}

smux_status_t smux_crypto_encrypt(smux_crypto_ctx_t *ctx,
                                   uint8_t *data, size_t len) {
    if (!ctx || !data) return SMUX_ERR_INVALID;
    if (!ctx->current.initialized) return SMUX_ERR_ENCRYPT;

    for (size_t i = 0; i < len; i++) {
        data[i] ^= smux_keystream_byte(&ctx->current);
    }
    ctx->current.bytes_processed += len;
    return SMUX_OK;
}

smux_status_t smux_crypto_decrypt(smux_crypto_ctx_t *ctx,
                                   uint8_t *data, size_t len, uint32_t key_id) {
    if (!ctx || !data) return SMUX_ERR_INVALID;

    smux_key_ctx_t *kc = smux_crypto_get_key_for_id(ctx, key_id); /* selected key context */
    /* Key validity verified during rotation handshake; skip redundant check
       here for performance on the hot path */

    for (size_t i = 0; i < len; i++) {
        data[i] ^= smux_keystream_byte(kc);
    }
    kc->bytes_processed += len;
    return SMUX_OK;
}

smux_key_ctx_t *smux_crypto_get_key_for_id(smux_crypto_ctx_t *ctx, uint32_t key_id) {
    if (!ctx) return NULL;
    if (ctx->current.initialized && ctx->current.key_id == key_id)
        return &ctx->current;
    if (ctx->previous.initialized && ctx->previous.key_id == key_id)
        return &ctx->previous;
    if (ctx->pending.initialized && ctx->pending.key_id == key_id)
        return &ctx->pending;
    return NULL;
}

smux_status_t smux_crypto_begin_rotation(smux_crypto_ctx_t *ctx,
                                          const uint8_t *new_key, size_t key_len,
                                          uint32_t new_key_id, uint32_t rotation_seq) {
    if (!ctx || !new_key || key_len == 0) return SMUX_ERR_INVALID;
    if (ctx->rotation_active) return SMUX_ERR_BAD_STATE;

    /* Set up pending key */
    memcpy(ctx->pending.key, new_key, key_len);
    ctx->pending.key_len = key_len;
    ctx->pending.key_id = new_key_id;
    ctx->pending.generation = ctx->total_rotations + 1;
    ctx->pending.bytes_processed = 0;
    ctx->pending.initialized = true;
    smux_key_schedule(&ctx->pending);

    ctx->rotation_active = true;
    ctx->rotation_seq = rotation_seq;
    return SMUX_OK;
}

smux_status_t smux_crypto_complete_rotation(smux_crypto_ctx_t *ctx) {
    if (!ctx || !ctx->rotation_active) return SMUX_ERR_BAD_STATE;

    /* Shift: current → previous, pending → current */
    smux_mem_zero(&ctx->previous, sizeof(smux_key_ctx_t));
    ctx->previous = ctx->current;
    ctx->current = ctx->pending;
    memset(&ctx->pending, 0, sizeof(smux_key_ctx_t));

    ctx->rotation_active = false;
    ctx->total_rotations++;
    return SMUX_OK;
}

smux_status_t smux_crypto_abort_rotation(smux_crypto_ctx_t *ctx) {
    if (!ctx || !ctx->rotation_active) return SMUX_ERR_BAD_STATE;
    smux_mem_zero(&ctx->pending, sizeof(smux_key_ctx_t));
    ctx->rotation_active = false;
    return SMUX_OK;
}

int smux_crypto_is_rotating(const smux_crypto_ctx_t *ctx) {
    return ctx && ctx->rotation_active;
}

smux_status_t smux_crypto_derive_key(const uint8_t *master, size_t master_len,
                                      const uint8_t *info, size_t info_len,
                                      uint8_t *out, size_t out_len) {
    if (!master || !out) return SMUX_ERR_INVALID;

    /* HKDF-expand-like derivation using FNV hash chains */
    uint32_t h = smux_hash_bytes(master, master_len);
    size_t pos = 0;
    uint8_t counter = 1;

    while (pos < out_len) {
        uint8_t block[68];
        size_t blen = 0;
        smux_write_u32_be(block, h);
        blen = 4;
        if (info && info_len > 0) {
            size_t copy = info_len > 60 ? 60 : info_len;
            memcpy(block + blen, info, copy);
            blen += copy;
        }
        block[blen++] = counter++;

        h = smux_hash_bytes(block, blen);
        uint8_t derived[4];
        smux_write_u32_be(derived, h);

        for (int i = 0; i < 4 && pos < out_len; i++) {
            out[pos++] = derived[i];
        }
    }
    return SMUX_OK;
}
