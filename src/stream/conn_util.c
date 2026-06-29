#include "stream.h"
#include "../frame/frame.h"
#include "../core/byte_utils.h"
#include "../diagnostics/diagnostics.h"
#include <stdlib.h>

/* External declarations */
extern smux_status_t smux_retry_queue_add(smux_retry_queue_t *q, uint32_t seq, uint16_t ch_id, const uint8_t *data, size_t len, uint64_t timestamp);
extern void smux_ext_encode_frag_meta(smux_frame_t *frame, uint16_t frag_id, uint16_t frag_total, uint32_t msg_len, uint32_t offset);
extern int smux_debug_session(const smux_session_t *s, char *buf, size_t len);
extern int smux_debug_channel_table(const smux_channel_table_t *ct, char *buf, size_t len);
extern int smux_debug_route_table(const smux_route_table_t *rt, char *buf, size_t len);
#include <string.h>

/* Connection utilities: convenience functions for common
   connection operations, batch processing, and lifecycle hooks. */

/* Send a data frame through the connection pipeline */
int smux_conn_send_data(smux_conn_t *conn, uint16_t channel_id,
                         const uint8_t *data, size_t len) {
    if (!conn || !data) return SMUX_ERR_INVALID;

    smux_channel_t *ch = smux_channel_table_get(conn->channels, channel_id);
    if (!ch || !smux_channel_accepts_data(ch)) return SMUX_ERR_BAD_STATE;

    /* Check flow control */
    if (smux_flow_available(&conn->flow[channel_id]) < (uint32_t)len) {
        return SMUX_ERR_OVERFLOW;
    }

    /* Build DATA frame */
    smux_frame_t frame;
    smux_frame_init(&frame, SMUX_FRAME_DATA, channel_id);
    frame.sequence = smux_session_next_seq(conn->session);
    frame.payload = (uint8_t *)data;
    frame.payload_len = (uint32_t)len;

    /* Encrypt if enabled */
    uint8_t enc_buf[SMUX_MAX_PAYLOAD];
    if (conn->crypto && conn->crypto->current.initialized) {
        memcpy(enc_buf, data, len);
        frame.payload = enc_buf;
        smux_crypto_encrypt_frame(conn->crypto, &frame);
    }

    /* Compress if enabled */
    uint8_t comp_buf[SMUX_MAX_PAYLOAD];
    if (conn->compressor) {
        smux_compress_frame(conn->compressor, &frame, comp_buf, sizeof(comp_buf));
    }

    /* Serialize and queue */
    uint8_t wire_buf[SMUX_MAX_FRAME_SIZE];
    int written = smux_frame_serialize(&frame, wire_buf, sizeof(wire_buf));
    if (written < 0) return SMUX_ERR_OVERFLOW;

    /* Add to send buffer */
    if (conn->send_len + (size_t)written > conn->send_cap) {
        size_t new_cap = conn->send_cap * 2;
        uint8_t *new_buf = realloc(conn->send_buf, new_cap);
        if (!new_buf) return SMUX_ERR_NOMEM;
        conn->send_buf = new_buf;
        conn->send_cap = new_cap;
    }
    memcpy(conn->send_buf + conn->send_len, wire_buf, (size_t)written);
    conn->send_len += (size_t)written;

    /* Update accounting */
    smux_flow_consume(&conn->flow[channel_id], (uint32_t)len);
    ch->bytes_sent += (uint32_t)len;
    ch->local_seq = frame.sequence;

    /* Queue for retransmission */
    smux_retry_queue_add(conn->retry_queue, frame.sequence, channel_id,
                          wire_buf, (size_t)written, conn->clock_ms);

    smux_diag_count(conn->diag, SMUX_STAT_FRAMES_TX);
    smux_diag_count_n(conn->diag, SMUX_STAT_BYTES_TX, len);
    smux_transcript_record(conn->transcript, SMUX_EVT_DATA_SEND, channel_id,
                          frame.sequence, NULL, 0);
    return SMUX_OK;
}

/* Send a fragmented message that exceeds max payload size */
int smux_conn_send_fragmented(smux_conn_t *conn, uint16_t channel_id,
                               const uint8_t *data, size_t len,
                               uint32_t max_fragment_size) {
    if (!conn || !data) return SMUX_ERR_INVALID;
    if (max_fragment_size == 0) max_fragment_size = SMUX_MAX_PAYLOAD / 2;

    uint16_t total_frags = (uint16_t)((len + max_fragment_size - 1) / max_fragment_size);
    uint16_t frag_id = (uint16_t)(conn->session->next_seq & 0xFFFF);
    size_t offset = 0;

    smux_diag_count(conn->diag, SMUX_STAT_FRAG_START);

    for (uint16_t i = 0; i < total_frags; i++) {
        size_t frag_len = len - offset;
        if (frag_len > max_fragment_size) frag_len = max_fragment_size;
        bool is_final = (i == total_frags - 1);

        smux_frame_t frame;
        smux_frame_init(&frame,
                        is_final ? SMUX_FRAME_FRAG_END : SMUX_FRAME_FRAGMENT,
                        channel_id);
        frame.sequence = smux_session_next_seq(conn->session);
        frame.payload = (uint8_t *)(data + offset);
        frame.payload_len = (uint32_t)frag_len;

        smux_ext_encode_frag_meta(&frame, frag_id, total_frags,
                                   (uint32_t)len, (uint32_t)offset);

        uint8_t wire_buf[SMUX_MAX_FRAME_SIZE];
        int written = smux_frame_serialize(&frame, wire_buf, sizeof(wire_buf));
        if (written < 0) return SMUX_ERR_OVERFLOW;

        if (conn->send_len + (size_t)written > conn->send_cap) {
            size_t new_cap = conn->send_cap * 2;
            uint8_t *new_buf = realloc(conn->send_buf, new_cap);
            if (!new_buf) return SMUX_ERR_NOMEM;
            conn->send_buf = new_buf;
            conn->send_cap = new_cap;
        }
        memcpy(conn->send_buf + conn->send_len, wire_buf, (size_t)written);
        conn->send_len += (size_t)written;

        offset += frag_len;
        smux_diag_count(conn->diag, SMUX_STAT_FRAMES_TX);
    }

    return SMUX_OK;
}

