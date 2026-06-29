#include "../core/types.h"
#include "../core/status.h"
#include "../core/byte_utils.h"
#include "../frame/frame.h"
#include "../checksum/crc32.h"
#include "../checksum/checksum.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* CLI frame inspector: reads binary input and prints frame details */

static void print_hex_dump(const uint8_t *data, size_t len, int indent) {
    for (size_t i = 0; i < len; i++) {
        if (i % 16 == 0) {
            if (i > 0) printf("\n");
            for (int j = 0; j < indent; j++) printf(" ");
            printf("%04zx: ", i);
        }
        printf("%02x ", data[i]);
    }
    if (len > 0) printf("\n");
}

static void print_extension(const smux_extension_t *ext) {
    const char *ext_names[] = {
        "unknown", "TIMESTAMP", "TRACE_ID", "PRIORITY",
        "ROUTE_TAG", "FRAG_META", "COMPRESS", "KEY_ID"
    };
    int idx = ext->ext_type <= 7 ? ext->ext_type : 0;
    printf("    Extension: type=%s(0x%04x) len=%u\n",
           ext_names[idx], ext->ext_type, ext->ext_len);

    if (ext->ext_type == SMUX_EXT_TIMESTAMP && ext->ext_len >= 8) {
        uint64_t ts = smux_read_u64_be(ext->ext_data);
        printf("      timestamp: %llu us\n", (unsigned long long)ts);
    } else if (ext->ext_type == SMUX_EXT_PRIORITY && ext->ext_len >= 4) {
        printf("      priority=%u weight=%u dep=%u\n",
               ext->ext_data[0], ext->ext_data[1],
               smux_read_u16_be(ext->ext_data + 2));
    } else if (ext->ext_type == SMUX_EXT_FRAG_META && ext->ext_len >= 12) {
        printf("      frag_id=%u total=%u msg_len=%u offset=%u\n",
               smux_read_u16_be(ext->ext_data),
               smux_read_u16_be(ext->ext_data + 2),
               smux_read_u32_be(ext->ext_data + 4),
               smux_read_u32_be(ext->ext_data + 8));
    } else if (ext->ext_type == SMUX_EXT_ROUTE_TAG && ext->ext_len >= 8) {
        printf("      route=%u hops=%u path_hash=%08x\n",
               smux_read_u16_be(ext->ext_data),
               smux_read_u16_be(ext->ext_data + 2),
               smux_read_u32_be(ext->ext_data + 4));
    } else if (ext->ext_type == SMUX_EXT_KEY_ID && ext->ext_len >= 4) {
        printf("      key_id=%u\n", smux_read_u32_be(ext->ext_data));
    } else if (ext->ext_len > 0) {
        print_hex_dump(ext->ext_data, ext->ext_len, 6);
    }
}

static void print_frame(const smux_frame_t *frame, int frame_num) {
    printf("Frame #%d:\n", frame_num);
    printf("  Magic: 0x%08x (%s)\n", frame->magic,
           frame->magic == SMUX_MAGIC ? "valid" : "INVALID");
    printf("  Type:  %s (0x%02x)\n", smux_frame_type_name(frame->type), frame->type);
    printf("  Flags: 0x%02x", frame->flags);
    if (frame->flags & SMUX_FLAG_COMPRESSED) printf(" COMPRESSED");
    if (frame->flags & SMUX_FLAG_ENCRYPTED) printf(" ENCRYPTED");
    if (frame->flags & SMUX_FLAG_HAS_EXT) printf(" HAS_EXT");
    if (frame->flags & SMUX_FLAG_URGENT) printf(" URGENT");
    if (frame->flags & SMUX_FLAG_RETRANSMIT) printf(" RETRANSMIT");
    if (frame->flags & SMUX_FLAG_FINAL) printf(" FINAL");
    printf("\n");
    printf("  Channel: %u\n", frame->channel_id);
    printf("  Sequence: %u\n", frame->sequence);
    printf("  Payload: %u bytes\n", frame->payload_len);
    printf("  CRC: 0x%08x\n", frame->crc);

    /* Extensions */
    if (frame->ext_count > 0) {
        printf("  Extensions: %u\n", frame->ext_count);
        for (uint8_t i = 0; i < frame->ext_count; i++) {
            print_extension(&frame->extensions[i]);
        }
    }

    /* Type-specific payload decode */
    if (frame->payload && frame->payload_len > 0) {
        switch (frame->type) {
            case SMUX_FRAME_HELLO:
            case SMUX_FRAME_HELLO_ACK:
                if (frame->payload_len >= 8) {
                    printf("  Version: %u.%u\n",
                           smux_read_u16_be(frame->payload),
                           smux_read_u16_be(frame->payload + 2));
                    printf("  Max Window: %u\n",
                           smux_read_u32_be(frame->payload + 4));
                }
                break;
            case SMUX_FRAME_WINDOW:
                if (frame->payload_len >= 4)
                    printf("  New Window: %u\n", smux_read_u32_be(frame->payload));
                break;
            case SMUX_FRAME_ERROR:
                if (frame->payload_len >= 4)
                    printf("  Error Code: %u\n", smux_read_u32_be(frame->payload));
                break;
            case SMUX_FRAME_RECEIPT:
            case SMUX_FRAME_RECEIPT_ACK:
                if (frame->payload_len >= 4)
                    printf("  Acked Seq: %u\n", smux_read_u32_be(frame->payload));
                break;
            case SMUX_FRAME_MIGRATE:
                if (frame->payload_len >= 6) {
                    printf("  From Route: %u\n", smux_read_u16_be(frame->payload));
                    printf("  To Route: %u\n", smux_read_u16_be(frame->payload + 2));
                    printf("  Channel Count: %u\n", smux_read_u16_be(frame->payload + 4));
                }
                break;
            case SMUX_FRAME_PRIORITY:
                if (frame->payload_len >= 2)
                    printf("  New Priority: %u Weight: %u\n",
                           frame->payload[0], frame->payload[1]);
                break;
            default:
                if (frame->payload_len <= 64) {
                    printf("  Payload:\n");
                    print_hex_dump(frame->payload, frame->payload_len, 4);
                } else {
                    printf("  Payload: (first 64 of %u bytes)\n", frame->payload_len);
                    print_hex_dump(frame->payload, 64, 4);
                }
                break;
        }
    }
    printf("\n");
}

int smux_cli_inspect(const uint8_t *data, size_t len) {
    if (!data || len == 0) {
        fprintf(stderr, "Error: empty input\n");
        return 1;
    }

    printf("Input: %zu bytes\n\n", len);

    size_t offset = 0;
    int frame_num = 0;

    while (offset + SMUX_MIN_FRAME_SIZE <= len) {
        /* Find next magic */
        uint32_t magic = smux_read_u32_be(data + offset);
        if (magic != SMUX_MAGIC) {
            offset++;
            continue;
        }

        smux_frame_t frame;
        smux_status_t rc = smux_frame_parse(data + offset, len - offset, &frame);
        if (rc == SMUX_OK) {
            frame_num++;
            print_frame(&frame, frame_num);
            offset += smux_frame_wire_size(&frame);
        } else {
            printf("Parse error at offset %zu: %s\n", offset, smux_status_str(rc));
            offset++;
        }
    }

    if (frame_num == 0) {
        printf("No valid frames found\n");
    } else {
        printf("Total: %d frames\n", frame_num);
    }

    return frame_num > 0 ? 0 : 1;
}
