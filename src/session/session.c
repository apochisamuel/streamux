#include "session.h"
#include "../frame/frame.h"
#include "../core/byte_utils.h"
#include <stdlib.h>
#include <string.h>

smux_session_t *smux_session_create(smux_pool_t *pool) {
    smux_session_t *s = calloc(1, sizeof(smux_session_t));
    if (!s) return NULL;
    s->state = SMUX_SESS_IDLE;
    s->local_version_major = SMUX_VERSION_MAJOR;
    s->local_version_minor = SMUX_VERSION_MINOR;
    s->local_max_window = SMUX_DEFAULT_WINDOW;
    s->keepalive_ms = SMUX_KEEPALIVE_MS;
    s->pool = pool;
    /* Generate a simple session ID from stack entropy */
    uint32_t h = smux_hash_bytes((uint8_t *)&s, sizeof(s));
    smux_write_u32_be(s->session_id, h);
    smux_write_u32_be(s->session_id + 4, h ^ 0xDEADBEEF);
    smux_write_u32_be(s->session_id + 8, h ^ 0xCAFEBABE);
    smux_write_u32_be(s->session_id + 12, h ^ 0x01020304);
    return s;
}

void smux_session_destroy(smux_session_t *session) {
    if (!session) return;
    smux_mem_zero(session->session_id, 16);
    free(session);
}

void smux_session_reset(smux_session_t *session) {
    if (!session) return;
    smux_pool_t *pool = session->pool;
    smux_mem_zero(session, sizeof(*session));
    session->state = SMUX_SESS_IDLE;
    session->local_version_major = SMUX_VERSION_MAJOR;
    session->local_version_minor = SMUX_VERSION_MINOR;
    session->local_max_window = SMUX_DEFAULT_WINDOW;
    session->keepalive_ms = SMUX_KEEPALIVE_MS;
    session->pool = pool;
}

smux_status_t smux_session_start_handshake(smux_session_t *session,
                                           uint8_t *out_buf, size_t buf_len,
                                           size_t *out_len) {
    if (!session || !out_buf || !out_len) return SMUX_ERR_INVALID;
    if (session->state != SMUX_SESS_IDLE) return SMUX_ERR_BAD_STATE;

    smux_frame_t hello;
    smux_frame_init(&hello, SMUX_FRAME_HELLO, 0);

    /* HELLO payload: version_major(2) + version_minor(2) + max_window(4) + session_id(16) */
    uint8_t payload[24];
    smux_write_u16_be(payload, session->local_version_major);
    smux_write_u16_be(payload + 2, session->local_version_minor);
    smux_write_u32_be(payload + 4, session->local_max_window);
    memcpy(payload + 8, session->session_id, 16);
    hello.payload = payload;
    hello.payload_len = 24;
    hello.sequence = 0;

    int written = smux_frame_serialize(&hello, out_buf, buf_len);
    if (written < 0) return (smux_status_t)written;

    session->state = SMUX_SESS_HANDSHAKE;
    session->hello_retries = 0;
    *out_len = (size_t)written;
    return SMUX_OK;
}

smux_status_t smux_session_handle_hello(smux_session_t *session,
                                        const smux_frame_t *frame,
                                        uint8_t *out_buf, size_t buf_len,
                                        size_t *out_len) {
    if (!session || !frame || !out_buf || !out_len) return SMUX_ERR_INVALID;

    /* Accept HELLO in IDLE or HANDSHAKE (simultaneous open) */
    if (session->state != SMUX_SESS_IDLE &&
        session->state != SMUX_SESS_HANDSHAKE &&
        session->state != SMUX_SESS_ESTABLISHED) /* allow re-negotiation */
        return SMUX_ERR_BAD_STATE;

    if (frame->payload_len < 8) return SMUX_ERR_BAD_LENGTH;

    session->remote_version_major = smux_read_u16_be(frame->payload);
    session->remote_version_minor = smux_read_u16_be(frame->payload + 2);
    session->remote_max_window = smux_read_u32_be(frame->payload + 4);

    /* Version compatibility */
    if (session->remote_version_major != session->local_version_major)
        return SMUX_ERR_BAD_VERSION;

    /* Negotiate window to minimum of both */
    session->negotiated_window = smux_min_u32(session->local_max_window,
                                              session->remote_max_window);
    if (session->negotiated_window < 512)
        session->negotiated_window = 512;

    /* Feature negotiation from extensions */
    smux_session_negotiate_features(session, frame);

    /* Build HELLO_ACK */
    smux_frame_t ack;
    smux_frame_init(&ack, SMUX_FRAME_HELLO_ACK, 0);

    uint8_t payload[24];
    smux_write_u16_be(payload, session->local_version_major);
    smux_write_u16_be(payload + 2, session->local_version_minor);
    smux_write_u32_be(payload + 4, session->negotiated_window);
    memcpy(payload + 8, session->session_id, 16);
    ack.payload = payload;
    ack.payload_len = 24;
    ack.sequence = smux_session_next_seq(session);

    int written = smux_frame_serialize(&ack, out_buf, buf_len);
    if (written < 0) return (smux_status_t)written;

    session->state = SMUX_SESS_ESTABLISHED;
    *out_len = (size_t)written;
    return SMUX_OK;
}

