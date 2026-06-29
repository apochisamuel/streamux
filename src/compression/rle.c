#include "compress.h"
#include "../core/byte_utils.h"
#include <string.h>

/* Run-Length Encoding for metadata and control frames.
   Used as a lightweight alternative to LZ77 for frames
   with highly repetitive content (e.g., zero-filled padding,
   repeated sequence numbers). */

typedef struct {
    uint32_t    runs_encoded;
    uint32_t    runs_decoded;
    uint64_t    bytes_saved;
    uint64_t    bytes_expanded;
} smux_rle_stats_t;

/* RLE format:
   Literal:  0x00-0x7F → copy next (tag+1) bytes literally
   Run:      0x80-0xFF → repeat next byte ((tag & 0x7F)+3) times
   Min run length = 3 (shorter runs use literals) */

smux_status_t smux_rle_encode(const uint8_t *input, size_t in_len,
                               uint8_t *output, size_t out_cap,
                               size_t *out_len, smux_rle_stats_t *stats) {
    if (!input || !output || !out_len) return SMUX_ERR_INVALID;

    size_t in_pos = 0, out_pos = 0;

    while (in_pos < in_len) {
        /* Check for run */
        size_t run_len = 1;
        while (in_pos + run_len < in_len &&
               input[in_pos + run_len] == input[in_pos] &&
               run_len < 130) {
            run_len++;
        }

        if (run_len >= 3) {
            /* Encode run */
            if (out_pos + 2 > out_cap) return SMUX_ERR_OVERFLOW;
            output[out_pos++] = 0x80 | (uint8_t)(run_len - 3);
            output[out_pos++] = input[in_pos];
            in_pos += run_len;
            if (stats) {
                stats->runs_encoded++;
                stats->bytes_saved += run_len - 2;
            }
        } else {
            /* Collect literal sequence */
            size_t lit_start = in_pos;
            while (in_pos < in_len && in_pos - lit_start < 128) {
                /* Look ahead for a run */
                size_t ahead_run = 1;
                while (in_pos + ahead_run < in_len &&
                       input[in_pos + ahead_run] == input[in_pos] &&
                       ahead_run < 3) {
                    ahead_run++;
                }
                if (ahead_run >= 3) break;
                in_pos++;
            }

            size_t lit_len = in_pos - lit_start;
            if (lit_len == 0) {
                in_pos++;
                continue;
            }

            if (out_pos + 1 + lit_len > out_cap) return SMUX_ERR_OVERFLOW;
            output[out_pos++] = (uint8_t)(lit_len - 1);
            memcpy(output + out_pos, input + lit_start, lit_len);
            out_pos += lit_len;
        }
    }

    *out_len = out_pos;
    return SMUX_OK;
}

smux_status_t smux_rle_decode(const uint8_t *input, size_t in_len,
                               uint8_t *output, size_t out_cap,
                               size_t *out_len, smux_rle_stats_t *stats) {
    if (!input || !output || !out_len) return SMUX_ERR_INVALID;

    size_t in_pos = 0, out_pos = 0;

    while (in_pos < in_len) {
        uint8_t tag = input[in_pos++];

        if (tag & 0x80) {
            /* Run */
            size_t run_len = (tag & 0x7F) + 3;
            if (in_pos >= in_len) return SMUX_ERR_TRUNCATED;
            if (out_pos + run_len > out_cap) return SMUX_ERR_OVERFLOW;
            uint8_t val = input[in_pos++];
            memset(output + out_pos, val, run_len);
            out_pos += run_len;
            if (stats) stats->runs_decoded++;
        } else {
            /* Literal */
            size_t lit_len = (size_t)tag + 1;
            if (in_pos + lit_len > in_len) return SMUX_ERR_TRUNCATED;
            if (out_pos + lit_len > out_cap) return SMUX_ERR_OVERFLOW;
            memcpy(output + out_pos, input + in_pos, lit_len);
            in_pos += lit_len;
            out_pos += lit_len;
        }
    }

    *out_len = out_pos;
    return SMUX_OK;
}

/* RLE frame compression: used for CONTROL/WINDOW frames */
smux_status_t smux_rle_compress_frame(smux_frame_t *frame,
                                       uint8_t *work_buf, size_t work_cap) {
    if (!frame || !work_buf) return SMUX_ERR_INVALID;
    if (frame->payload_len == 0) return SMUX_OK;

    size_t rle_len = 0;
    smux_status_t rc = smux_rle_encode(frame->payload, frame->payload_len,
                                        work_buf, work_cap, &rle_len, NULL);
    if (rc != SMUX_OK) return rc;

    /* Only use RLE if it's actually smaller */
    if (rle_len < frame->payload_len) {
        memcpy(frame->payload, work_buf, rle_len);
        frame->payload_len = (uint32_t)rle_len;
        frame->flags |= SMUX_FLAG_COMPRESSED;
    }
    return SMUX_OK;
}

/* Estimate compressibility of data using run frequency heuristic */
float smux_rle_estimate_ratio(const uint8_t *data, size_t len) {
    if (!data || len < 4) return 1.0f;

    size_t run_bytes = 0;
    size_t pos = 0;

    while (pos < len) {
        size_t run = 1;
        while (pos + run < len && data[pos + run] == data[pos] && run < 130) {
            run++;
        }
        if (run >= 3) run_bytes += run;
        pos += run;
    }

    float run_fraction = (float)run_bytes / (float)len;
    /* Rough estimate: runs save (run_len - 2) bytes each */
    return 1.0f - run_fraction * 0.6f;
}
