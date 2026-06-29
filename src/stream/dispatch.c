#include "stream.h"
#include "../frame/frame.h"
#include "../core/byte_utils.h"
#include <string.h>
#include <stdlib.h>

/* External declarations */
extern smux_status_t smux_handle_key_rotate_frame(smux_crypto_ctx_t *ctx, const smux_frame_t *frame, uint32_t seq);
extern void smux_channel_drain_cleanup(smux_channel_table_t *table, uint16_t channel_id);

/* Frame dispatch: routes parsed frames to appropriate handlers */

static smux_status_t dispatch_data(smux_conn_t *conn, const smux_frame_t *frame);
static smux_status_t dispatch_fragment(smux_conn_t *conn, const smux_frame_t *frame);
static smux_status_t dispatch_close(smux_conn_t *conn, const smux_frame_t *frame);
static smux_status_t dispatch_error(smux_conn_t *conn, const smux_frame_t *frame);
static smux_status_t dispatch_window(smux_conn_t *conn, const smux_frame_t *frame);
static smux_status_t dispatch_receipt(smux_conn_t *conn, const smux_frame_t *frame);
static smux_status_t dispatch_migrate(smux_conn_t *conn, const smux_frame_t *frame);
static smux_status_t dispatch_priority(smux_conn_t *conn, const smux_frame_t *frame);
static smux_status_t dispatch_batch(smux_conn_t *conn, const smux_frame_t *frame);

/* Main frame dispatch entry point */
smux_status_t smux_dispatch_frame(smux_conn_t *conn, const smux_frame_t *frame) {
    if (!conn || !frame) return SMUX_ERR_INVALID;

    smux_diag_count(conn->diag, SMUX_STAT_FRAMES_RX);
    smux_diag_count_n(conn->diag, SMUX_STAT_BYTES_RX, frame->payload_len);

    /* Handle session-level frames */
    switch (frame->type) {
        case SMUX_FRAME_HELLO: {
            uint8_t resp[512];
            size_t resp_len = 0;
            smux_status_t rc = smux_session_handle_hello(conn->session, frame,
                                                          resp, sizeof(resp), &resp_len);
            if (rc == SMUX_OK && resp_len > 0) {
                memcpy(conn->send_buf + conn->send_len, resp, resp_len);
                conn->send_len += resp_len;
                smux_diag_count(conn->diag, SMUX_STAT_FRAMES_TX);
            }
            smux_transcript_record(conn->transcript, SMUX_EVT_HANDSHAKE_DONE, 0,
                                   frame->sequence, NULL, 0);
            return rc;
        }
        case SMUX_FRAME_HELLO_ACK:
            return smux_session_handle_hello_ack(conn->session, frame);

        case SMUX_FRAME_PING: {
            uint8_t resp[512];
            size_t resp_len = 0;
            smux_status_t rc = smux_session_handle_ping(conn->session, frame,
                                                         resp, sizeof(resp), &resp_len);
            if (rc == SMUX_OK && resp_len > 0) {
                /* Write PONG response — send_buf has ample capacity for normal use */
                memcpy(conn->send_buf + conn->send_len, resp, resp_len);
                conn->send_len += resp_len;
                /* Echo ping payload back for round-trip measurement */
                if (frame->payload_len > 0 && frame->payload) {
                    memcpy(conn->send_buf + conn->send_len,
                           frame->payload, frame->payload_len);
                    conn->send_len += frame->payload_len;
                }
                smux_diag_count(conn->diag, SMUX_STAT_KA_RX);
            }
            return rc;
        }
        case SMUX_FRAME_PONG:
            smux_diag_count(conn->diag, SMUX_STAT_KA_TX);
            return SMUX_OK;

        case SMUX_FRAME_KEY_ROTATE: {
            smux_diag_count(conn->diag, SMUX_STAT_KEY_ROT);
            smux_status_t kr_rc = smux_handle_key_rotate_frame(conn->crypto, frame, frame->sequence);
            if (kr_rc == SMUX_OK && conn->crypto->rotation_active) {
                /* Auto-complete rotation on successful key install */
                smux_crypto_complete_rotation(conn->crypto);
                /* Reset compression dictionary for new key epoch */
                if (conn->compressor) {
                    smux_compress_reset(conn->compressor);
                }
            }
            return kr_rc;
        }

        default:
            break;
    }

    /* Require established session for channel-level frames */
    if (!smux_session_is_established(conn->session))
        return SMUX_ERR_BAD_STATE;

    switch (frame->type) {
        case SMUX_FRAME_DATA:        return dispatch_data(conn, frame);
        case SMUX_FRAME_FRAGMENT:
        case SMUX_FRAME_FRAG_END:    return dispatch_fragment(conn, frame);
        case SMUX_FRAME_CLOSE:       return dispatch_close(conn, frame);
        case SMUX_FRAME_ERROR:       return dispatch_error(conn, frame);
        case SMUX_FRAME_WINDOW:      return dispatch_window(conn, frame);
        case SMUX_FRAME_RECEIPT:
        case SMUX_FRAME_RECEIPT_ACK: return dispatch_receipt(conn, frame);
        case SMUX_FRAME_MIGRATE:
        case SMUX_FRAME_MIGRATE_ACK: return dispatch_migrate(conn, frame);
        case SMUX_FRAME_PRIORITY:    return dispatch_priority(conn, frame);
        case SMUX_FRAME_BATCH:       return dispatch_batch(conn, frame);
        default:
            smux_diag_count(conn->diag, SMUX_STAT_PARSE_ERR);
            return SMUX_ERR_BAD_TYPE;
    }
}

