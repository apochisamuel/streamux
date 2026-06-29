#include "api.h"
#include "../frame/frame.h"
#include "../core/byte_utils.h"
/* Frame encoding helpers are in cli/cli_encode */
#include <stdlib.h>
#include <string.h>

void smux_config_default(smux_config_t *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->max_channels = SMUX_MAX_CHANNELS;
    cfg->default_window = SMUX_DEFAULT_WINDOW;
    cfg->keepalive_ms = 30000;
    cfg->enable_compression = false;
    cfg->enable_encryption = false;
    cfg->dict_size = SMUX_DICT_SIZE;
    cfg->max_routes = SMUX_MAX_ROUTES;
    cfg->transcript_capacity = SMUX_TRANSCRIPT_CAP;
}

smux_managed_t *smux_managed_create(const smux_config_t *cfg) {
    smux_managed_t *m = calloc(1, sizeof(smux_managed_t));
    if (!m) return NULL;

    if (cfg) {
        m->config = *cfg;
    } else {
        smux_config_default(&m->config);
    }

    m->conn = smux_conn_create();
    if (!m->conn) {
        free(m);
        return NULL;
    }

    /* Apply config */
    smux_conn_set_window(m->conn, m->config.default_window);
    m->conn->session->keepalive_ms = m->config.keepalive_ms;

    if (m->config.enable_compression) {
        smux_conn_enable_compression(m->conn);
    }

    if (m->config.log_fn) {
        m->conn->log_fn = m->config.log_fn;
        m->conn->log_ctx = m->config.log_ctx;
    }

    return m;
}

void smux_managed_destroy(smux_managed_t *m) {
    if (!m) return;
    smux_conn_destroy(m->conn);
    free(m);
}

int smux_managed_connect(smux_managed_t *m) {
    if (!m) return SMUX_ERR_INVALID;
    int rc = smux_conn_start(m->conn);
    if (rc == SMUX_OK) {
        m->connect_time = m->conn->clock_ms;
    }
    return rc;
}

int smux_managed_feed(smux_managed_t *m, const uint8_t *data, size_t len) {
    if (!m) return SMUX_ERR_INVALID;
    int frames = smux_conn_feed(m->conn, data, len);
    m->last_activity = m->conn->clock_ms;

    /* Check if handshake completed */
    if (!m->handshake_done && smux_session_is_established(m->conn->session)) {
        m->handshake_done = true;
    }
    return frames;
}

int smux_managed_tick(smux_managed_t *m, uint64_t now_ms) {
    if (!m) return SMUX_ERR_INVALID;
    smux_conn_set_clock(m->conn, now_ms);

    /* Advance timers */
    smux_timer_advance(m->conn->timers, now_ms);

    /* Process delayed entries */
    smux_delayed_entry_t delayed[8];
    int count = smux_delayed_process(m->conn->delayed, now_ms, delayed, 8);
    for (int i = 0; i < count; i++) {
        smux_segment_t *seg = smux_segment_create(delayed[i].data, delayed[i].data_len,
                                                    delayed[i].channel_id, delayed[i].seq);
        if (seg) smux_segment_list_push(&m->conn->segments[delayed[i].channel_id], seg);
    }

    return count;
}

int smux_managed_channel_open(smux_managed_t *m, uint16_t channel_id) {
    if (!m) return SMUX_ERR_INVALID;
    int rc = smux_conn_channel_open(m->conn, channel_id);
    if (rc == SMUX_OK) m->open_channels++;
    return rc;
}

int smux_managed_channel_close(smux_managed_t *m, uint16_t channel_id) {
    if (!m) return SMUX_ERR_INVALID;
    int rc = smux_conn_channel_close(m->conn, channel_id);
    if (rc == SMUX_OK && m->open_channels > 0) m->open_channels--;
    return rc;
}

int smux_managed_channel_read(smux_managed_t *m, uint16_t channel_id,
                               uint8_t *buf, size_t cap, size_t *nread) {
    if (!m) return SMUX_ERR_INVALID;
    return smux_conn_channel_read(m->conn, channel_id, buf, cap, nread);
}

int smux_managed_flush(smux_managed_t *m, uint8_t *out, size_t cap, size_t *written) {
    if (!m) return SMUX_ERR_INVALID;
    return smux_conn_flush(m->conn, out, cap, written);
}

int smux_managed_is_connected(const smux_managed_t *m) {
    return m && m->handshake_done && smux_conn_state(m->conn) == SMUX_SESS_ESTABLISHED;
}

int smux_managed_channel_count(const smux_managed_t *m) {
    return m ? (int)m->open_channels : 0;
}
