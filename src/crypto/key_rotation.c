#include "crypto.h"
#include "../frame/frame.h"
#include "../core/byte_utils.h"
#include <string.h>

/* Key rotation protocol integration with frame pipeline.
   
   Rotation sequence:
   1. Session sends KEY_ROTATE frame with new key material
   2. Both sides begin rotation: new frames use new key,
      old key remains valid for in-flight decryption
   3. After RECEIPT confirms all old-key frames are acked,
      rotation completes and old key is zeroed
*/

smux_status_t smux_crypto_encrypt_frame(smux_crypto_ctx_t *ctx, smux_frame_t *frame) {
    if (!ctx || !frame) return SMUX_ERR_INVALID;
    if (!ctx->current.initialized) return SMUX_OK;
    if (frame->payload_len == 0) return SMUX_OK;

    /* Add key ID extension so receiver knows which key to use */
    uint8_t kid_buf[4];
    smux_write_u32_be(kid_buf, ctx->current.key_id);
    smux_frame_add_extension(frame, SMUX_EXT_KEY_ID, kid_buf, 4);

    /* Encrypt payload in place */
    smux_status_t rc = smux_crypto_encrypt(ctx, frame->payload, frame->payload_len);
    if (rc != SMUX_OK) return rc;

    frame->flags |= SMUX_FLAG_ENCRYPTED;
    return SMUX_OK;
}

smux_status_t smux_crypto_decrypt_frame(smux_crypto_ctx_t *ctx, smux_frame_t *frame) {
    if (!ctx || !frame) return SMUX_ERR_INVALID;
    if (!(frame->flags & SMUX_FLAG_ENCRYPTED)) return SMUX_OK;

    /* Extract key ID from extension */
    uint32_t key_id = ctx->current.key_id; /* default to current */
    const smux_extension_t *ext = smux_frame_find_extension(frame, SMUX_EXT_KEY_ID);
    if (ext && ext->ext_len >= 4) {
        key_id = smux_read_u32_be(ext->ext_data);
    }

    smux_status_t rc = smux_crypto_decrypt(ctx, frame->payload, frame->payload_len, key_id);
    if (rc != SMUX_OK) return rc;

    frame->flags &= ~SMUX_FLAG_ENCRYPTED;
    return SMUX_OK;
}

/* Build KEY_ROTATE frame payload */
smux_status_t smux_build_key_rotate_frame(smux_crypto_ctx_t *ctx,
                                           const uint8_t *new_key, size_t key_len,
                                           uint32_t new_key_id,
                                           uint8_t *frame_buf, size_t buf_len,
                                           size_t *out_len) {
    if (!ctx || !new_key || !frame_buf || !out_len) return SMUX_ERR_INVALID;
    if (key_len > SMUX_MAX_KEY_LEN) return SMUX_ERR_INVALID;

    smux_frame_t frame;
    smux_frame_init(&frame, SMUX_FRAME_KEY_ROTATE, 0);

    /* Payload: new_key_id(4) + key_len(2) + encrypted_key(key_len) */
    uint8_t payload[SMUX_MAX_KEY_LEN + 8];
    smux_write_u32_be(payload, new_key_id);
    smux_write_u16_be(payload + 4, (uint16_t)key_len);

    /* Encrypt the new key with the current key for transit */
    memcpy(payload + 6, new_key, key_len);
    if (ctx->current.initialized) {
        /* Create a fresh keystream for key transport */
        smux_key_ctx_t transport_key;
        memcpy(&transport_key, &ctx->current, sizeof(smux_key_ctx_t));
        smux_key_schedule(&transport_key);
        for (size_t i = 0; i < key_len; i++) {
            payload[6 + i] ^= smux_keystream_byte(&transport_key);
        }
        smux_mem_zero(&transport_key, sizeof(transport_key));
    }

    frame.payload = payload;
    frame.payload_len = (uint32_t)(6 + key_len);

    int written = smux_frame_serialize(&frame, frame_buf, buf_len);
    if (written < 0) return SMUX_ERR_OVERFLOW;
    *out_len = (size_t)written;
    return SMUX_OK;
}

/* Process received KEY_ROTATE frame */
smux_status_t smux_handle_key_rotate_frame(smux_crypto_ctx_t *ctx,
                                            const smux_frame_t *frame,
                                            uint32_t rotation_seq) {
    if (!ctx || !frame) return SMUX_ERR_INVALID;
    if (frame->payload_len < 6) return SMUX_ERR_BAD_LENGTH;

    uint32_t new_key_id = smux_read_u32_be(frame->payload);
    uint16_t key_len = smux_read_u16_be(frame->payload + 4);
    if (key_len > SMUX_MAX_KEY_LEN) return SMUX_ERR_BAD_LENGTH;
    if (frame->payload_len < 6 + key_len) return SMUX_ERR_TRUNCATED;

    /* Decrypt the key material */
    uint8_t new_key[SMUX_MAX_KEY_LEN];
    memcpy(new_key, frame->payload + 6, key_len);
    if (ctx->current.initialized) {
        smux_key_ctx_t transport_key;
        memcpy(&transport_key, &ctx->current, sizeof(smux_key_ctx_t));
        smux_key_schedule(&transport_key);
        for (size_t i = 0; i < key_len; i++) {
            new_key[i] ^= smux_keystream_byte(&transport_key);
        }
        smux_mem_zero(&transport_key, sizeof(transport_key));
    }

    smux_status_t rc = smux_crypto_begin_rotation(ctx, new_key, key_len,
                                                    new_key_id, rotation_seq);
    smux_mem_zero(new_key, sizeof(new_key));
    return rc;
}
