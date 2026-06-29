#include "frame.h"
#include "../core/byte_utils.h"

/* Deep frame validation: semantic checks beyond basic parse.
   Validates payload content structure for control frames. */

static smux_status_t validate_hello(const smux_frame_t *frame) {
    if (frame->payload_len < 8) return SMUX_ERR_BAD_LENGTH;
    /* HELLO payload: version_major(2) + version_minor(2) + max_window(4) */
    uint16_t ver_major = smux_read_u16_be(frame->payload);
    if (ver_major < 1) return SMUX_ERR_BAD_VERSION;
    uint32_t max_win = smux_read_u32_be(frame->payload + 4);
    if (max_win == 0 || max_win > SMUX_MAX_WINDOW) return SMUX_ERR_INVALID;
    return SMUX_OK;
}

static smux_status_t validate_hello_ack(const smux_frame_t *frame) {
    if (frame->payload_len < 8) return SMUX_ERR_BAD_LENGTH;
    uint16_t ver_major = smux_read_u16_be(frame->payload);
    if (ver_major < 1) return SMUX_ERR_BAD_VERSION;
    return SMUX_OK;
}

static smux_status_t validate_data(const smux_frame_t *frame) {
    if (frame->channel_id == 0) return SMUX_ERR_BAD_CHANNEL;
    if (frame->payload_len == 0) return SMUX_ERR_BAD_LENGTH;
    return SMUX_OK;
}

static smux_status_t validate_fragment(const smux_frame_t *frame) {
    if (frame->channel_id == 0) return SMUX_ERR_BAD_CHANNEL;
    /* Fragment needs frag meta extension */
    const smux_extension_t *ext = smux_frame_find_extension(frame, SMUX_EXT_FRAG_META);
    if (!ext) {
        /* Fragment without metadata extension: at minimum need payload */
        if (frame->payload_len == 0) return SMUX_ERR_BAD_LENGTH;
    } else {
        if (ext->ext_len < 12) return SMUX_ERR_BAD_LENGTH;
        uint16_t frag_total = smux_read_u16_be(ext->ext_data + 2);
        if (frag_total == 0) return SMUX_ERR_INVALID;
        uint32_t msg_len = smux_read_u32_be(ext->ext_data + 4);
        if (msg_len == 0) return SMUX_ERR_INVALID;
    }
    return SMUX_OK;
}

static smux_status_t validate_close(const smux_frame_t *frame) {
    if (frame->channel_id == 0) return SMUX_ERR_BAD_CHANNEL;
    return SMUX_OK;
}

static smux_status_t validate_error(const smux_frame_t *frame) {
    if (frame->payload_len < 4) return SMUX_ERR_BAD_LENGTH;
    return SMUX_OK;
}

static smux_status_t validate_window(const smux_frame_t *frame) {
    if (frame->channel_id == 0) return SMUX_ERR_BAD_CHANNEL;
    if (frame->payload_len < 4) return SMUX_ERR_BAD_LENGTH;
    uint32_t new_win = smux_read_u32_be(frame->payload);
    if (new_win > SMUX_MAX_WINDOW) return SMUX_ERR_INVALID;
    return SMUX_OK;
}

static smux_status_t validate_receipt(const smux_frame_t *frame) {
    if (frame->payload_len < 4) return SMUX_ERR_BAD_LENGTH;
    return SMUX_OK;
}

static smux_status_t validate_migrate(const smux_frame_t *frame) {
    if (frame->payload_len < 6) return SMUX_ERR_BAD_LENGTH;
    return SMUX_OK;
}

static smux_status_t validate_key_rotate(const smux_frame_t *frame) {
    if (frame->payload_len < 4) return SMUX_ERR_BAD_LENGTH;
    return SMUX_OK;
}

static smux_status_t validate_compress(const smux_frame_t *frame) {
    if (frame->payload_len < 2) return SMUX_ERR_BAD_LENGTH;
    return SMUX_OK;
}

static smux_status_t validate_priority(const smux_frame_t *frame) {
    if (frame->channel_id == 0) return SMUX_ERR_BAD_CHANNEL;
    if (frame->payload_len < 2) return SMUX_ERR_BAD_LENGTH;
    uint8_t pri = frame->payload[0];
    if (pri > SMUX_MAX_PRIORITY) return SMUX_ERR_PRIORITY;
    return SMUX_OK;
}

static smux_status_t validate_batch(const smux_frame_t *frame) {
    if (frame->payload_len < 4) return SMUX_ERR_BAD_LENGTH;
    uint16_t batch_count = smux_read_u16_be(frame->payload);
    if (batch_count == 0 || batch_count > SMUX_BATCH_MAX) return SMUX_ERR_BATCH_FULL;
    return SMUX_OK;
}

smux_status_t smux_frame_deep_validate(const smux_frame_t *frame) {
    if (!frame) return SMUX_ERR_INVALID;

    switch (frame->type) {
        case SMUX_FRAME_HELLO:       return validate_hello(frame);
        case SMUX_FRAME_HELLO_ACK:   return validate_hello_ack(frame);
        case SMUX_FRAME_DATA:        return validate_data(frame);
        case SMUX_FRAME_FRAGMENT:    return validate_fragment(frame);
        case SMUX_FRAME_FRAG_END:    return validate_fragment(frame);
        case SMUX_FRAME_CLOSE:       return validate_close(frame);
        case SMUX_FRAME_ERROR:       return validate_error(frame);
        case SMUX_FRAME_WINDOW:      return validate_window(frame);
        case SMUX_FRAME_PING:        return SMUX_OK;
        case SMUX_FRAME_PONG:        return SMUX_OK;
        case SMUX_FRAME_RECEIPT:     return validate_receipt(frame);
        case SMUX_FRAME_RECEIPT_ACK: return validate_receipt(frame);
        case SMUX_FRAME_MIGRATE:     return validate_migrate(frame);
        case SMUX_FRAME_MIGRATE_ACK: return validate_migrate(frame);
        case SMUX_FRAME_KEY_ROTATE:  return validate_key_rotate(frame);
        case SMUX_FRAME_COMPRESS:    return validate_compress(frame);
        case SMUX_FRAME_PRIORITY:    return validate_priority(frame);
        case SMUX_FRAME_BATCH:       return validate_batch(frame);
        default:                     return SMUX_ERR_BAD_TYPE;
    }
}