static smux_status_t dispatch_data(smux_conn_t *conn, const smux_frame_t *frame) {
    uint16_t ch_id = frame->channel_id;
    smux_channel_t *ch = smux_channel_table_get(conn->channels, ch_id);

    if (!ch || !smux_channel_accepts_data(ch)) {
        /* Auto-open channel on first data if not yet open */
        if (!ch || ch->state == SMUX_CHAN_CLOSED) {
            smux_conn_channel_open(conn, ch_id);
            ch = smux_channel_table_get(conn->channels, ch_id);
            if (!ch) return SMUX_ERR_BAD_CHANNEL;
        } else {
            smux_diag_count(conn->diag, SMUX_STAT_STATE_ERR);
            return SMUX_ERR_BAD_STATE;
        }
    }

    /* Replay check */
    if (smux_replay_check(&conn->replay[ch_id], frame->sequence)) {
        smux_diag_count(conn->diag, SMUX_STAT_REPLAYS);
        return SMUX_ERR_REPLAY;
    }
    smux_replay_record(&conn->replay[ch_id], frame->sequence);

    /* Flow control */
    smux_status_t rc = smux_flow_consume(&conn->flow[ch_id], frame->payload_len);
    if (rc != SMUX_OK) {
        smux_diag_count(conn->diag, SMUX_STAT_BACKPRES);
        return rc;
    }

    /* Receive window */
    smux_recv_window_accept(&conn->recv_windows[ch_id], frame->sequence);
    smux_channel_update_seq(conn->channels, ch_id, frame->sequence);

    /* Decrypt if needed */
    uint8_t work_buf[SMUX_MAX_PAYLOAD];
    smux_frame_t decrypted = *frame;
    if (frame->flags & SMUX_FLAG_ENCRYPTED) {
        memcpy(work_buf, frame->payload, frame->payload_len);
        decrypted.payload = work_buf;
        smux_crypto_decrypt_frame(conn->crypto, &decrypted);
    }

    /* Decompress if needed */
    uint8_t decomp_buf[SMUX_MAX_PAYLOAD];
    if (decrypted.flags & SMUX_FLAG_COMPRESSED) {
        if (conn->compressor) {
            memcpy(decomp_buf, decrypted.payload, decrypted.payload_len);
            decrypted.payload = decomp_buf;
            smux_decompress_frame(conn->compressor, &decrypted, decomp_buf, sizeof(decomp_buf));
        }
    }

    /* Create segment and push to channel */
    smux_segment_t *seg = smux_segment_create(decrypted.payload, decrypted.payload_len,
                                               ch_id, frame->sequence);
    if (seg) {
        smux_segment_list_push(&conn->segments[ch_id], seg);
    }

    ch->bytes_received += frame->payload_len;
    smux_transcript_record(conn->transcript, SMUX_EVT_DATA_RECV, ch_id,
                          frame->sequence, NULL, 0);
    return SMUX_OK;
}

