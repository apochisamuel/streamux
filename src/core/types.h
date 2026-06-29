#ifndef SMUX_CORE_TYPES_H
#define SMUX_CORE_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define SMUX_MAGIC          0x534D5558u
#define SMUX_VERSION_MAJOR  1
#define SMUX_VERSION_MINOR  0
#define SMUX_MAX_CHANNELS   256
#define SMUX_MAX_FRAME_SIZE 65536
#define SMUX_MIN_FRAME_SIZE 20
#define SMUX_HEADER_SIZE    16
#define SMUX_CRC_SIZE       4
#define SMUX_MAX_PAYLOAD    (SMUX_MAX_FRAME_SIZE - SMUX_HEADER_SIZE - SMUX_CRC_SIZE)
#define SMUX_DEFAULT_WINDOW 8192
#define SMUX_MAX_WINDOW     (1u << 24)
#define SMUX_MAX_EXTENSIONS 8
#define SMUX_EXT_HEADER_SIZE 4
#define SMUX_MAX_ROUTES     16
#define SMUX_MAX_RETRY      8
#define SMUX_TIMER_SLOTS    512
#define SMUX_TRANSCRIPT_CAP 4096
#define SMUX_MAX_FRAGMENTS  128
#define SMUX_MAX_KEY_LEN    64
#define SMUX_DICT_SIZE      4096
#define SMUX_POOL_SLABS     32
#define SMUX_SLAB_SIZE      4096
#define SMUX_MAX_PRIORITY   7
#define SMUX_KEEPALIVE_MS   30000
#define SMUX_RETRY_BASE_MS  500
#define SMUX_RETRY_MAX_MS   16000
#define SMUX_BATCH_MAX      16
#define SMUX_SEQ_WINDOW     1024

typedef enum {
    SMUX_FRAME_HELLO        = 0x01,
    SMUX_FRAME_HELLO_ACK    = 0x02,
    SMUX_FRAME_DATA         = 0x10,
    SMUX_FRAME_FRAGMENT     = 0x11,
    SMUX_FRAME_FRAG_END     = 0x12,
    SMUX_FRAME_CLOSE        = 0x20,
    SMUX_FRAME_ERROR        = 0x21,
    SMUX_FRAME_WINDOW       = 0x30,
    SMUX_FRAME_PING         = 0x40,
    SMUX_FRAME_PONG         = 0x41,
    SMUX_FRAME_RECEIPT      = 0x50,
    SMUX_FRAME_RECEIPT_ACK  = 0x51,
    SMUX_FRAME_MIGRATE      = 0x60,
    SMUX_FRAME_MIGRATE_ACK  = 0x61,
    SMUX_FRAME_KEY_ROTATE   = 0x70,
    SMUX_FRAME_COMPRESS     = 0x80,
    SMUX_FRAME_PRIORITY     = 0x90,
    SMUX_FRAME_BATCH        = 0xA0
} smux_frame_type_t;

typedef enum {
    SMUX_FLAG_NONE          = 0x00,
    SMUX_FLAG_COMPRESSED    = 0x01,
    SMUX_FLAG_ENCRYPTED     = 0x02,
    SMUX_FLAG_HAS_EXT       = 0x04,
    SMUX_FLAG_URGENT        = 0x08,
    SMUX_FLAG_RETRANSMIT    = 0x10,
    SMUX_FLAG_FINAL         = 0x20
} smux_flags_t;

typedef enum {
    SMUX_SESS_IDLE          = 0,
    SMUX_SESS_HANDSHAKE     = 1,
    SMUX_SESS_ESTABLISHED   = 2,
    SMUX_SESS_DRAINING      = 3,
    SMUX_SESS_CLOSED        = 4
} smux_session_state_t;

typedef enum {
    SMUX_CHAN_CLOSED         = 0,
    SMUX_CHAN_OPENING        = 1,
    SMUX_CHAN_OPEN           = 2,
    SMUX_CHAN_DRAINING       = 3,
    SMUX_CHAN_ERROR          = 4,
    SMUX_CHAN_MIGRATING      = 5
} smux_channel_state_t;

typedef struct {
    uint16_t ext_type;
    uint16_t ext_len;
    uint8_t  ext_data[256];
} smux_extension_t;

typedef struct {
    uint32_t            magic;
    uint8_t             flags;
    uint8_t             type;
    uint16_t            channel_id;
    uint32_t            sequence;
    uint32_t            payload_len;
    uint8_t             ext_count;
    smux_extension_t    extensions[SMUX_MAX_EXTENSIONS];
    uint8_t            *payload;
    uint32_t            crc;
} smux_frame_t;

typedef struct smux_buffer_slice {
    uint8_t            *data;
    size_t              capacity;
    size_t              length;
    int                 ref_count;
    struct smux_buffer_slice *next;
} smux_slice_t;

typedef struct {
    uint16_t            channel_id;
    smux_channel_state_t state;
    uint32_t            local_seq;
    uint32_t            remote_seq;
    uint32_t            window_size;
    uint32_t            window_used;
    uint32_t            credit;
    uint8_t             priority;
    uint16_t            route_id;
    bool                has_reassembly;
    void               *reassembly_ctx;
    void               *delivery_ctx;
    uint64_t            opened_at;
    uint64_t            last_activity;
    uint32_t            bytes_sent;
    uint32_t            bytes_received;
    uint32_t            fragments_pending;
    bool                close_requested;
    bool                error_flagged;
} smux_channel_t;

typedef struct {
    uint16_t            route_id;
    bool                active;
    uint32_t            capacity;
    uint32_t            latency_us;
    uint32_t            channels_assigned;
    uint16_t            channel_list[SMUX_MAX_CHANNELS];
    uint32_t            pending_acks;
    uint64_t            migrated_at;
} smux_route_t;

typedef struct {
    uint32_t            seq;
    uint8_t            *data;
    size_t              data_len;
    uint16_t            channel_id;
    uint8_t             retries;
    uint64_t            sent_at;
    uint64_t            next_retry;
    bool                acked;
    bool                cancelled;
    uint16_t            route_id;
} smux_retry_entry_t;

typedef struct {
    uint64_t            timestamp;
    uint16_t            event_type;
    uint16_t            channel_id;
    uint32_t            sequence;
    uint32_t            detail_len;
    uint8_t             detail[128];
} smux_event_t;

typedef void (*smux_alloc_fn)(void *ctx, size_t size, void **out);
typedef void (*smux_free_fn)(void *ctx, void *ptr);
typedef void (*smux_log_fn)(void *ctx, int level, const char *msg);

typedef struct smux_conn smux_conn_t;

#endif
