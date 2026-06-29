#include "../core/types.h"
#include "../core/byte_utils.h"
#include "../frame/frame.h"
#include "../checksum/checksum.h"
#include <stdio.h>
#include <string.h>

/* CLI decoder: decode frames and extract payload to output */

typedef struct {
    int         frame_count;
    int         error_count;
    size_t      total_payload_bytes;
    uint32_t    channels_seen[SMUX_MAX_CHANNELS / 32 + 1];
    int         unique_channels;
} smux_decode_stats_t;

static void mark_channel(smux_decode_stats_t *stats, uint16_t ch_id) {
    if (ch_id >= SMUX_MAX_CHANNELS) return;
    uint32_t word = ch_id / 32;
    uint32_t bit = ch_id % 32;
    if (!(stats->channels_seen[word] & (1u << bit))) {
        stats->channels_seen[word] |= (1u << bit);
        stats->unique_channels++;
    }
}

int smux_cli_decode(const uint8_t *data, size_t len,
                     uint8_t *payload_out, size_t out_cap, size_t *out_written,
                     int verbose) {
    if (!data || len == 0 || !out_written) return -1;

    smux_decode_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    size_t out_pos = 0;
    size_t offset = 0;

    while (offset + SMUX_MIN_FRAME_SIZE <= len) {
        uint32_t magic = smux_read_u32_be(data + offset);
        if (magic != SMUX_MAGIC) {
            offset++;
            continue;
        }

        smux_frame_t frame;
        smux_status_t rc = smux_frame_parse(data + offset, len - offset, &frame);
        if (rc != SMUX_OK) {
            stats.error_count++;
            offset++;
            continue;
        }

        stats.frame_count++;
        mark_channel(&stats, frame.channel_id);

        /* Extract payload for data/fragment frames */
        if (smux_frame_is_data(frame.type) && frame.payload_len > 0) {
            if (payload_out && out_pos + frame.payload_len <= out_cap) {
                memcpy(payload_out + out_pos, frame.payload, frame.payload_len);
                out_pos += frame.payload_len;
            }
            stats.total_payload_bytes += frame.payload_len;
        }

        if (verbose) {
            printf("[%04zu] %s ch=%u seq=%u len=%u",
                   offset, smux_frame_type_name(frame.type),
                   frame.channel_id, frame.sequence, frame.payload_len);
            if (frame.flags & SMUX_FLAG_COMPRESSED) printf(" [C]");
            if (frame.flags & SMUX_FLAG_ENCRYPTED) printf(" [E]");
            if (frame.flags & SMUX_FLAG_RETRANSMIT) printf(" [R]");
            printf("\n");
        }

        offset += smux_frame_wire_size(&frame);
    }

    *out_written = out_pos;

    if (verbose) {
        printf("\n--- Decode Summary ---\n");
        printf("Frames: %d (errors: %d)\n", stats.frame_count, stats.error_count);
        printf("Payload bytes: %zu\n", stats.total_payload_bytes);
        printf("Unique channels: %d\n", stats.unique_channels);
    }

    return stats.frame_count;
}

/* Validate a binary stream: check all frames parse and have valid CRCs */
int smux_cli_validate(const uint8_t *data, size_t len) {
    if (!data || len == 0) return -1;

    int valid = 0, invalid = 0;
    size_t offset = 0;

    while (offset + SMUX_MIN_FRAME_SIZE <= len) {
        uint32_t magic = smux_read_u32_be(data + offset);
        if (magic != SMUX_MAGIC) {
            offset++;
            continue;
        }

        smux_frame_t frame;
        smux_status_t rc = smux_frame_parse(data + offset, len - offset, &frame);
        if (rc == SMUX_OK) {
            rc = smux_frame_validate(&frame);
            if (rc == SMUX_OK) {
                valid++;
            } else {
                invalid++;
                printf("Frame at %zu: validation error: %s\n",
                       offset, smux_status_str(rc));
            }
            offset += smux_frame_wire_size(&frame);
        } else {
            invalid++;
            printf("Frame at %zu: parse error: %s\n", offset, smux_status_str(rc));
            offset++;
        }
    }

    printf("Validation: %d valid, %d invalid\n", valid, invalid);
    return invalid == 0 ? 0 : 1;
}