/* Batch-send multiple small frames as a single BATCH frame */
int smux_conn_send_batch(smux_conn_t *conn, const smux_frame_t *frames,
                          int count) {
    if (!conn || !frames || count <= 0) return SMUX_ERR_INVALID;
    if (count > SMUX_BATCH_MAX) return SMUX_ERR_BATCH_FULL;

    /* Build batch payload: count(2) + total_len(2) + frames */
    uint8_t batch_payload[SMUX_MAX_PAYLOAD];
    size_t pos = 0;

    smux_write_u16_be(batch_payload, (uint16_t)count);
    pos = 4; /* reserve 2 bytes for total_len */

    for (int i = 0; i < count; i++) {
        int written = smux_frame_serialize(&frames[i],
                                            batch_payload + pos,
                                            sizeof(batch_payload) - pos);
        if (written < 0) return SMUX_ERR_OVERFLOW;
        pos += (size_t)written;
    }

    smux_write_u16_be(batch_payload + 2, (uint16_t)(pos - 4));

    /* Wrap in BATCH frame */
    smux_frame_t batch;
    smux_frame_init(&batch, SMUX_FRAME_BATCH, 0);
    batch.sequence = smux_session_next_seq(conn->session);
    batch.payload = batch_payload;
    batch.payload_len = (uint32_t)pos;

    uint8_t wire_buf[SMUX_MAX_FRAME_SIZE];
    int written = smux_frame_serialize(&batch, wire_buf, sizeof(wire_buf));
    if (written < 0) return SMUX_ERR_OVERFLOW;

    if (conn->send_len + (size_t)written > conn->send_cap) {
        size_t new_cap = conn->send_cap * 2;
        uint8_t *new_buf = realloc(conn->send_buf, new_cap);
        if (!new_buf) return SMUX_ERR_NOMEM;
        conn->send_buf = new_buf;
        conn->send_cap = new_cap;
    }
    memcpy(conn->send_buf + conn->send_len, wire_buf, (size_t)written);
    conn->send_len += (size_t)written;

    smux_diag_count(conn->diag, SMUX_STAT_BATCH);
    return SMUX_OK;
}

/* Request window update from receiver */
int smux_conn_request_window(smux_conn_t *conn, uint16_t channel_id,
                              uint32_t new_window) {
    if (!conn) return SMUX_ERR_INVALID;

    smux_frame_t frame;
    smux_frame_init(&frame, SMUX_FRAME_WINDOW, channel_id);
    frame.sequence = smux_session_next_seq(conn->session);

    uint8_t payload[4];
    smux_write_u32_be(payload, new_window);
    frame.payload = payload;
    frame.payload_len = 4;

    uint8_t wire_buf[128];
    int written = smux_frame_serialize(&frame, wire_buf, sizeof(wire_buf));
    if (written < 0) return SMUX_ERR_OVERFLOW;

    memcpy(conn->send_buf + conn->send_len, wire_buf, (size_t)written);
    conn->send_len += (size_t)written;

    smux_diag_count(conn->diag, SMUX_STAT_WIN_UPD);
    return SMUX_OK;
}

/* Connection summary for debugging */
int smux_conn_summary(const smux_conn_t *conn, char *buf, size_t len) {
    if (!conn || !buf || len == 0) return 0;
    int written = 0;
    int remaining = (int)len;

    int n = smux_debug_session(conn->session, buf, len);
    if (n > 0) { written += n; remaining -= n; }

    if (remaining > 0) {
        n = smux_debug_channel_table(conn->channels, buf + written, (size_t)remaining);
        if (n > 0) { written += n; remaining -= n; }
    }

    if (remaining > 0) {
        n = smux_debug_route_table(conn->routes, buf + written, (size_t)remaining);
        if (n > 0) { written += n; remaining -= n; }
    }

    if (remaining > 0 && conn->diag) {
        n = smux_diag_report(conn->diag, buf + written, (size_t)remaining);
        if (n > 0) { written += n; remaining -= n; }
    }

    return written;
}
