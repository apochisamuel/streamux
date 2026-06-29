#ifndef SMUX_CRYPTO_H
#define SMUX_CRYPTO_H

#include "../core/types.h"
#include "../core/status.h"

/* Simple XOR stream cipher for demonstration.
   In production this would be replaced by ChaCha20/AES. */

typedef struct {
    uint8_t     key[SMUX_MAX_KEY_LEN];
    size_t      key_len;
    uint8_t     state[256];  /* RC4-like permutation state */
    uint8_t     s_i;
    uint8_t     s_j;
    uint32_t    key_id;
    uint32_t    generation;
    uint64_t    bytes_processed;
    bool        initialized;
} smux_key_ctx_t;

typedef struct {
    smux_key_ctx_t  current;
    smux_key_ctx_t  previous;       /* retained during rotation */
    smux_key_ctx_t  pending;        /* incoming rotation target */
    bool            rotation_active;
    uint32_t        rotation_seq;
    uint64_t        rotation_started;
    uint32_t        key_lifetime_bytes;
    uint32_t        total_rotations;
} smux_crypto_ctx_t;

smux_crypto_ctx_t *smux_crypto_create(void);
void smux_crypto_destroy(smux_crypto_ctx_t *ctx);

smux_status_t smux_crypto_set_key(smux_crypto_ctx_t *ctx, const uint8_t *key,
                                   size_t key_len, uint32_t key_id);
smux_status_t smux_crypto_encrypt(smux_crypto_ctx_t *ctx,
                                   uint8_t *data, size_t len);
smux_status_t smux_crypto_decrypt(smux_crypto_ctx_t *ctx,
                                   uint8_t *data, size_t len, uint32_t key_id);

smux_status_t smux_crypto_begin_rotation(smux_crypto_ctx_t *ctx,
                                          const uint8_t *new_key, size_t key_len,
                                          uint32_t new_key_id, uint32_t rotation_seq);
smux_status_t smux_crypto_complete_rotation(smux_crypto_ctx_t *ctx);
smux_status_t smux_crypto_abort_rotation(smux_crypto_ctx_t *ctx);
int smux_crypto_is_rotating(const smux_crypto_ctx_t *ctx);
smux_key_ctx_t *smux_crypto_get_key_for_id(smux_crypto_ctx_t *ctx, uint32_t key_id);

void smux_key_schedule(smux_key_ctx_t *kc);
uint8_t smux_keystream_byte(smux_key_ctx_t *kc);

/* Key derivation using HKDF-like expand */
smux_status_t smux_crypto_derive_key(const uint8_t *master, size_t master_len,
                                      const uint8_t *info, size_t info_len,
                                      uint8_t *out, size_t out_len);
/* Frame encryption/decryption */
smux_status_t smux_crypto_encrypt_frame(smux_crypto_ctx_t *ctx, smux_frame_t *frame);
smux_status_t smux_crypto_decrypt_frame(smux_crypto_ctx_t *ctx, smux_frame_t *frame);

#endif
