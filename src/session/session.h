#ifndef SMUX_SESSION_H
#define SMUX_SESSION_H

#include "../core/types.h"
#include "../core/status.h"
#include "../buffer/buffer.h"

typedef struct {
    smux_session_state_t state;
    uint16_t            local_version_major;
    uint16_t            local_version_minor;
    uint16_t            remote_version_major;
    uint16_t            remote_version_minor;
    uint32_t            local_max_window;
    uint32_t            remote_max_window;
    uint32_t            negotiated_window;
    uint32_t            next_seq;
    uint32_t            remote_seq;
    uint64_t            established_at;
    uint64_t            last_recv;
    uint64_t            last_send;
    uint32_t            keepalive_ms;
    uint32_t            hello_retries;
    bool                compression_enabled;
    bool                encryption_enabled;
    uint8_t             session_id[16];
    smux_pool_t        *pool;
} smux_session_t;

smux_session_t *smux_session_create(smux_pool_t *pool);
void smux_session_destroy(smux_session_t *session);
void smux_session_reset(smux_session_t *session);
smux_status_t smux_session_start_handshake(smux_session_t *session,
                                           uint8_t *out_buf, size_t buf_len,
                                           size_t *out_len);
smux_status_t smux_session_handle_hello(smux_session_t *session,
                                        const smux_frame_t *frame,
                                        uint8_t *out_buf, size_t buf_len,
                                        size_t *out_len);
smux_status_t smux_session_handle_hello_ack(smux_session_t *session,
                                            const smux_frame_t *frame);
smux_status_t smux_session_handle_ping(smux_session_t *session,
                                       const smux_frame_t *frame,
                                       uint8_t *out_buf, size_t buf_len,
                                       size_t *out_len);
uint32_t smux_session_next_seq(smux_session_t *session);
int smux_session_is_established(const smux_session_t *session);
int smux_session_check_keepalive(smux_session_t *session, uint64_t now_ms);
smux_status_t smux_session_negotiate_features(smux_session_t *session,
                                               const smux_frame_t *frame);

#endif
