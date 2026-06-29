#ifndef SMUX_CHANNEL_H
#define SMUX_CHANNEL_H

#include "../core/types.h"
#include "../core/status.h"
#include "../buffer/buffer.h"

typedef struct {
    smux_channel_t  channels[SMUX_MAX_CHANNELS];
    uint16_t        active_count;
    uint16_t        draining_count;
    smux_pool_t    *pool;
} smux_channel_table_t;

smux_channel_table_t *smux_channel_table_create(smux_pool_t *pool);
void smux_channel_table_destroy(smux_channel_table_t *table);
smux_channel_t *smux_channel_table_get(smux_channel_table_t *table, uint16_t id);
smux_channel_t *smux_channel_table_find_open(smux_channel_table_t *table, uint16_t id);

smux_status_t smux_channel_open(smux_channel_table_t *table, uint16_t id,
                                uint32_t window_size, uint16_t route_id);
smux_status_t smux_channel_close(smux_channel_table_t *table, uint16_t id);
smux_status_t smux_channel_error(smux_channel_table_t *table, uint16_t id,
                                 uint32_t error_code);
smux_status_t smux_channel_drain(smux_channel_table_t *table, uint16_t id);
smux_status_t smux_channel_set_state(smux_channel_table_t *table, uint16_t id,
                                      smux_channel_state_t state);
smux_status_t smux_channel_update_seq(smux_channel_table_t *table, uint16_t id,
                                       uint32_t seq);
smux_status_t smux_channel_set_priority(smux_channel_table_t *table, uint16_t id,
                                         uint8_t priority);
smux_status_t smux_channel_migrate(smux_channel_table_t *table, uint16_t id,
                                    uint16_t new_route_id);
void smux_channel_cleanup_resources(smux_channel_table_t *table, uint16_t id);
int smux_channel_accepts_data(const smux_channel_t *ch);
int smux_channel_can_receive(const smux_channel_t *ch);
uint32_t smux_channel_available_window(const smux_channel_t *ch);

#endif
