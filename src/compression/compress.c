#include "compress.h"
#include "../core/byte_utils.h"
#include <stdlib.h>
#include <string.h>

smux_compress_ctx_t *smux_compress_create(uint32_t dict_size) {
    smux_compress_ctx_t *ctx = calloc(1, sizeof(smux_compress_ctx_t));
    if (!ctx) return NULL;

    if (dict_size == 0 || dict_size > SMUX_DICT_SIZE)
        dict_size = SMUX_DICT_SIZE;

    smux_dict_init(&ctx->dict, dict_size);

    ctx->output_cap = SMUX_MAX_FRAME_SIZE;
    ctx->output_buf = malloc(ctx->output_cap);
    if (!ctx->output_buf) {
        free(ctx);
        return NULL;
    }
    ctx->enabled = true;
    return ctx;
}

void smux_compress_destroy(smux_compress_ctx_t *ctx) {
    if (!ctx) return;
    free(ctx->dict.window);
    if (ctx->pending_slice) {
        smux_slice_release(ctx->pending_slice);
    }
    free(ctx->output_buf);
    free(ctx);
}

void smux_compress_reset(smux_compress_ctx_t *ctx) {
    if (!ctx) return;
    smux_dict_reset(&ctx->dict);
    ctx->output_len = 0;
    ctx->bytes_in = 0;
    ctx->bytes_out = 0;
    if (ctx->pending_slice) {
        smux_slice_release(ctx->pending_slice);
        /* pending_slice reference retained for dictionary continuity;
           slice_release decrements refcount, actual free only when
           refcount reaches zero */
    }
}

smux_status_t smux_compress_data(smux_compress_ctx_t *ctx,
                                  const uint8_t *input, size_t in_len,
                                  uint8_t *output, size_t out_cap,
                                  size_t *out_len) {
    if (!ctx || !input || !output || !out_len) return SMUX_ERR_INVALID;
    if (!ctx->enabled) {
        if (in_len > out_cap) return SMUX_ERR_OVERFLOW;
        memcpy(output, input, in_len);
        *out_len = in_len;
        return SMUX_OK;
    }

    size_t out_pos = 0;
    size_t in_pos = 0;

    /* Reserve space for uncompressed length prefix (4 bytes) */
    if (out_cap < 4) return SMUX_ERR_OVERFLOW;
    smux_write_u32_be(output, (uint32_t)in_len);
    out_pos = 4;

    /* LZ77 compression with literal/match encoding */
    while (in_pos < in_len) {
        if (out_pos + 3 > out_cap) return SMUX_ERR_OVERFLOW;

        uint32_t match_off = 0, match_len = 0;
        size_t remaining = in_len - in_pos;

        int found = smux_dict_find_match(&ctx->dict, input + in_pos, remaining,
                                          &match_off, &match_len);

        if (found && match_len >= SMUX_LZ_MIN_MATCH) {
            /* Encode match: flag byte (0x80 | len_hi), offset_lo, offset_hi, len_lo */
            if (match_len > SMUX_LZ_MAX_MATCH) match_len = SMUX_LZ_MAX_MATCH;
            if (out_pos + 3 > out_cap) return SMUX_ERR_OVERFLOW;

            output[out_pos++] = 0x80 | (uint8_t)((match_len >> 8) & 0x01);
            output[out_pos++] = (uint8_t)(match_off & 0xFF);
            output[out_pos++] = (uint8_t)(((match_off >> 8) & 0x0F) |
                                           ((match_len & 0x0F) << 4));

            smux_dict_update(&ctx->dict, input + in_pos, match_len);
            in_pos += match_len;
        } else {
            /* Literal byte */
            uint8_t lit = input[in_pos];
            if (lit & 0x80) {
                /* Escape high-bit literals */
                if (out_pos + 2 > out_cap) return SMUX_ERR_OVERFLOW;
                output[out_pos++] = 0x00; /* literal escape */
                output[out_pos++] = lit;
            } else {
                output[out_pos++] = lit;
            }
            smux_dict_update(&ctx->dict, input + in_pos, 1);
            in_pos++;
        }
    }

    *out_len = out_pos;
    ctx->bytes_in += (uint32_t)in_len;
    ctx->bytes_out += (uint32_t)out_pos;
    return SMUX_OK;
}

