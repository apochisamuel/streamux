#include "api.h"
#include "../frame/frame.h"
#include "../core/byte_utils.h"
#include "../checksum/checksum.h"
#include <string.h>

extern void smux_ext_encode_frag_meta(smux_frame_t *frame, uint16_t frag_id, uint16_t frag_total, uint32_t msg_len, uint32_t offset);

/* Seed corpus builders: create valid frame sequences for testing */

int smux_build_handshake_seed(uint8_t *buf, size_t cap, size_t *out_len,
                               uint32_t window) {
    if (!buf || !out_len || cap < 256) return SMUX_ERR_INVALID;
    size_t pos = 0;

    /* HELLO frame */
    smux_frame_t hello;
    smux_frame_init(&hello, SMUX_FRAME_HELLO, 0);
    hello.sequence = 1;

    uint8_t hello_payload[24];
    smux_write_u16_be(hello_payload, SMUX_VERSION_MAJOR);
    smux_write_u16_be(hello_payload + 2, SMUX_VERSION_MINOR);
    smux_write_u32_be(hello_payload + 4, window);
    memset(hello_payload + 8, 0x41, 16); /* session ID */
    hello.payload = hello_payload;
    hello.payload_len = 24;

    int written = smux_frame_serialize(&hello, buf + pos, cap - pos);
    if (written < 0) return SMUX_ERR_OVERFLOW;
    pos += (size_t)written;

    /* HELLO_ACK frame */
    smux_frame_t ack;
    smux_frame_init(&ack, SMUX_FRAME_HELLO_ACK, 0);
    ack.sequence = 2;

    uint8_t ack_payload[24];
    smux_write_u16_be(ack_payload, SMUX_VERSION_MAJOR);
    smux_write_u16_be(ack_payload + 2, SMUX_VERSION_MINOR);
    smux_write_u32_be(ack_payload + 4, window);
    memset(ack_payload + 8, 0x42, 16);
    ack.payload = ack_payload;
    ack.payload_len = 24;

    written = smux_frame_serialize(&ack, buf + pos, cap - pos);
    if (written < 0) return SMUX_ERR_OVERFLOW;
    pos += (size_t)written;

    *out_len = pos;
    return SMUX_OK;
}

int smux_build_data_seed(uint8_t *buf, size_t cap, size_t *out_len,
                          uint16_t channel_id, const uint8_t *payload,
                          uint32_t payload_len) {
    if (!buf || !out_len || cap < 128) return SMUX_ERR_INVALID;
    size_t pos = 0;

    /* Handshake first */
    size_t hs_len = 0;
    int rc = smux_build_handshake_seed(buf, cap, &hs_len, SMUX_DEFAULT_WINDOW);
    if (rc != SMUX_OK) return rc;
    pos = hs_len;

    /* DATA frame */
    smux_frame_t data;
    smux_frame_init(&data, SMUX_FRAME_DATA, channel_id);
    data.sequence = 3;

    if (payload && payload_len > 0) {
        data.payload = (uint8_t *)payload;
        data.payload_len = payload_len;
    } else {
        uint8_t default_payload[64];
        memset(default_payload, 'D', sizeof(default_payload));
        data.payload = default_payload;
        data.payload_len = 64;
    }

    int written = smux_frame_serialize(&data, buf + pos, cap - pos);
    if (written < 0) return SMUX_ERR_OVERFLOW;
    pos += (size_t)written;

    *out_len = pos;
    return SMUX_OK;
}

int smux_build_fragment_seed(uint8_t *buf, size_t cap, size_t *out_len,
                              uint16_t channel_id, uint16_t total_frags,
                              uint32_t msg_len) {
    if (!buf || !out_len || cap < 512) return SMUX_ERR_INVALID;
    if (total_frags == 0 || total_frags > 16) total_frags = 3;

    size_t pos = 0;

    /* Handshake */
    size_t hs_len = 0;
    int rc = smux_build_handshake_seed(buf, cap, &hs_len, SMUX_DEFAULT_WINDOW);
    if (rc != SMUX_OK) return rc;
    pos = hs_len;

    uint32_t frag_size = msg_len / total_frags;
    uint8_t frag_payload[256];
    uint32_t seq = 3;

    for (uint16_t i = 0; i < total_frags; i++) {
        smux_frame_t frag;
        int is_final = (i == total_frags - 1);
        smux_frame_init(&frag, is_final ? SMUX_FRAME_FRAG_END : SMUX_FRAME_FRAGMENT,
                        channel_id);
        frag.sequence = seq++;

        uint32_t this_size = is_final ? (msg_len - frag_size * i) : frag_size;
        if (this_size > sizeof(frag_payload)) this_size = sizeof(frag_payload);
        memset(frag_payload, 'F' + i, this_size);
        frag.payload = frag_payload;
        frag.payload_len = this_size;

        smux_ext_encode_frag_meta(&frag, 1, total_frags, msg_len, frag_size * i);

        int written = smux_frame_serialize(&frag, buf + pos, cap - pos);
        if (written < 0) return SMUX_ERR_OVERFLOW;
        pos += (size_t)written;
    }

    *out_len = pos;
    return SMUX_OK;
}

int smux_build_migrate_seed(uint8_t *buf, size_t cap, size_t *out_len,
                             uint16_t from_route, uint16_t to_route) {
    if (!buf || !out_len || cap < 512) return SMUX_ERR_INVALID;
    size_t pos = 0;

    /* Handshake */
    size_t hs_len = 0;
    int rc = smux_build_handshake_seed(buf, cap, &hs_len, SMUX_DEFAULT_WINDOW);
    if (rc != SMUX_OK) return rc;
    pos = hs_len;

    /* Open a channel with DATA */
    uint8_t data_payload[32];
    memset(data_payload, 'M', sizeof(data_payload));
    smux_frame_t data;
    smux_frame_init(&data, SMUX_FRAME_DATA, 1);
    data.sequence = 3;
    data.payload = data_payload;
    data.payload_len = 32;
    int written = smux_frame_serialize(&data, buf + pos, cap - pos);
    if (written < 0) return SMUX_ERR_OVERFLOW;
    pos += (size_t)written;

    /* MIGRATE frame */
    smux_frame_t migrate;
    smux_frame_init(&migrate, SMUX_FRAME_MIGRATE, 0);
    migrate.sequence = 4;

    uint8_t mig_payload[6];
    smux_write_u16_be(mig_payload, from_route);
    smux_write_u16_be(mig_payload + 2, to_route);
    smux_write_u16_be(mig_payload + 4, 1); /* 1 channel to migrate */
    migrate.payload = mig_payload;
    migrate.payload_len = 6;

    written = smux_frame_serialize(&migrate, buf + pos, cap - pos);
    if (written < 0) return SMUX_ERR_OVERFLOW;
    pos += (size_t)written;

    /* MIGRATE_ACK */
    smux_frame_t mig_ack;
    smux_frame_init(&mig_ack, SMUX_FRAME_MIGRATE_ACK, 0);
    mig_ack.sequence = 5;
    mig_ack.payload_len = 0;

    written = smux_frame_serialize(&mig_ack, buf + pos, cap - pos);
    if (written < 0) return SMUX_ERR_OVERFLOW;
    pos += (size_t)written;

    *out_len = pos;
    return SMUX_OK;
}