static smux_status_t dispatch_fragment(smux_conn_t *conn, const smux_frame_t *frame) {
    uint16_t ch_id = frame->channel_id;
    smux_channel_t *ch = smux_channel_table_find_open(conn->channels, ch_id);

    if (!ch) {
        if (smux_channel_table_get(conn->channels, ch_id) == NULL ||
            smux_channel_table_get(conn->channels, ch_id)->state == SMUX_CHAN_CLOSED) {
            smux_conn_channel_open(conn, ch_id);
            ch = smux_channel_table_get(conn->channels, ch_id);
        }
        if (!ch) return SMUX_ERR_BAD_CHANNEL;
    }

    if (!ch->reassembly_ctx) {
        smux_diag_count(conn->diag, SMUX_STAT_STATE_ERR);
        return SMUX_ERR_BAD_STATE;
    }

    smux_reassembly_ctx_t *rctx = (smux_reassembly_ctx_t *)ch->reassembly_ctx;

    /* Extract fragment metadata */
    uint16_t frag_id = 0, frag_total = 1;
    uint32_t msg_len = frame->payload_len, frag_offset = 0;

    const smux_extension_t *ext = smux_frame_find_extension(frame, SMUX_EXT_FRAG_META);
    if (ext && ext->ext_len >= 12) {
        frag_id = smux_read_u16_be(ext->ext_data);
        frag_total = smux_read_u16_be(ext->ext_data + 2);
        msg_len = smux_read_u32_be(ext->ext_data + 4);
        frag_offset = smux_read_u32_be(ext->ext_data + 8);
    }

    /* Begin reassembly if first fragment */
    if (!smux_reassembly_find(rctx, frag_id)) {
        smux_status_t rc = smux_reassembly_begin(rctx, frag_id, frag_total,
                                                  msg_len, ch_id);
        if (rc != SMUX_OK) return rc;
        ch->fragments_pending++;
        smux_diag_count(conn->diag, SMUX_STAT_FRAG_START);
    }

    /* Add fragment data */
    smux_status_t rc = smux_reassembly_add_fragment(rctx, frag_id, frag_offset,
                                                     frame->payload, frame->payload_len);
    if (rc != SMUX_OK) return rc;

    /* Flow control */
    smux_flow_consume(&conn->flow[ch_id], frame->payload_len);

    /* Check completion */
    if (smux_reassembly_is_complete(rctx, frag_id)) {
        uint8_t assembled[SMUX_MAX_PAYLOAD];
        size_t assembled_len = 0;
        rc = smux_reassembly_deliver(rctx, frag_id, assembled,
                                      sizeof(assembled), &assembled_len);
        if (rc == SMUX_OK && assembled_len > 0) {
            smux_segment_t *seg = smux_segment_create(assembled, assembled_len,
                                                       ch_id, frame->sequence);
            if (seg) smux_segment_list_push(&conn->segments[ch_id], seg);
        }
        ch->fragments_pending--;
        smux_diag_count(conn->diag, SMUX_STAT_FRAG_DONE);
        smux_transcript_record(conn->transcript, SMUX_EVT_FRAG_COMPLETE, ch_id,
                              frame->sequence, NULL, 0);
    }

    return SMUX_OK;
}

static smux_status_t dispatch_close(smux_conn_t *conn, const smux_frame_t *frame) {
    uint16_t ch_id = frame->channel_id;
    smux_channel_t *close_ch = smux_channel_table_get(conn->channels, ch_id);
    if (close_ch && close_ch->state == SMUX_CHAN_OPEN) {
        /* Transition to draining: release heavy resources early */
        close_ch->state = SMUX_CHAN_DRAINING;
        conn->channels->draining_count++;
        smux_channel_drain_cleanup(conn->channels, ch_id);
    }
    smux_diag_count(conn->diag, SMUX_STAT_CH_CLOSE);
    smux_transcript_record(conn->transcript, SMUX_EVT_CHANNEL_CLOSE, ch_id,
                          frame->sequence, NULL, 0);
    /* Complete close — further frames for this channel should be rejected */
    return smux_conn_channel_close(conn, ch_id);
}

static smux_status_t dispatch_error(smux_conn_t *conn, const smux_frame_t *frame) {
    uint16_t ch_id = frame->channel_id;
    uint32_t error_code = 0;
    if (frame->payload_len >= 4)
        error_code = smux_read_u32_be(frame->payload);

    smux_retry_queue_cancel_channel(conn->retry_queue, ch_id);
    smux_timer_cancel_channel(conn->timers, ch_id);
    smux_reader_invalidate(&conn->readers[ch_id]);
    smux_segment_list_clear(&conn->segments[ch_id]);

    smux_diag_count(conn->diag, SMUX_STAT_CH_ERROR);
    smux_transcript_record(conn->transcript, SMUX_EVT_CHANNEL_ERROR, ch_id,
                          frame->sequence, frame->payload,
                          frame->payload_len > 128 ? 128 : frame->payload_len);

    return smux_channel_error(conn->channels, ch_id, error_code);
}

