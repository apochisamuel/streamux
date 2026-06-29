#include "../core/types.h"
#include "../core/byte_utils.h"
#include "../frame/frame.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void smux_ext_encode_frag_meta(smux_frame_t *frame, uint16_t frag_id, uint16_t frag_total, uint32_t msg_len, uint32_t offset);

/* CLI encoder: builds frames from command descriptions */

typedef struct {
    uint8_t     type;
    uint16_t    channel_id;
    uint32_t    sequence;
    uint8_t     flags;
    uint8_t    *payload;
    uint32_t    payload_len;
} smux_encode_cmd_t;

static int parse_frame_type(const char *name) {
    struct { const char *n; int t; } types[] = {
        {"hello", SMUX_FRAME_HELLO}, {"hello_ack", SMUX_FRAME_HELLO_ACK},
        {"data", SMUX_FRAME_DATA}, {"fragment", SMUX_FRAME_FRAGMENT},
        {"frag_end", SMUX_FRAME_FRAG_END}, {"close", SMUX_FRAME_CLOSE},
        {"error", SMUX_FRAME_ERROR}, {"window", SMUX_FRAME_WINDOW},
        {"ping", SMUX_FRAME_PING}, {"pong", SMUX_FRAME_PONG},
        {"receipt", SMUX_FRAME_RECEIPT}, {"receipt_ack", SMUX_FRAME_RECEIPT_ACK},
        {"migrate", SMUX_FRAME_MIGRATE}, {"migrate_ack", SMUX_FRAME_MIGRATE_ACK},
        {"key_rotate", SMUX_FRAME_KEY_ROTATE}, {"compress", SMUX_FRAME_COMPRESS},
        {"priority", SMUX_FRAME_PRIORITY}, {"batch", SMUX_FRAME_BATCH},
        {NULL, 0}
    };
    for (int i = 0; types[i].n; i++) {
        if (strcmp(name, types[i].n) == 0) return types[i].t;
    }
    return -1;
}

int smux_cli_encode_hello(uint8_t *buf, size_t buf_len, size_t *out_len,
                           uint16_t version_major, uint16_t version_minor,
                           uint32_t max_window) {
    smux_frame_t frame;
    smux_frame_init(&frame, SMUX_FRAME_HELLO, 0);

    uint8_t payload[24];
    smux_write_u16_be(payload, version_major);
    smux_write_u16_be(payload + 2, version_minor);
    smux_write_u32_be(payload + 4, max_window);
    memset(payload + 8, 0, 16); /* session id placeholder */
    frame.payload = payload;
    frame.payload_len = 24;

    int written = smux_frame_serialize(&frame, buf, buf_len);
    if (written < 0) return written;
    *out_len = (size_t)written;
    return 0;
}

int smux_cli_encode_hello_ack(uint8_t *buf, size_t buf_len, size_t *out_len,
                               uint16_t version_major, uint16_t version_minor,
                               uint32_t negotiated_window, uint32_t seq) {
    smux_frame_t frame;
    smux_frame_init(&frame, SMUX_FRAME_HELLO_ACK, 0);
    frame.sequence = seq;

    uint8_t payload[24];
    smux_write_u16_be(payload, version_major);
    smux_write_u16_be(payload + 2, version_minor);
    smux_write_u32_be(payload + 4, negotiated_window);
    memset(payload + 8, 0, 16);
    frame.payload = payload;
    frame.payload_len = 24;

    int written = smux_frame_serialize(&frame, buf, buf_len);
    if (written < 0) return written;
    *out_len = (size_t)written;
    return 0;
}

int smux_cli_encode_data(uint8_t *buf, size_t buf_len, size_t *out_len,
                          uint16_t channel_id, uint32_t seq,
                          const uint8_t *payload, uint32_t payload_len,
                          uint8_t flags) {
    smux_frame_t frame;
    smux_frame_init(&frame, SMUX_FRAME_DATA, channel_id);
    frame.sequence = seq;
    frame.flags = flags;
    frame.payload = (uint8_t *)payload;
    frame.payload_len = payload_len;

    int written = smux_frame_serialize(&frame, buf, buf_len);
    if (written < 0) return written;
    *out_len = (size_t)written;
    return 0;
}

