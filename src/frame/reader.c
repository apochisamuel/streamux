#include "frame.h"
#include "../core/byte_utils.h"
#include "../checksum/crc32.h"
#include <string.h>

smux_status_t smux_frame_parse(const uint8_t *raw, size_t raw_len,
                                smux_frame_t *frame) {
    if (!raw || !frame) return SMUX_ERR_INVALID;
    if (raw_len < SMUX_MIN_FRAME_SIZE) return SMUX_ERR_TRUNCATED;

    memset(frame, 0, sizeof(*frame));

    /* Read fixed header: magic(4) + flags(1) + type(1) + channel(2) + seq(4) + len(4) = 16 */
    frame->magic = smux_read_u32_be(raw);
    if (frame->magic != SMUX_MAGIC) return SMUX_ERR_BAD_MAGIC;

    frame->flags = raw[4];
    frame->type = raw[5];
    frame->channel_id = smux_read_u16_be(raw + 6);
    frame->sequence = smux_read_u32_be(raw + 8);
    frame->payload_len = smux_read_u32_be(raw + 12);

    /* Validate total length */
    size_t total_needed = SMUX_HEADER_SIZE;
    
    /* Parse extensions if present */
    const uint8_t *cursor = raw + SMUX_HEADER_SIZE;
    size_t remaining = raw_len - SMUX_HEADER_SIZE;

    if (frame->flags & SMUX_FLAG_HAS_EXT) {
        if (remaining < 1) return SMUX_ERR_TRUNCATED;
        uint8_t ext_count = *cursor;
        cursor++;
        remaining--;
        total_needed++;

        if (ext_count > SMUX_MAX_EXTENSIONS) return SMUX_ERR_INVALID;
        frame->ext_count = ext_count;

        for (uint8_t i = 0; i < ext_count; i++) {
            if (remaining < SMUX_EXT_HEADER_SIZE) return SMUX_ERR_TRUNCATED;
            frame->extensions[i].ext_type = smux_read_u16_be(cursor);
            frame->extensions[i].ext_len = smux_read_u16_be(cursor + 2);
            cursor += SMUX_EXT_HEADER_SIZE;
            remaining -= SMUX_EXT_HEADER_SIZE;
            total_needed += SMUX_EXT_HEADER_SIZE;

            if (frame->extensions[i].ext_len > 256) return SMUX_ERR_BAD_LENGTH;
            if (remaining < frame->extensions[i].ext_len) return SMUX_ERR_TRUNCATED;
            memcpy(frame->extensions[i].ext_data, cursor, frame->extensions[i].ext_len);
            cursor += frame->extensions[i].ext_len;
            remaining -= frame->extensions[i].ext_len;
            total_needed += frame->extensions[i].ext_len;
        }
    }

    /* Payload */
    if (frame->payload_len > SMUX_MAX_PAYLOAD) return SMUX_ERR_BAD_LENGTH;
    if (remaining < frame->payload_len + SMUX_CRC_SIZE) return SMUX_ERR_TRUNCATED;

    frame->payload = (uint8_t *)(cursor);
    cursor += frame->payload_len;
    remaining -= frame->payload_len;

    /* CRC verification */
    frame->crc = smux_read_u32_be(cursor);
    size_t checked_len = (size_t)(cursor - raw);
    uint32_t computed = smux_crc32(raw, checked_len);
    if (computed != frame->crc) return SMUX_ERR_BAD_CRC;

    return SMUX_OK;
}

smux_status_t smux_frame_validate(const smux_frame_t *frame) {
    if (!frame) return SMUX_ERR_INVALID;

    /* Type range check */
    switch (frame->type) {
        case SMUX_FRAME_HELLO:
        case SMUX_FRAME_HELLO_ACK:
        case SMUX_FRAME_DATA:
        case SMUX_FRAME_FRAGMENT:
        case SMUX_FRAME_FRAG_END:
        case SMUX_FRAME_CLOSE:
        case SMUX_FRAME_ERROR:
        case SMUX_FRAME_WINDOW:
        case SMUX_FRAME_PING:
        case SMUX_FRAME_PONG:
        case SMUX_FRAME_RECEIPT:
        case SMUX_FRAME_RECEIPT_ACK:
        case SMUX_FRAME_MIGRATE:
        case SMUX_FRAME_MIGRATE_ACK:
        case SMUX_FRAME_KEY_ROTATE:
        case SMUX_FRAME_COMPRESS:
        case SMUX_FRAME_PRIORITY:
        case SMUX_FRAME_BATCH:
            break;
        default:
            return SMUX_ERR_BAD_TYPE;
    }

    /* Session frames must have channel 0 */
    if ((frame->type == SMUX_FRAME_HELLO || frame->type == SMUX_FRAME_HELLO_ACK) &&
        frame->channel_id != 0)
        return SMUX_ERR_BAD_CHANNEL;

    /* Ping/pong must have channel 0 */
    if ((frame->type == SMUX_FRAME_PING || frame->type == SMUX_FRAME_PONG) &&
        frame->channel_id != 0)
        return SMUX_ERR_BAD_CHANNEL;

    /* Data/fragment frames need valid channel */
    if (smux_frame_is_data(frame->type) && frame->channel_id == 0)
        return SMUX_ERR_BAD_CHANNEL;

    /* HELLO payload must contain version negotiation */
    if (frame->type == SMUX_FRAME_HELLO && frame->payload_len < 8)
        return SMUX_ERR_BAD_LENGTH;

    /* WINDOW update payload must be 4 bytes (new window size) */
    if (frame->type == SMUX_FRAME_WINDOW && frame->payload_len < 4)
        return SMUX_ERR_BAD_LENGTH;

    /* MIGRATE payload must contain route info */
    if (frame->type == SMUX_FRAME_MIGRATE && frame->payload_len < 6)
        return SMUX_ERR_BAD_LENGTH;

    return SMUX_OK;
}