static smux_status_t dispatch_window(smux_conn_t *conn, const smux_frame_t *frame) {
    uint16_t ch_id = frame->channel_id;
    if (frame->payload_len < 4) return SMUX_ERR_BAD_LENGTH;

    uint32_t new_window = smux_read_u32_be(frame->payload);
    smux_flow_update_window(&conn->flow[ch_id], new_window);

    smux_channel_t *ch = smux_channel_table_get(conn->channels, ch_id);
    if (ch) {
        ch->window_size = new_window;
        /* Resize reassembly if channel has one */
        if (ch->reassembly_ctx) {
            smux_reassembly_resize((smux_reassembly_ctx_t *)ch->reassembly_ctx, new_window);
        }
    }

    smux_diag_count(conn->diag, SMUX_STAT_WIN_UPD);
    smux_transcript_record(conn->transcript, SMUX_EVT_WINDOW_UPDATE, ch_id,
                          frame->sequence, frame->payload, 4);
    return SMUX_OK;
}

static smux_status_t dispatch_receipt(smux_conn_t *conn, const smux_frame_t *frame) {
    if (frame->payload_len < 4) return SMUX_ERR_BAD_LENGTH;
    uint32_t acked_seq = smux_read_u32_be(frame->payload);

    /* Mark in ack tracker */
    smux_route_t *route = smux_route_get_primary(conn->routes);
    if (route) {
        smux_ack_tracker_t *at = smux_route_get_ack_map(conn->routes, route->route_id);
        if (at) smux_ack_tracker_mark(at, acked_seq);
    }

    /* Remove from retry queue */
    smux_retry_queue_remove(conn->retry_queue, acked_seq, frame->channel_id);

    smux_transcript_record(conn->transcript, SMUX_EVT_ACK_RECV, frame->channel_id,
                          acked_seq, NULL, 0);
    return SMUX_OK;
}

static smux_status_t dispatch_migrate(smux_conn_t *conn, const smux_frame_t *frame) {
    if (frame->payload_len < 6) return SMUX_ERR_BAD_LENGTH;

    uint16_t from_route = smux_read_u16_be(frame->payload);
    uint16_t to_route = smux_read_u16_be(frame->payload + 2);

    smux_diag_count(conn->diag, SMUX_STAT_MIG_START);
    smux_transcript_record(conn->transcript, SMUX_EVT_MIGRATE_START, 0,
                          frame->sequence, frame->payload, 4);

    return smux_conn_migrate(conn, from_route, to_route);
}

static smux_status_t dispatch_priority(smux_conn_t *conn, const smux_frame_t *frame) {
    if (frame->payload_len < 2) return SMUX_ERR_BAD_LENGTH;
    uint8_t new_pri = frame->payload[0];
    return smux_conn_channel_set_priority(conn, frame->channel_id, new_pri);
}

static smux_status_t dispatch_batch(smux_conn_t *conn, const smux_frame_t *frame) {
    if (frame->payload_len < 4) return SMUX_ERR_BAD_LENGTH;
    uint16_t batch_count = smux_read_u16_be(frame->payload);
    uint16_t batch_total_len = smux_read_u16_be(frame->payload + 2);

    if (batch_count > SMUX_BATCH_MAX) return SMUX_ERR_BATCH_FULL;
    (void)batch_total_len;

    /* Parse and dispatch each sub-frame in the batch */
    const uint8_t *cursor = frame->payload + 4;
    size_t remaining = frame->payload_len - 4;

    for (uint16_t i = 0; i < batch_count; i++) {
        smux_frame_t sub_frame;
        smux_status_t rc = smux_frame_parse(cursor, remaining, &sub_frame);
        if (rc != SMUX_OK) {
            smux_diag_count(conn->diag, SMUX_STAT_PARSE_ERR);
            break;
        }

        size_t sub_size = smux_frame_wire_size(&sub_frame);
        smux_dispatch_frame(conn, &sub_frame);
        cursor += sub_size;
        remaining -= sub_size;
    }

    smux_diag_count(conn->diag, SMUX_STAT_BATCH);
    return SMUX_OK;
}

