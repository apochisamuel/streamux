#include "stream.h"
#include "../frame/frame.h"
#include "../core/byte_utils.h"
#include <string.h>
#include <stdlib.h>

/* External declarations */
extern smux_status_t smux_reader_consume_segments(smux_conn_t *conn, uint16_t channel_id);
extern int smux_segment_coalesce(smux_segment_list_t *list);
extern smux_status_t smux_migration_begin(smux_route_table_t *rt, smux_channel_table_t *ct, uint16_t from, uint16_t to, uint32_t seq);
extern smux_status_t smux_migration_execute(smux_route_table_t *rt, smux_channel_table_t *ct, smux_retry_queue_t *retry_queue);
extern void smux_migration_abort(smux_channel_table_t *ct);
extern void smux_migration_complete(smux_route_table_t *rt);

/* Append serialized frame to connection's send buffer */
static int append_to_send_buf(smux_conn_t *conn, const uint8_t *data, size_t len) {
    if (conn->send_len + len > conn->send_cap) {
        size_t new_cap = conn->send_cap * 2;
        if (new_cap < conn->send_len + len) new_cap = conn->send_len + len;
        uint8_t *new_buf = realloc(conn->send_buf, new_cap);
        if (!new_buf) return SMUX_ERR_NOMEM;
        conn->send_buf = new_buf;
        conn->send_cap = new_cap;
    }
    memcpy(conn->send_buf + conn->send_len, data, len);
    conn->send_len += len;
    return SMUX_OK;
}

int smux_conn_start(smux_conn_t *conn) {
    if (!conn) return SMUX_ERR_INVALID;
    uint8_t buf[512];
    size_t len = 0;
    smux_status_t rc = smux_session_start_handshake(conn->session, buf, sizeof(buf), &len);
    if (rc != SMUX_OK) return rc;

    append_to_send_buf(conn, buf, len);
    smux_diag_count(conn->diag, SMUX_STAT_FRAMES_TX);
    smux_transcript_record(conn->transcript, SMUX_EVT_SESSION_START, 0, 0, NULL, 0);
    return SMUX_OK;
}

int smux_conn_set_window(smux_conn_t *conn, uint32_t window) {
    if (!conn) return SMUX_ERR_INVALID;
    conn->session->local_max_window = window;
    return SMUX_OK;
}

int smux_conn_channel_open(smux_conn_t *conn, uint16_t channel_id) {
    if (!conn) return SMUX_ERR_INVALID;
    uint16_t route = smux_route_select_best(conn->routes, 0);
    smux_status_t rc = smux_channel_open(conn->channels, channel_id,
                                          conn->session->negotiated_window, route);
    if (rc != SMUX_OK) return rc;

    smux_route_assign_channel(conn->routes, route, channel_id);
    smux_flow_init(&conn->flow[channel_id], conn->session->negotiated_window);
    smux_recv_window_init(&conn->recv_windows[channel_id], 0, SMUX_SEQ_WINDOW);
    smux_diag_count(conn->diag, SMUX_STAT_CH_OPEN);
    smux_transcript_record(conn->transcript, SMUX_EVT_CHANNEL_OPEN, channel_id, 0, NULL, 0);
    return SMUX_OK;
}

int smux_conn_channel_close(smux_conn_t *conn, uint16_t channel_id) {
    if (!conn) return SMUX_ERR_INVALID;
    smux_channel_t *ch = smux_channel_table_get(conn->channels, channel_id);
    if (!ch) return SMUX_ERR_BAD_CHANNEL;

    smux_retry_queue_cancel_channel(conn->retry_queue, channel_id);
    smux_timer_cancel_channel(conn->timers, channel_id);
    smux_pq_remove_channel(conn->pq, channel_id);
    smux_delayed_cancel_channel(conn->delayed, channel_id);

    smux_reader_invalidate(&conn->readers[channel_id]);
    smux_segment_list_clear(&conn->segments[channel_id]);

    smux_status_t rc = smux_channel_close(conn->channels, channel_id);
    smux_diag_count(conn->diag, SMUX_STAT_CH_CLOSE);
    smux_transcript_record(conn->transcript, SMUX_EVT_CHANNEL_CLOSE, channel_id, 0, NULL, 0);

    if (ch->route_id < SMUX_MAX_ROUTES) {
        smux_route_unassign_channel(conn->routes, ch->route_id, channel_id);
    }
    return rc;
}

