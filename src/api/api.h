#ifndef SMUX_API_H
#define SMUX_API_H

#include "../core/types.h"
#include "../core/status.h"
#include "../stream/stream.h"

/* High-level public API for libstreamux.
   Wraps the connection object with a simplified interface
   suitable for embedding in applications. */

/* Connection configuration */
typedef struct {
    uint32_t    max_channels;
    uint32_t    default_window;
    uint32_t    keepalive_ms;
    bool        enable_compression;
    bool        enable_encryption;
    uint32_t    dict_size;
    uint32_t    max_routes;
    int         transcript_capacity;
    smux_log_fn log_fn;
    void       *log_ctx;
} smux_config_t;

void smux_config_default(smux_config_t *cfg);

/* Managed connection: wraps conn with config-driven setup */
typedef struct {
    smux_conn_t    *conn;
    smux_config_t   config;
    bool            handshake_done;
    uint32_t        open_channels;
    uint64_t        connect_time;
    uint64_t        last_activity;
} smux_managed_t;

smux_managed_t *smux_managed_create(const smux_config_t *cfg);
void smux_managed_destroy(smux_managed_t *m);
int smux_managed_connect(smux_managed_t *m);
int smux_managed_feed(smux_managed_t *m, const uint8_t *data, size_t len);
int smux_managed_tick(smux_managed_t *m, uint64_t now_ms);
int smux_managed_channel_open(smux_managed_t *m, uint16_t channel_id);
int smux_managed_channel_close(smux_managed_t *m, uint16_t channel_id);
int smux_managed_channel_read(smux_managed_t *m, uint16_t channel_id,
                               uint8_t *buf, size_t cap, size_t *nread);
int smux_managed_flush(smux_managed_t *m, uint8_t *out, size_t cap, size_t *written);
int smux_managed_is_connected(const smux_managed_t *m);
int smux_managed_channel_count(const smux_managed_t *m);

/* Multiplexer: manages multiple connections with round-robin dispatch */
typedef struct {
    smux_managed_t *connections[32];
    int             count;
    int             capacity;
    int             rr_index;
    uint64_t        tick;
} smux_mux_t;

smux_mux_t *smux_mux_create(int capacity);
void smux_mux_destroy(smux_mux_t *mux);
int smux_mux_add(smux_mux_t *mux, smux_managed_t *m);
int smux_mux_remove(smux_mux_t *mux, int index);
smux_managed_t *smux_mux_get(smux_mux_t *mux, int index);
int smux_mux_tick(smux_mux_t *mux, uint64_t now_ms);
int smux_mux_select_next(smux_mux_t *mux);

/* Connection pool for server-side use */
typedef struct {
    smux_managed_t **pool;
    int              size;
    int              capacity;
    smux_config_t    default_config;
    int             *free_list;
    int              free_count;
} smux_conn_pool_t;

smux_conn_pool_t *smux_conn_pool_create(int capacity, const smux_config_t *cfg);
void smux_conn_pool_destroy(smux_conn_pool_t *pool);
smux_managed_t *smux_conn_pool_acquire(smux_conn_pool_t *pool);
int smux_conn_pool_release(smux_conn_pool_t *pool, smux_managed_t *m);
int smux_conn_pool_active_count(const smux_conn_pool_t *pool);

/* Utility: build seed corpus frame sequences */
int smux_build_handshake_seed(uint8_t *buf, size_t cap, size_t *out_len,
                               uint32_t window);
int smux_build_data_seed(uint8_t *buf, size_t cap, size_t *out_len,
                          uint16_t channel_id, const uint8_t *payload,
                          uint32_t payload_len);
int smux_build_fragment_seed(uint8_t *buf, size_t cap, size_t *out_len,
                              uint16_t channel_id, uint16_t total_frags,
                              uint32_t msg_len);
int smux_build_migrate_seed(uint8_t *buf, size_t cap, size_t *out_len,
                             uint16_t from_route, uint16_t to_route);

#endif
