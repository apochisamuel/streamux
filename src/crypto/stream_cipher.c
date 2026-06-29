#include "crypto.h"
#include "../core/byte_utils.h"
#include <string.h>

/* Extended cipher operations: nonce handling, MAC computation,
   and per-frame IV generation for the stream cipher context. */

typedef struct {
    uint8_t     nonce[12];
    uint32_t    counter;
    uint32_t    frame_counter;
} smux_cipher_state_t;

/* Internal keystream function (defined first for use below) */
static uint8_t keystream_byte_internal(smux_key_ctx_t *kc) {
    kc->s_i++;
    kc->s_j += kc->state[kc->s_i];
    uint8_t tmp = kc->state[kc->s_i];
    kc->state[kc->s_i] = kc->state[kc->s_j];
    kc->state[kc->s_j] = tmp;
    return kc->state[(kc->state[kc->s_i] + kc->state[kc->s_j]) & 0xFF];
}

/* Generate per-frame initialization vector from nonce + counter */
static void derive_frame_iv(const uint8_t *nonce, uint32_t frame_num,
                             uint8_t *iv_out) {
    memcpy(iv_out, nonce, 12);
    /* XOR frame number into the last 4 bytes */
    iv_out[8] ^= (uint8_t)(frame_num >> 24);
    iv_out[9] ^= (uint8_t)(frame_num >> 16);
    iv_out[10] ^= (uint8_t)(frame_num >> 8);
    iv_out[11] ^= (uint8_t)(frame_num);
}

/* Simple keyed MAC using cascaded FNV hash */
uint32_t smux_compute_mac(const uint8_t *key, size_t key_len,
                           const uint8_t *data, size_t data_len) {
    /* Two-pass FNV with key mixing */
    uint32_t h1 = smux_hash_bytes(key, key_len);
    uint32_t h2 = smux_hash_bytes(data, data_len);

    /* Mix: hash(key || counter || data_hash) */
    uint8_t mix[12];
    smux_write_u32_be(mix, h1);
    smux_write_u32_be(mix + 4, 0x4D414321); /* "MAC!" */
    smux_write_u32_be(mix + 8, h2);
    return smux_hash_bytes(mix, 12);
}

/* Verify MAC on received data */
int smux_verify_mac(const uint8_t *key, size_t key_len,
                    const uint8_t *data, size_t data_len,
                    uint32_t expected_mac) {
    uint32_t computed = smux_compute_mac(key, key_len, data, data_len);
    /* Constant-time comparison */
    uint32_t diff = computed ^ expected_mac;
    return diff == 0;
}

/* Encrypt data with per-frame IV derivation */
smux_status_t smux_stream_encrypt(smux_key_ctx_t *kc, const uint8_t *nonce,
                                   uint32_t frame_num,
                                   uint8_t *data, size_t len) {
    if (!kc || !nonce || !data) return SMUX_ERR_INVALID;
    if (!kc->initialized) return SMUX_ERR_ENCRYPT;

    uint8_t iv[12];
    derive_frame_iv(nonce, frame_num, iv);

    /* Re-initialize cipher state with IV-modified key */
    smux_key_ctx_t frame_kc;
    memcpy(&frame_kc, kc, sizeof(smux_key_ctx_t));
    /* Mix IV into key before schedule */
    for (size_t i = 0; i < 12 && i < frame_kc.key_len; i++) {
        frame_kc.key[i] ^= iv[i];
    }
    /* Reinit permutation with modified key */
    for (int i = 0; i < 256; i++) frame_kc.state[i] = (uint8_t)i;
    uint8_t j = 0;
    for (int i = 0; i < 256; i++) {
        j = j + frame_kc.state[i] + frame_kc.key[i % frame_kc.key_len];
        uint8_t tmp = frame_kc.state[i];
        frame_kc.state[i] = frame_kc.state[j];
        frame_kc.state[j] = tmp;
    }
    frame_kc.s_i = 0;
    frame_kc.s_j = 0;

    /* Skip first 256 bytes of keystream (drop phase) */
    for (int i = 0; i < 256; i++) {
        keystream_byte_internal(&frame_kc);
    }

    /* XOR keystream with data */
    for (size_t i = 0; i < len; i++) {
        data[i] ^= keystream_byte_internal(&frame_kc);
    }

    smux_mem_zero(&frame_kc, sizeof(frame_kc));
    return SMUX_OK;
}