int smux_cli_encode_fragment(uint8_t *buf, size_t buf_len, size_t *out_len,
                              uint16_t channel_id, uint32_t seq,
                              uint16_t frag_id, uint16_t frag_total,
                              uint32_t msg_len, uint32_t frag_offset,
                              const uint8_t *payload, uint32_t payload_len,
                              int is_final) {
    smux_frame_t frame;
    smux_frame_init(&frame, is_final ? SMUX_FRAME_FRAG_END : SMUX_FRAME_FRAGMENT, channel_id);
    frame.sequence = seq;
    frame.payload = (uint8_t *)payload;
    frame.payload_len = payload_len;

    smux_ext_encode_frag_meta(&frame, frag_id, frag_total, msg_len, frag_offset);

    int written = smux_frame_serialize(&frame, buf, buf_len);
    if (written < 0) return written;
    *out_len = (size_t)written;
    return 0;
}

int smux_cli_encode_close(uint8_t *buf, size_t buf_len, size_t *out_len,
                           uint16_t channel_id, uint32_t seq) {
    smux_frame_t frame;
    smux_frame_init(&frame, SMUX_FRAME_CLOSE, channel_id);
    frame.sequence = seq;
    frame.payload_len = 0;

    int written = smux_frame_serialize(&frame, buf, buf_len);
    if (written < 0) return written;
    *out_len = (size_t)written;
    return 0;
}

int smux_cli_encode_error(uint8_t *buf, size_t buf_len, size_t *out_len,
                           uint16_t channel_id, uint32_t seq, uint32_t error_code) {
    smux_frame_t frame;
    smux_frame_init(&frame, SMUX_FRAME_ERROR, channel_id);
    frame.sequence = seq;

    uint8_t payload[4];
    smux_write_u32_be(payload, error_code);
    frame.payload = payload;
    frame.payload_len = 4;

    int written = smux_frame_serialize(&frame, buf, buf_len);
    if (written < 0) return written;
    *out_len = (size_t)written;
    return 0;
}

int smux_cli_encode_window(uint8_t *buf, size_t buf_len, size_t *out_len,
                            uint16_t channel_id, uint32_t seq, uint32_t new_window) {
    smux_frame_t frame;
    smux_frame_init(&frame, SMUX_FRAME_WINDOW, channel_id);
    frame.sequence = seq;

    uint8_t payload[4];
    smux_write_u32_be(payload, new_window);
    frame.payload = payload;
    frame.payload_len = 4;

    int written = smux_frame_serialize(&frame, buf, buf_len);
    if (written < 0) return written;
    *out_len = (size_t)written;
    return 0;
}

int smux_cli_encode_receipt(uint8_t *buf, size_t buf_len, size_t *out_len,
                             uint16_t channel_id, uint32_t seq, uint32_t acked_seq) {
    smux_frame_t frame;
    smux_frame_init(&frame, SMUX_FRAME_RECEIPT, channel_id);
    frame.sequence = seq;

    uint8_t payload[4];
    smux_write_u32_be(payload, acked_seq);
    frame.payload = payload;
    frame.payload_len = 4;

    int written = smux_frame_serialize(&frame, buf, buf_len);
    if (written < 0) return written;
    *out_len = (size_t)written;
    return 0;
}

int smux_cli_encode_migrate(uint8_t *buf, size_t buf_len, size_t *out_len,
                             uint32_t seq, uint16_t from_route, uint16_t to_route,
                             uint16_t channel_count) {
    smux_frame_t frame;
    smux_frame_init(&frame, SMUX_FRAME_MIGRATE, 0);
    frame.sequence = seq;

    uint8_t payload[6];
    smux_write_u16_be(payload, from_route);
    smux_write_u16_be(payload + 2, to_route);
    smux_write_u16_be(payload + 4, channel_count);
    frame.payload = payload;
    frame.payload_len = 6;

    int written = smux_frame_serialize(&frame, buf, buf_len);
    if (written < 0) return written;
    *out_len = (size_t)written;
    return 0;
}

int smux_cli_encode_priority(uint8_t *buf, size_t buf_len, size_t *out_len,
                              uint16_t channel_id, uint32_t seq,
                              uint8_t priority, uint8_t weight) {
    smux_frame_t frame;
    smux_frame_init(&frame, SMUX_FRAME_PRIORITY, channel_id);
    frame.sequence = seq;

    uint8_t payload[2];
    payload[0] = priority;
    payload[1] = weight;
    frame.payload = payload;
    frame.payload_len = 2;

    int written = smux_frame_serialize(&frame, buf, buf_len);
    if (written < 0) return written;
    *out_len = (size_t)written;
    return 0;
}

int smux_cli_encode_ping(uint8_t *buf, size_t buf_len, size_t *out_len, uint32_t seq) {
    smux_frame_t frame;
    smux_frame_init(&frame, SMUX_FRAME_PING, 0);
    frame.sequence = seq;
    frame.payload_len = 0;

    int written = smux_frame_serialize(&frame, buf, buf_len);
    if (written < 0) return written;
    *out_len = (size_t)written;
    return 0;
}
