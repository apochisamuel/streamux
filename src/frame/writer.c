#include "frame.h"
#include "../core/byte_utils.h"
#include "../checksum/crc32.h"
#include <string.h>

void smux_frame_init(smux_frame_t *frame, uint8_t type, uint16_t channel_id) {
    if (!frame) return;
    memset(frame, 0, sizeof(*frame));
    frame->magic = SMUX_MAGIC;
    frame->type = type;
    frame->channel_id = channel_id;
}

size_t smux_frame_wire_size(const smux_frame_t *frame) {
    size_t size = SMUX_HEADER_SIZE;
    if (frame->ext_count > 0) {
        size += 1; /* ext count byte */
        for (uint8_t i = 0; i < frame->ext_count; i++) {
            size += SMUX_EXT_HEADER_SIZE + frame->extensions[i].ext_len;
        }
    }
    size += frame->payload_len;
    size += SMUX_CRC_SIZE;
    return size;
}

int smux_frame_serialize(const smux_frame_t *frame, uint8_t *buf, size_t buf_len) {
    if (!frame || !buf) return SMUX_ERR_INVALID;

    size_t needed = smux_frame_wire_size(frame);
    if (buf_len < needed) return SMUX_ERR_OVERFLOW;

    /* Write fixed header */
    smux_write_u32_be(buf, frame->magic);
    buf[4] = frame->flags | (frame->ext_count > 0 ? SMUX_FLAG_HAS_EXT : 0);
    buf[5] = frame->type;
    smux_write_u16_be(buf + 6, frame->channel_id);
    smux_write_u32_be(buf + 8, frame->sequence);
    smux_write_u32_be(buf + 12, frame->payload_len);

    size_t offset = SMUX_HEADER_SIZE;

    /* Write extensions */
    if (frame->ext_count > 0) {
        buf[offset++] = frame->ext_count;
        for (uint8_t i = 0; i < frame->ext_count; i++) {
            smux_write_u16_be(buf + offset, frame->extensions[i].ext_type);
            smux_write_u16_be(buf + offset + 2, frame->extensions[i].ext_len);
            offset += SMUX_EXT_HEADER_SIZE;
            if (frame->extensions[i].ext_len > 0) {
                memcpy(buf + offset, frame->extensions[i].ext_data,
                       frame->extensions[i].ext_len);
                offset += frame->extensions[i].ext_len;
            }
        }
    }

    /* Write payload */
    if (frame->payload_len > 0 && frame->payload) {
        memcpy(buf + offset, frame->payload, frame->payload_len);
        offset += frame->payload_len;
    }

    /* Compute and write CRC */
    uint32_t crc = smux_crc32(buf, offset);
    smux_write_u32_be(buf + offset, crc);
    offset += SMUX_CRC_SIZE;

    return (int)offset;
}

int smux_frame_add_extension(smux_frame_t *frame, uint16_t type,
                             const uint8_t *data, uint16_t len) {
    if (!frame || frame->ext_count >= SMUX_MAX_EXTENSIONS)
        return SMUX_ERR_OVERFLOW;
    if (len > 256) return SMUX_ERR_BAD_LENGTH;

    smux_extension_t *ext = &frame->extensions[frame->ext_count];
    ext->ext_type = type;
    ext->ext_len = len;
    if (data && len > 0)
        memcpy(ext->ext_data, data, len);
    frame->ext_count++;
    frame->flags |= SMUX_FLAG_HAS_EXT;
    return SMUX_OK;
}

const smux_extension_t *smux_frame_find_extension(const smux_frame_t *frame,
                                                   uint16_t type) {
    if (!frame) return NULL;
    for (uint8_t i = 0; i < frame->ext_count; i++) {
        if (frame->extensions[i].ext_type == type)
            return &frame->extensions[i];
    }
    return NULL;
}

int smux_frame_is_control(uint8_t type) {
    switch (type) {
        case SMUX_FRAME_HELLO:
        case SMUX_FRAME_HELLO_ACK:
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
            return 1;
        default:
            return 0;
    }
}

int smux_frame_is_data(uint8_t type) {
    return type == SMUX_FRAME_DATA ||
           type == SMUX_FRAME_FRAGMENT ||
           type == SMUX_FRAME_FRAG_END;
}

int smux_frame_requires_channel(uint8_t type) {
    return smux_frame_is_data(type) ||
           type == SMUX_FRAME_CLOSE ||
           type == SMUX_FRAME_ERROR ||
           type == SMUX_FRAME_WINDOW ||
           type == SMUX_FRAME_PRIORITY;
}

int smux_frame_requires_session(uint8_t type) {
    return type != SMUX_FRAME_HELLO;
}

const char *smux_frame_type_name(uint8_t type) {
    switch (type) {
        case SMUX_FRAME_HELLO:       return "HELLO";
        case SMUX_FRAME_HELLO_ACK:   return "HELLO_ACK";
        case SMUX_FRAME_DATA:        return "DATA";
        case SMUX_FRAME_FRAGMENT:    return "FRAGMENT";
        case SMUX_FRAME_FRAG_END:    return "FRAG_END";
        case SMUX_FRAME_CLOSE:       return "CLOSE";
        case SMUX_FRAME_ERROR:       return "ERROR";
        case SMUX_FRAME_WINDOW:      return "WINDOW";
        case SMUX_FRAME_PING:        return "PING";
        case SMUX_FRAME_PONG:        return "PONG";
        case SMUX_FRAME_RECEIPT:     return "RECEIPT";
        case SMUX_FRAME_RECEIPT_ACK: return "RECEIPT_ACK";
        case SMUX_FRAME_MIGRATE:     return "MIGRATE";
        case SMUX_FRAME_MIGRATE_ACK: return "MIGRATE_ACK";
        case SMUX_FRAME_KEY_ROTATE:  return "KEY_ROTATE";
        case SMUX_FRAME_COMPRESS:    return "COMPRESS";
        case SMUX_FRAME_PRIORITY:    return "PRIORITY";
        case SMUX_FRAME_BATCH:       return "BATCH";
        default:                     return "UNKNOWN";
    }
}
