#include "frame.h"
#include "../core/byte_utils.h"
#include <string.h>

/* Header extension codec: packs structured metadata into extension slots */

typedef struct {
    uint64_t timestamp_us;
} smux_ext_timestamp_t;

typedef struct {
    uint8_t  trace_id[16];
} smux_ext_trace_t;

typedef struct {
    uint8_t  priority;
    uint8_t  weight;
    uint16_t dependency;
} smux_ext_priority_t;

typedef struct {
    uint16_t route_id;
    uint16_t hop_count;
    uint32_t path_hash;
} smux_ext_route_tag_t;

typedef struct {
    uint16_t fragment_id;
    uint16_t fragment_total;
    uint32_t message_len;
    uint32_t fragment_offset;
} smux_ext_frag_meta_t;

int smux_ext_encode_timestamp(smux_frame_t *frame, uint64_t ts_us) {
    uint8_t buf[8];
    smux_write_u64_be(buf, ts_us);
    return smux_frame_add_extension(frame, SMUX_EXT_TIMESTAMP, buf, 8);
}

int smux_ext_decode_timestamp(const smux_frame_t *frame, uint64_t *ts_us) {
    const smux_extension_t *ext = smux_frame_find_extension(frame, SMUX_EXT_TIMESTAMP);
    if (!ext || ext->ext_len < 8) return SMUX_ERR_INVALID;
    *ts_us = smux_read_u64_be(ext->ext_data);
    return SMUX_OK;
}

int smux_ext_encode_trace(smux_frame_t *frame, const uint8_t trace_id[16]) {
    return smux_frame_add_extension(frame, SMUX_EXT_TRACE_ID, trace_id, 16);
}

int smux_ext_decode_trace(const smux_frame_t *frame, uint8_t trace_id[16]) {
    const smux_extension_t *ext = smux_frame_find_extension(frame, SMUX_EXT_TRACE_ID);
    if (!ext || ext->ext_len < 16) return SMUX_ERR_INVALID;
    memcpy(trace_id, ext->ext_data, 16);
    return SMUX_OK;
}

int smux_ext_encode_priority(smux_frame_t *frame, uint8_t pri, uint8_t wt, uint16_t dep) {
    uint8_t buf[4];
    buf[0] = pri;
    buf[1] = wt;
    smux_write_u16_be(buf + 2, dep);
    return smux_frame_add_extension(frame, SMUX_EXT_PRIORITY, buf, 4);
}

int smux_ext_decode_priority(const smux_frame_t *frame, uint8_t *pri, uint8_t *wt, uint16_t *dep) {
    const smux_extension_t *ext = smux_frame_find_extension(frame, SMUX_EXT_PRIORITY);
    if (!ext || ext->ext_len < 4) return SMUX_ERR_INVALID;
    *pri = ext->ext_data[0];
    *wt = ext->ext_data[1];
    *dep = smux_read_u16_be(ext->ext_data + 2);
    return SMUX_OK;
}

int smux_ext_encode_route_tag(smux_frame_t *frame, uint16_t route_id,
                               uint16_t hops, uint32_t path_hash) {
    uint8_t buf[8];
    smux_write_u16_be(buf, route_id);
    smux_write_u16_be(buf + 2, hops);
    smux_write_u32_be(buf + 4, path_hash);
    return smux_frame_add_extension(frame, SMUX_EXT_ROUTE_TAG, buf, 8);
}

int smux_ext_decode_route_tag(const smux_frame_t *frame, uint16_t *route_id,
                               uint16_t *hops, uint32_t *path_hash) {
    const smux_extension_t *ext = smux_frame_find_extension(frame, SMUX_EXT_ROUTE_TAG);
    if (!ext || ext->ext_len < 8) return SMUX_ERR_INVALID;
    *route_id = smux_read_u16_be(ext->ext_data);
    *hops = smux_read_u16_be(ext->ext_data + 2);
    *path_hash = smux_read_u32_be(ext->ext_data + 4);
    return SMUX_OK;
}

int smux_ext_encode_frag_meta(smux_frame_t *frame, uint16_t frag_id,
                               uint16_t frag_total, uint32_t msg_len,
                               uint32_t frag_offset) {
    uint8_t buf[12];
    smux_write_u16_be(buf, frag_id);
    smux_write_u16_be(buf + 2, frag_total);
    smux_write_u32_be(buf + 4, msg_len);
    smux_write_u32_be(buf + 8, frag_offset);
    return smux_frame_add_extension(frame, SMUX_EXT_FRAG_META, buf, 12);
}

int smux_ext_decode_frag_meta(const smux_frame_t *frame, uint16_t *frag_id,
                               uint16_t *frag_total, uint32_t *msg_len,
                               uint32_t *frag_offset) {
    const smux_extension_t *ext = smux_frame_find_extension(frame, SMUX_EXT_FRAG_META);
    if (!ext || ext->ext_len < 12) return SMUX_ERR_INVALID;
    *frag_id = smux_read_u16_be(ext->ext_data);
    *frag_total = smux_read_u16_be(ext->ext_data + 2);
    *msg_len = smux_read_u32_be(ext->ext_data + 4);
    *frag_offset = smux_read_u32_be(ext->ext_data + 8);
    return SMUX_OK;
}
