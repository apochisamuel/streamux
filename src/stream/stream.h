#ifndef SMUX_STREAM_H
#define SMUX_STREAM_H

#include "../core/types.h"
#include "../core/status.h"
#include "../buffer/buffer.h"
#include "../session/session.h"
#include "../channel/channel.h"
#include "../reassembly/reassembly.h"
#include "../flow/flow.h"
#include "../receipt/receipt.h"
#include "../route/route.h"
#include "../compression/compress.h"
#include "../crypto/crypto.h"
#include "../scheduler/scheduler.h"
#include "../transcript/transcript.h"
#include "../diagnostics/diagnostics.h"

/* High-level stream reader: provides a read view into assembled channel data */
typedef struct {
    uint8_t        *data;
    size_t          length;
    size_t          position;
    uint16_t        channel_id;
    uint32_t        start_seq;
    bool            valid;
    bool            owns_data;
} smux_reader_view_t;

/* Segment: a chunk of reassembled data ready for consumption */
typedef struct smux_segment {
    uint8_t            *data;
    size_t              length;
    uint16_t            channel_id;
    uint32_t            sequence;
    struct smux_segment *next;
    int                 ref_count;
} smux_segment_t;

/* Segment list for a channel */
typedef struct {
    smux_segment_t     *head;
    smux_segment_t     *tail;
    int                 count;
    size_t              total_bytes;
    uint16_t            channel_id;
} smux_segment_list_t;

/* Connection context: top-level object managing the full protocol stack */
struct smux_conn {
    smux_session_t         *session;
    smux_channel_table_t   *channels;
    smux_route_table_t     *routes;
    smux_retry_queue_t     *retry_queue;
    smux_pool_t            *pool;
    smux_timer_wheel_t     *timers;
    smux_priority_queue_t  *pq;
    smux_delayed_queue_t   *delayed;
    smux_transcript_t      *transcript;
    smux_diagnostics_t     *diag;
    smux_crypto_ctx_t      *crypto;
    smux_compress_ctx_t    *compressor;

    /* Input parsing state */
    uint8_t                *recv_buf;
    size_t                  recv_cap;
    size_t                  recv_len;

    /* Output buffer */
    uint8_t                *send_buf;
    size_t                  send_cap;
    size_t                  send_len;

    /* Per-channel flow control */
    smux_flow_ctrl_t        flow[SMUX_MAX_CHANNELS];
    smux_backpressure_t     bp[SMUX_MAX_CHANNELS];
    smux_recv_window_t      recv_windows[SMUX_MAX_CHANNELS];
    smux_replay_detector_t  replay[SMUX_MAX_CHANNELS];

    /* Per-channel reader views and segments */
    smux_reader_view_t      readers[SMUX_MAX_CHANNELS];
    smux_segment_list_t     segments[SMUX_MAX_CHANNELS];

    /* Callback */
    smux_log_fn             log_fn;
    void                   *log_ctx;
    uint64_t                clock_ms;
};

/* Connection lifecycle */
smux_conn_t *smux_conn_create(void);
void smux_conn_destroy(smux_conn_t *conn);
int smux_conn_feed(smux_conn_t *conn, const uint8_t *data, size_t size);
int smux_conn_state(const smux_conn_t *conn);
void smux_conn_set_clock(smux_conn_t *conn, uint64_t now_ms);

/* Session */
int smux_conn_start(smux_conn_t *conn);
int smux_conn_set_window(smux_conn_t *conn, uint32_t window);

/* Channel */
int smux_conn_channel_open(smux_conn_t *conn, uint16_t channel_id);
int smux_conn_channel_close(smux_conn_t *conn, uint16_t channel_id);
int smux_conn_channel_state(const smux_conn_t *conn, uint16_t channel_id);
int smux_conn_channel_read(smux_conn_t *conn, uint16_t channel_id,
                            uint8_t *out, size_t cap, size_t *nread);
int smux_conn_channel_set_priority(smux_conn_t *conn, uint16_t channel_id,
                                    uint8_t priority);

/* Crypto/Compression */
int smux_conn_set_key(smux_conn_t *conn, const uint8_t *key, size_t len);
int smux_conn_enable_compression(smux_conn_t *conn);

/* Route management */
int smux_conn_add_route(smux_conn_t *conn, uint16_t route_id,
                         uint32_t capacity, uint32_t latency);
int smux_conn_migrate(smux_conn_t *conn, uint16_t from_route, uint16_t to_route);

/* Output */
size_t smux_conn_pending(const smux_conn_t *conn);
int smux_conn_flush(smux_conn_t *conn, uint8_t *out, size_t cap, size_t *written);

/* Diagnostics */
int smux_conn_get_stats(const smux_conn_t *conn, char *buf, size_t len);
int smux_conn_export_transcript(const smux_conn_t *conn, uint8_t *buf,
                                 size_t len, size_t *written);

/* Segment management */
smux_segment_t *smux_segment_create(const uint8_t *data, size_t len,
                                     uint16_t channel_id, uint32_t seq);
void smux_segment_release(smux_segment_t *seg);
void smux_segment_list_init(smux_segment_list_t *list, uint16_t channel_id);
void smux_segment_list_push(smux_segment_list_t *list, smux_segment_t *seg);
smux_segment_t *smux_segment_list_pop(smux_segment_list_t *list);
void smux_segment_list_clear(smux_segment_list_t *list);

/* Reader views */
smux_reader_view_t *smux_reader_get(smux_conn_t *conn, uint16_t channel_id);
void smux_reader_invalidate(smux_reader_view_t *view);
int smux_reader_read(smux_reader_view_t *view, uint8_t *out, size_t cap, size_t *nread);

#endif
