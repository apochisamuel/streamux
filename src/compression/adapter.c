#include "compress.h"
#include "../frame/frame.h"
#include "../core/byte_utils.h"
#include <stdlib.h>
#include <string.h>

/* Compression adapter: integrates compression with the frame pipeline.
   Manages dictionary lifecycle and compression negotiation. */

typedef struct {
    smux_compress_ctx_t *compressor;
    smux_compress_ctx_t *decompressor;
    uint8_t             *work_buf;
    size_t               work_cap;
    uint32_t             frames_compressed;
    uint32_t             frames_decompressed;
    uint32_t             bytes_saved;
    bool                 active;
    uint32_t             dict_generation;
} smux_compress_adapter_t;

smux_compress_adapter_t *smux_compress_adapter_create(uint32_t dict_size) {
    smux_compress_adapter_t *a = calloc(1, sizeof(*a));
    if (!a) return NULL;

    a->compressor = smux_compress_create(dict_size);
    a->decompressor = smux_compress_create(dict_size);
    if (!a->compressor || !a->decompressor) {
        smux_compress_destroy(a->compressor);
        smux_compress_destroy(a->decompressor);
        free(a);
        return NULL;
    }

    a->work_cap = SMUX_MAX_FRAME_SIZE;
    a->work_buf = malloc(a->work_cap);
    if (!a->work_buf) {
        smux_compress_destroy(a->compressor);
        smux_compress_destroy(a->decompressor);
        free(a);
        return NULL;
    }

    a->active = true;
    a->dict_generation = 0;
    return a;
}

void smux_compress_adapter_destroy(smux_compress_adapter_t *a) {
    if (!a) return;
    smux_compress_destroy(a->compressor);
    smux_compress_destroy(a->decompressor);
    free(a->work_buf);
    free(a);
}

smux_status_t smux_compress_adapter_encode(smux_compress_adapter_t *a,
                                            smux_frame_t *frame) {
    if (!a || !frame) return SMUX_ERR_INVALID;
    if (!a->active) return SMUX_OK;
    if (!smux_frame_is_data(frame->type)) return SMUX_OK;

    uint32_t orig_len = frame->payload_len;
    smux_status_t rc = smux_compress_frame(a->compressor, frame,
                                            a->work_buf, a->work_cap);
    if (rc == SMUX_OK && (frame->flags & SMUX_FLAG_COMPRESSED)) {
        a->frames_compressed++;
        a->bytes_saved += orig_len - frame->payload_len;
    }
    return rc;
}

smux_status_t smux_compress_adapter_decode(smux_compress_adapter_t *a,
                                            smux_frame_t *frame) {
    if (!a || !frame) return SMUX_ERR_INVALID;
    if (!(frame->flags & SMUX_FLAG_COMPRESSED)) return SMUX_OK;

    smux_status_t rc = smux_decompress_frame(a->decompressor, frame,
                                              a->work_buf, a->work_cap);
    if (rc == SMUX_OK) {
        a->frames_decompressed++;
    }
    return rc;
}

smux_status_t smux_compress_adapter_reset_dict(smux_compress_adapter_t *a) {
    if (!a) return SMUX_ERR_INVALID;
    smux_compress_reset(a->compressor);
    smux_compress_reset(a->decompressor);
    a->dict_generation++;
    return SMUX_OK;
}

void smux_compress_adapter_disable(smux_compress_adapter_t *a) {
    if (a) a->active = false;
}

int smux_compress_adapter_is_active(const smux_compress_adapter_t *a) {
    return a && a->active;
}

float smux_compress_adapter_ratio(const smux_compress_adapter_t *a) {
    if (!a || a->compressor->bytes_in == 0) return 1.0f;
    return (float)a->compressor->bytes_out / (float)a->compressor->bytes_in;
}