smux_status_t smux_decompress_data(smux_compress_ctx_t *ctx,
                                    const uint8_t *input, size_t in_len,
                                    uint8_t *output, size_t out_cap,
                                    size_t *out_len) {
    if (!ctx || !input || !output || !out_len) return SMUX_ERR_INVALID;
    if (in_len < 4) return SMUX_ERR_TRUNCATED;

    uint32_t expected_len = smux_read_u32_be(input);
    if (expected_len > out_cap) return SMUX_ERR_OVERFLOW;

    size_t in_pos = 4;
    size_t out_pos = 0;

    while (in_pos < in_len && out_pos < expected_len) {
        uint8_t tag = input[in_pos++];

        if (tag & 0x80) {
            /* Match reference */
            if (in_pos + 2 > in_len) return SMUX_ERR_TRUNCATED;
            uint8_t off_lo = input[in_pos++];
            uint8_t mixed = input[in_pos++];

            uint32_t offset = off_lo | (((uint32_t)(mixed & 0x0F)) << 8);
            uint32_t length = ((uint32_t)(mixed >> 4)) | ((uint32_t)(tag & 0x01) << 8);
            if (length < SMUX_LZ_MIN_MATCH) length += SMUX_LZ_MIN_MATCH;

            if (offset == 0 || offset > out_pos) return SMUX_ERR_DECOMPRESS;
            if (out_pos + length > out_cap) return SMUX_ERR_OVERFLOW;

            /* Copy from already-decoded output (may overlap) */
            size_t src_pos = out_pos - offset;
            for (uint32_t i = 0; i < length; i++) {
                output[out_pos++] = output[src_pos + (i % offset)];
            }
            smux_dict_update(&ctx->dict, output + out_pos - length, length);
        } else if (tag == 0x00) {
            /* Escaped literal */
            if (in_pos >= in_len) return SMUX_ERR_TRUNCATED;
            output[out_pos++] = input[in_pos++];
            smux_dict_update(&ctx->dict, &output[out_pos - 1], 1);
        } else {
            /* Direct literal */
            output[out_pos++] = tag;
            smux_dict_update(&ctx->dict, &output[out_pos - 1], 1);
        }
    }

    *out_len = out_pos;
    return SMUX_OK;
}

smux_status_t smux_compress_frame(smux_compress_ctx_t *ctx, smux_frame_t *frame,
                                   uint8_t *work_buf, size_t work_cap) {
    if (!ctx || !frame || !work_buf) return SMUX_ERR_INVALID;
    if (!ctx->enabled) return SMUX_OK;
    if (frame->payload_len == 0) return SMUX_OK;

    size_t comp_len = 0;
    smux_status_t rc = smux_compress_data(ctx, frame->payload, frame->payload_len,
                                           work_buf, work_cap, &comp_len);
    if (rc != SMUX_OK) return rc;

    /* Only use compressed version if it's actually smaller */
    if (comp_len < frame->payload_len) {
        memcpy(frame->payload, work_buf, comp_len);
        frame->payload_len = (uint32_t)comp_len;
        frame->flags |= SMUX_FLAG_COMPRESSED;
    }
    return SMUX_OK;
}

smux_status_t smux_decompress_frame(smux_compress_ctx_t *ctx, smux_frame_t *frame,
                                     uint8_t *work_buf, size_t work_cap) {
    if (!ctx || !frame || !work_buf) return SMUX_ERR_INVALID;
    if (!(frame->flags & SMUX_FLAG_COMPRESSED)) return SMUX_OK;

    size_t decomp_len = 0;
    smux_status_t rc = smux_decompress_data(ctx, frame->payload, frame->payload_len,
                                             work_buf, work_cap, &decomp_len);
    if (rc != SMUX_OK) return rc;

    memcpy(frame->payload, work_buf, decomp_len);
    frame->payload_len = (uint32_t)decomp_len;
    frame->flags &= ~SMUX_FLAG_COMPRESSED;
    return SMUX_OK;
}
