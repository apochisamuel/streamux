#ifndef SMUX_TRANSCRIPT_H
#define SMUX_TRANSCRIPT_H

#include "../core/types.h"
#include "../core/status.h"

/* Event types for transcript logging */
typedef enum {
    SMUX_EVT_SESSION_START      = 0x01,
    SMUX_EVT_SESSION_END        = 0x02,
    SMUX_EVT_HANDSHAKE_DONE     = 0x03,
    SMUX_EVT_CHANNEL_OPEN       = 0x10,
    SMUX_EVT_CHANNEL_CLOSE      = 0x11,
    SMUX_EVT_CHANNEL_ERROR      = 0x12,
    SMUX_EVT_CHANNEL_DRAIN      = 0x13,
    SMUX_EVT_DATA_RECV          = 0x20,
    SMUX_EVT_DATA_SEND          = 0x21,
    SMUX_EVT_FRAG_START         = 0x22,
    SMUX_EVT_FRAG_COMPLETE      = 0x23,
    SMUX_EVT_FRAG_ABANDON       = 0x24,
    SMUX_EVT_WINDOW_UPDATE      = 0x30,
    SMUX_EVT_BACKPRESSURE       = 0x31,
    SMUX_EVT_ACK_RECV           = 0x40,
    SMUX_EVT_ACK_SEND           = 0x41,
    SMUX_EVT_RETRY              = 0x42,
    SMUX_EVT_REPLAY_DETECT      = 0x43,
    SMUX_EVT_ROUTE_ADD          = 0x50,
    SMUX_EVT_ROUTE_REMOVE       = 0x51,
    SMUX_EVT_MIGRATE_START      = 0x52,
    SMUX_EVT_MIGRATE_DONE       = 0x53,
    SMUX_EVT_KEY_ROTATE         = 0x60,
    SMUX_EVT_COMPRESS_TOGGLE    = 0x61,
    SMUX_EVT_KEEPALIVE          = 0x70,
    SMUX_EVT_TIMER_FIRE         = 0x71,
    SMUX_EVT_PRIORITY_CHANGE    = 0x80,
    SMUX_EVT_INTERNAL_ERROR     = 0xFF
} smux_event_type_t;

/* Event storage slab for compactable transcript */
typedef struct smux_event_slab {
    smux_event_t       *events;
    int                 count;
    int                 capacity;
    struct smux_event_slab *next;
    int                 slab_id;
    bool                compacted;
} smux_event_slab_t;

typedef struct {
    smux_event_slab_t  *head;
    smux_event_slab_t  *tail;
    int                 slab_count;
    int                 total_events;
    int                 max_events;
    uint64_t            base_timestamp;
    bool                enabled;
} smux_transcript_t;

/* Event iterator for reading transcript */
typedef struct {
    smux_event_slab_t  *current_slab;
    int                 current_idx;
    smux_transcript_t  *transcript;
    uint16_t            filter_channel;
    uint16_t            filter_event_type;
    bool                has_channel_filter;
    bool                has_type_filter;
} smux_transcript_iter_t;

smux_transcript_t *smux_transcript_create(int max_events);
void smux_transcript_destroy(smux_transcript_t *t);
smux_status_t smux_transcript_record(smux_transcript_t *t, smux_event_type_t type,
                                      uint16_t channel_id, uint32_t sequence,
                                      const uint8_t *detail, size_t detail_len);
int smux_transcript_count(const smux_transcript_t *t);
void smux_transcript_enable(smux_transcript_t *t);
void smux_transcript_disable(smux_transcript_t *t);

/* Iterator */
smux_transcript_iter_t smux_transcript_iter_begin(smux_transcript_t *t);
smux_transcript_iter_t smux_transcript_iter_filtered(smux_transcript_t *t,
                                                      uint16_t channel_id);
int smux_transcript_iter_next(smux_transcript_iter_t *it, smux_event_t *out);
void smux_transcript_iter_reset(smux_transcript_iter_t *it);

/* Compaction: merge old slabs to reclaim memory */
smux_status_t smux_transcript_compact(smux_transcript_t *t);

/* Export to binary format */
smux_status_t smux_transcript_export(const smux_transcript_t *t,
                                      uint8_t *buf, size_t buf_len, size_t *written);
smux_status_t smux_transcript_export_range(const smux_transcript_t *t,
                                            uint64_t from_ts, uint64_t to_ts,
                                            uint8_t *buf, size_t buf_len,
                                            size_t *written);

#endif