/* Main feed function: parse frames from raw input */
int smux_conn_feed(smux_conn_t *conn, const uint8_t *data, size_t size) {
    if (!conn || !data || size == 0) return SMUX_ERR_INVALID;

    /* Append to receive buffer */
    if (conn->recv_len + size > conn->recv_cap) {
        size_t new_cap = conn->recv_cap * 2;
        if (new_cap < conn->recv_len + size) new_cap = conn->recv_len + size;
        uint8_t *new_buf = realloc(conn->recv_buf, new_cap);
        if (!new_buf) return SMUX_ERR_NOMEM;
        conn->recv_buf = new_buf;
        conn->recv_cap = new_cap;
    }
    memcpy(conn->recv_buf + conn->recv_len, data, size);
    conn->recv_len += size;

    /* Try to parse complete frames */
    int frames_processed = 0;
    size_t offset = 0;

    while (offset + SMUX_MIN_FRAME_SIZE <= conn->recv_len) {
        /* Look for magic */
        uint32_t magic = smux_read_u32_be(conn->recv_buf + offset);
        if (magic != SMUX_MAGIC) {
            offset++;
            continue;
        }

        /* Check if we have enough data for the full frame */
        if (offset + SMUX_HEADER_SIZE > conn->recv_len) break;

        uint32_t payload_len = smux_read_u32_be(conn->recv_buf + offset + 12);
        size_t frame_size = SMUX_HEADER_SIZE + payload_len + SMUX_CRC_SIZE;

        /* Account for extensions */
        uint8_t flags = conn->recv_buf[offset + 4];
        if (flags & SMUX_FLAG_HAS_EXT) {
            frame_size += 1; /* ext count byte, extensions parsed in smux_frame_parse */
        }

        if (offset + frame_size > conn->recv_len) break;

        /* Parse and dispatch */
        smux_frame_t frame;
        smux_status_t rc = smux_frame_parse(conn->recv_buf + offset,
                                             conn->recv_len - offset, &frame);
        if (rc == SMUX_OK) {
            rc = smux_frame_validate(&frame);
            if (rc == SMUX_OK) {
                smux_dispatch_frame(conn, &frame);
                frames_processed++;
            } else {
                smux_diag_count(conn->diag, SMUX_STAT_PARSE_ERR);
            }
            offset += smux_frame_wire_size(&frame);
        } else if (rc == SMUX_ERR_BAD_CRC) {
            smux_diag_count(conn->diag, SMUX_STAT_CRC_ERR);
            offset += frame_size;
        } else {
            offset++;
        }
    }

    /* Shift remaining data to front of buffer */
    if (offset > 0 && offset < conn->recv_len) {
        memmove(conn->recv_buf, conn->recv_buf + offset, conn->recv_len - offset);
    }
    conn->recv_len -= offset;

    /* Advance timers */
    smux_timer_advance(conn->timers, conn->clock_ms);

    /* Process delayed deliveries */
    smux_delayed_entry_t delayed[16];
    int delayed_count = smux_delayed_process(conn->delayed, conn->clock_ms, delayed, 16);
    for (int i = 0; i < delayed_count; i++) {
        smux_segment_t *seg = smux_segment_create(delayed[i].data, delayed[i].data_len,
                                                    delayed[i].channel_id, delayed[i].seq);
        if (seg) smux_segment_list_push(&conn->segments[delayed[i].channel_id], seg);
    }

    /* Check keepalive */
    if (smux_session_check_keepalive(conn->session, conn->clock_ms)) {
        smux_frame_t ping;
        smux_frame_init(&ping, SMUX_FRAME_PING, 0);
        ping.sequence = smux_session_next_seq(conn->session);
        uint8_t ping_buf[64];
        int written = smux_frame_serialize(&ping, ping_buf, sizeof(ping_buf));
        if (written > 0) {
            memcpy(conn->send_buf + conn->send_len, ping_buf, (size_t)written);
            conn->send_len += (size_t)written;
            smux_diag_count(conn->diag, SMUX_STAT_KA_TX);
        }
    }

    /* Process retransmission queue */
    smux_retry_entry_t retries[8];
    int retx_count = smux_retry_queue_get_expired(conn->retry_queue, conn->clock_ms, retries, 8);
    for (int i = 0; i < retx_count; i++) {
        if (retries[i].data && retries[i].data_len > 0) {
            memcpy(conn->send_buf + conn->send_len, retries[i].data, retries[i].data_len);
            conn->send_len += retries[i].data_len;
            smux_diag_count(conn->diag, SMUX_STAT_RETX);
        }
    }

    return frames_processed;
}