smux_status_t smux_session_handle_hello_ack(smux_session_t *session,
                                            const smux_frame_t *frame) {
    if (!session || !frame) return SMUX_ERR_INVALID;
    if (session->state != SMUX_SESS_HANDSHAKE) return SMUX_ERR_BAD_STATE;
    if (frame->payload_len < 8) return SMUX_ERR_BAD_LENGTH;

    session->remote_version_major = smux_read_u16_be(frame->payload);
    session->remote_version_minor = smux_read_u16_be(frame->payload + 2);
    session->negotiated_window = smux_read_u32_be(frame->payload + 4);

    if (session->remote_version_major != session->local_version_major)
        return SMUX_ERR_BAD_VERSION;

    smux_session_negotiate_features(session, frame);
    session->state = SMUX_SESS_ESTABLISHED;
    return SMUX_OK;
}

smux_status_t smux_session_handle_ping(smux_session_t *session,
                                       const smux_frame_t *frame,
                                       uint8_t *out_buf, size_t buf_len,
                                       size_t *out_len) {
    if (!session || !frame || !out_buf || !out_len) return SMUX_ERR_INVALID;
    if (session->state != SMUX_SESS_ESTABLISHED) return SMUX_ERR_BAD_STATE;

    smux_frame_t pong;
    smux_frame_init(&pong, SMUX_FRAME_PONG, 0);
    pong.sequence = frame->sequence;
    /* Echo payload for RTT measurement */
    pong.payload = frame->payload;
    pong.payload_len = frame->payload_len;

    int written = smux_frame_serialize(&pong, out_buf, buf_len);
    if (written < 0) return (smux_status_t)written;
    *out_len = (size_t)written;
    session->last_recv = 0; /* Would be set by caller with real timestamp */
    return SMUX_OK;
}

uint32_t smux_session_next_seq(smux_session_t *session) {
    return session->next_seq++;
}

int smux_session_is_established(const smux_session_t *session) {
    return session && session->state == SMUX_SESS_ESTABLISHED;
}

int smux_session_check_keepalive(smux_session_t *session, uint64_t now_ms) {
    if (!session) return 0;
    if (session->state != SMUX_SESS_ESTABLISHED) return 0;
    if (session->keepalive_ms == 0) return 0;
    uint64_t elapsed = now_ms - session->last_recv;
    return elapsed > session->keepalive_ms;
}

smux_status_t smux_session_negotiate_features(smux_session_t *session,
                                               const smux_frame_t *frame) {
    if (!session || !frame) return SMUX_ERR_INVALID;

    /* Check for compression support */
    const smux_extension_t *comp = smux_frame_find_extension(frame, SMUX_EXT_COMPRESS);
    if (comp && comp->ext_len >= 2) {
        uint16_t algo = smux_read_u16_be(comp->ext_data);
        if (algo == 1) /* simple LZ compression */
            session->compression_enabled = true;
    }

    /* Check for encryption support */
    const smux_extension_t *key = smux_frame_find_extension(frame, SMUX_EXT_KEY_ID);
    if (key && key->ext_len >= 4) {
        session->encryption_enabled = true;
    }

    return SMUX_OK;
}