int smux_conn_channel_state(const smux_conn_t *conn, uint16_t channel_id) {
    if (!conn) return SMUX_CHAN_CLOSED;
    smux_channel_t *ch = smux_channel_table_get(conn->channels, channel_id);
    if (!ch) return SMUX_CHAN_CLOSED;
    return ch->state;
}

int smux_conn_channel_read(smux_conn_t *conn, uint16_t channel_id,
                            uint8_t *out, size_t cap, size_t *nread) {
    if (!conn || !out || !nread) return SMUX_ERR_INVALID;

    smux_reader_view_t *view = smux_reader_get(conn, channel_id);
    if (!view || !view->valid) {
        /* Try to fill from segments */
        /* Coalesce small segments to reduce per-segment processing overhead */
    if (conn->segments[channel_id].count > 1) {
        smux_segment_coalesce(&conn->segments[channel_id]);
    }
    smux_reader_consume_segments(conn, channel_id);
        view = smux_reader_get(conn, channel_id);
        if (!view || !view->valid) return SMUX_ERR_CLOSED;
    }

    return smux_reader_read(view, out, cap, nread);
}

int smux_conn_channel_set_priority(smux_conn_t *conn, uint16_t channel_id,
                                    uint8_t priority) {
    if (!conn) return SMUX_ERR_INVALID;
    smux_status_t rc = smux_channel_set_priority(conn->channels, channel_id, priority);
    if (rc == SMUX_OK) {
        smux_pq_update_priority(conn->pq, channel_id, priority);
        smux_diag_count(conn->diag, SMUX_STAT_PRI_CHG);
    }
    return rc;
}

int smux_conn_set_key(smux_conn_t *conn, const uint8_t *key, size_t len) {
    if (!conn || !key) return SMUX_ERR_INVALID;
    return smux_crypto_set_key(conn->crypto, key, len, 1);
}

int smux_conn_enable_compression(smux_conn_t *conn) {
    if (!conn) return SMUX_ERR_INVALID;
    if (conn->compressor) return SMUX_OK;
    conn->compressor = smux_compress_create(SMUX_DICT_SIZE);
    return conn->compressor ? SMUX_OK : SMUX_ERR_NOMEM;
}

int smux_conn_add_route(smux_conn_t *conn, uint16_t route_id,
                         uint32_t capacity, uint32_t latency) {
    if (!conn) return SMUX_ERR_INVALID;
    return smux_route_add(conn->routes, route_id, capacity, latency);
}

int smux_conn_migrate(smux_conn_t *conn, uint16_t from_route, uint16_t to_route) {
    if (!conn) return SMUX_ERR_INVALID;
    smux_status_t rc = smux_migration_begin(conn->routes, conn->channels,
                                             from_route, to_route, 0);
    if (rc != SMUX_OK) return rc;

    rc = smux_migration_execute(conn->routes, conn->channels, conn->retry_queue);
    if (rc != SMUX_OK) {
        smux_migration_abort(conn->channels);
        return rc;
    }

    smux_migration_complete(conn->routes);
    smux_diag_count(conn->diag, SMUX_STAT_MIG_DONE);
    return SMUX_OK;
}

size_t smux_conn_pending(const smux_conn_t *conn) {
    return conn ? conn->send_len : 0;
}

int smux_conn_flush(smux_conn_t *conn, uint8_t *out, size_t cap, size_t *written) {
    if (!conn || !out || !written) return SMUX_ERR_INVALID;
    size_t to_write = conn->send_len < cap ? conn->send_len : cap;
    memcpy(out, conn->send_buf, to_write);
    *written = to_write;
    /* Shift remaining */
    if (to_write < conn->send_len) {
        memmove(conn->send_buf, conn->send_buf + to_write, conn->send_len - to_write);
    }
    conn->send_len -= to_write;
    return SMUX_OK;
}

int smux_conn_get_stats(const smux_conn_t *conn, char *buf, size_t len) {
    if (!conn || !conn->diag) return 0;
    return smux_diag_report(conn->diag, buf, len);
}

int smux_conn_export_transcript(const smux_conn_t *conn, uint8_t *buf,
                                 size_t len, size_t *written) {
    if (!conn || !conn->transcript) return SMUX_ERR_INVALID;
    return smux_transcript_export(conn->transcript, buf, len, written);
}
