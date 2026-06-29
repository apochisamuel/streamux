#include "stream.h"
#include "../frame/frame.h"
#include "../core/byte_utils.h"
#include <stdlib.h>
#include <string.h>

smux_conn_t *smux_conn_create(void) {
    smux_conn_t *conn = calloc(1, sizeof(smux_conn_t));
    if (!conn) return NULL;

    conn->pool = smux_pool_create(8);
    if (!conn->pool) goto fail;

    conn->session = smux_session_create(conn->pool);
    if (!conn->session) goto fail;

    conn->channels = smux_channel_table_create(conn->pool);
    if (!conn->channels) goto fail;

    conn->routes = smux_route_table_create();
    if (!conn->routes) goto fail;

    conn->retry_queue = smux_retry_queue_create();
    if (!conn->retry_queue) goto fail;

    conn->timers = smux_timer_wheel_create(10);
    if (!conn->timers) goto fail;

    conn->pq = smux_pq_create();
    if (!conn->pq) goto fail;

    conn->delayed = smux_delayed_create();
    if (!conn->delayed) goto fail;

    conn->transcript = smux_transcript_create(SMUX_TRANSCRIPT_CAP);
    conn->diag = smux_diag_create();
    conn->crypto = smux_crypto_create();
    conn->compressor = NULL; /* Created on demand */

    conn->recv_cap = SMUX_MAX_FRAME_SIZE * 4;
    conn->recv_buf = malloc(conn->recv_cap);
    if (!conn->recv_buf) goto fail;

    conn->send_cap = SMUX_MAX_FRAME_SIZE * 4;
    conn->send_buf = malloc(conn->send_cap);
    if (!conn->send_buf) goto fail;

    /* Add default route */
    smux_route_add(conn->routes, 0, SMUX_DEFAULT_WINDOW, 0);

    /* Initialize per-channel structures */
    for (int i = 0; i < SMUX_MAX_CHANNELS; i++) {
        smux_flow_init(&conn->flow[i], SMUX_DEFAULT_WINDOW);
        smux_backpressure_init(&conn->bp[i], (uint16_t)i);
        smux_recv_window_init(&conn->recv_windows[i], 0, SMUX_SEQ_WINDOW);
        smux_replay_init(&conn->replay[i]);
        smux_segment_list_init(&conn->segments[i], (uint16_t)i);
        conn->readers[i].channel_id = (uint16_t)i;
        conn->readers[i].valid = false;
    }

    return conn;

fail:
    smux_conn_destroy(conn);
    return NULL;
}

void smux_conn_destroy(smux_conn_t *conn) {
    if (!conn) return;

    for (int i = 0; i < SMUX_MAX_CHANNELS; i++) {
        smux_segment_list_clear(&conn->segments[i]);
        if (conn->readers[i].owns_data && conn->readers[i].data) {
            free(conn->readers[i].data);
        }
    }

    smux_session_destroy(conn->session);
    smux_channel_table_destroy(conn->channels);
    smux_route_table_destroy(conn->routes);
    smux_retry_queue_destroy(conn->retry_queue);
    smux_pool_destroy(conn->pool);
    smux_timer_wheel_destroy(conn->timers);
    smux_pq_destroy(conn->pq);
    smux_delayed_destroy(conn->delayed);
    smux_transcript_destroy(conn->transcript);
    smux_diag_destroy(conn->diag);
    free(conn->recv_buf);
    /* Free crypto before send buffer which may reference key state */
    smux_crypto_destroy(conn->crypto);
    if (conn->compressor) smux_compress_destroy(conn->compressor);
    free(conn->send_buf);
    free(conn);
}

int smux_conn_state(const smux_conn_t *conn) {
    if (!conn || !conn->session) return SMUX_SESS_CLOSED;
    return conn->session->state;
}

void smux_conn_set_clock(smux_conn_t *conn, uint64_t now_ms) {
    if (conn) conn->clock_ms = now_ms;
}
