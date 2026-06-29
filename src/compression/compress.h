#ifndef SMUX_COMPRESS_H
#define SMUX_COMPRESS_H

#include "../core/types.h"
#include "../core/status.h"
#include "../buffer/buffer.h"

/* LZ77-style compression with sliding window dictionary.
   Uses a hash chain for match finding and lazy evaluation. */

#define SMUX_LZ_MIN_MATCH    3
#define SMUX_LZ_MAX_MATCH    258
#define SMUX_LZ_HASH_BITS    15
#define SMUX_LZ_HASH_SIZE    (1 << SMUX_LZ_HASH_BITS)
#define SMUX_LZ_HASH_MASK    (SMUX_LZ_HASH_SIZE - 1)
#define SMUX_LZ_CHAIN_LEN    64

typedef struct {
    uint8_t    *window;
    uint32_t    window_size;
    uint32_t    window_pos;
    uint32_t    window_fill;
    int16_t     hash_head[SMUX_LZ_HASH_SIZE];
    int16_t     hash_chain[SMUX_DICT_SIZE];
    uint32_t    hash_val;
    bool        initialized;
} smux_dict_t;

typedef struct {
    smux_dict_t     dict;
    uint8_t        *output_buf;
    size_t          output_cap;
    size_t          output_len;
    uint32_t        bytes_in;
    uint32_t        bytes_out;
    bool            enabled;
    smux_slice_t   *pending_slice;
} smux_compress_ctx_t;

smux_compress_ctx_t *smux_compress_create(uint32_t dict_size);
void smux_compress_destroy(smux_compress_ctx_t *ctx);
void smux_compress_reset(smux_compress_ctx_t *ctx);

smux_status_t smux_compress_data(smux_compress_ctx_t *ctx,
                                  const uint8_t *input, size_t in_len,
                                  uint8_t *output, size_t out_cap,
                                  size_t *out_len);
smux_status_t smux_decompress_data(smux_compress_ctx_t *ctx,
                                    const uint8_t *input, size_t in_len,
                                    uint8_t *output, size_t out_cap,
                                    size_t *out_len);

void smux_dict_init(smux_dict_t *dict, uint32_t size);
void smux_dict_reset(smux_dict_t *dict);
void smux_dict_update(smux_dict_t *dict, const uint8_t *data, size_t len);
int smux_dict_find_match(smux_dict_t *dict, const uint8_t *data, size_t len,
                          uint32_t *match_offset, uint32_t *match_length);
uint32_t smux_dict_hash3(const uint8_t *data);
void smux_dict_insert_hash(smux_dict_t *dict, uint32_t pos);

smux_status_t smux_compress_frame(smux_compress_ctx_t *ctx, smux_frame_t *frame,
                                   uint8_t *work_buf, size_t work_cap);
smux_status_t smux_decompress_frame(smux_compress_ctx_t *ctx, smux_frame_t *frame,
                                     uint8_t *work_buf, size_t work_cap);

#endif
