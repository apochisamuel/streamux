#include "frame.h"
#include "../core/byte_utils.h"
#include "../checksum/checksum.h"
#include <string.h>

/* Batch frame construction and parsing.
   A BATCH frame contains multiple sub-frames packed into a single
   outer frame, reducing per-frame CRC/header overhead on busy connections. */

typedef struct {
    smux_frame_t    frames[SMUX_BATCH_MAX];
    int             count;
    size_t          total_size;
    uint8_t         work_buf[SMUX_MAX_FRAME_SIZE];
    size_t          work_len;
} smux_batch_builder_t;

void smux_batch_init(smux_batch_builder_t *b) {
    if (!b) return;
    memset(b, 0, sizeof(*b));
}

int smux_batch_add(smux_batch_builder_t *b, const smux_frame_t *frame) {
    if (!b || !frame) return SMUX_ERR_INVALID;
    if (b->count >= SMUX_BATCH_MAX) return SMUX_ERR_BATCH_FULL;

    size_t frame_size = smux_frame_wire_size(frame);
    if (b->total_size + frame_size > SMUX_MAX_PAYLOAD - 4) return SMUX_ERR_OVERFLOW;

    b->frames[b->count] = *frame;
    b->count++;
    b->total_size += frame_size;
    return SMUX_OK;
}

int smux_batch_serialize(const smux_batch_builder_t *b,
                          uint8_t *out, size_t out_cap, size_t *out_len) {
    if (!b || !out || !out_len) return SMUX_ERR_INVALID;
    if (b->count == 0) return SMUX_ERR_INVALID;

    /* Build outer BATCH frame */
    smux_frame_t batch;
    smux_frame_init(&batch, SMUX_FRAME_BATCH, 0);

    /* Payload: count(2) + total_len(2) + serialized sub-frames */
    uint8_t payload[SMUX_MAX_PAYLOAD];
    size_t pos = 0;

    smux_write_u16_be(payload, (uint16_t)b->count);
    smux_write_u16_be(payload + 2, (uint16_t)b->total_size);
    pos = 4;

    for (int i = 0; i < b->count; i++) {
        int written = smux_frame_serialize(&b->frames[i],
                                            payload + pos, sizeof(payload) - pos);
        if (written < 0) return SMUX_ERR_OVERFLOW;
        pos += (size_t)written;
    }

    batch.payload = payload;
    batch.payload_len = (uint32_t)pos;

    int written = smux_frame_serialize(&batch, out, out_cap);
    if (written < 0) return SMUX_ERR_OVERFLOW;
    *out_len = (size_t)written;
    return SMUX_OK;
}

int smux_batch_parse(const uint8_t *data, size_t len,
                      smux_frame_t *frames, int max_frames, int *count) {
    if (!data || !frames || !count) return SMUX_ERR_INVALID;
    if (len < 4) return SMUX_ERR_BAD_LENGTH;

    uint16_t batch_count = smux_read_u16_be(data);
    uint16_t batch_total = smux_read_u16_be(data + 2);
    (void)batch_total;

    if (batch_count > (uint16_t)max_frames) batch_count = (uint16_t)max_frames;

    const uint8_t *cursor = data + 4;
    size_t remaining = len - 4;
    int parsed = 0;

    for (uint16_t i = 0; i < batch_count; i++) {
        smux_status_t rc = smux_frame_parse(cursor, remaining, &frames[parsed]);
        if (rc != SMUX_OK) break;

        size_t frame_size = smux_frame_wire_size(&frames[parsed]);
        cursor += frame_size;
        remaining -= frame_size;
        parsed++;
    }

    *count = parsed;
    return parsed > 0 ? SMUX_OK : SMUX_ERR_TRUNCATED;
}

/* Classify frames for optimal batching by priority/urgency */
int smux_batch_classify(const smux_frame_t *frames, int count,
                         int *urgent_indices, int *urgent_count,
                         int *normal_indices, int *normal_count) {
    if (!frames || !urgent_indices || !normal_indices) return SMUX_ERR_INVALID;

    *urgent_count = 0;
    *normal_count = 0;

    for (int i = 0; i < count; i++) {
        if (frames[i].flags & SMUX_FLAG_URGENT ||
            frames[i].type == SMUX_FRAME_CLOSE ||
            frames[i].type == SMUX_FRAME_ERROR ||
            frames[i].type == SMUX_FRAME_KEY_ROTATE) {
            urgent_indices[(*urgent_count)++] = i;
        } else {
            normal_indices[(*normal_count)++] = i;
        }
    }
    return SMUX_OK;
}

/* Calculate optimal batch size based on frame sizes and wire overhead */
int smux_batch_optimal_count(const smux_frame_t *frames, int count,
                              size_t target_size) {
    if (!frames || count <= 0) return 0;

    size_t running_total = 4; /* batch header */
    int optimal = 0;

    for (int i = 0; i < count && i < SMUX_BATCH_MAX; i++) {
        size_t frame_size = smux_frame_wire_size(&frames[i]);
        if (running_total + frame_size > target_size) break;
        running_total += frame_size;
        optimal = i + 1;
    }

    /* Batching only worth it for 2+ frames */
    return optimal >= 2 ? optimal : 0;
}
