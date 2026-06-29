#ifndef SMUX_ROUTE_H
#define SMUX_ROUTE_H
#include "../core/types.h"
#include "../core/status.h"
#include "../receipt/receipt.h"

typedef struct {
    smux_route_t       routes[SMUX_MAX_ROUTES];
    int                active_count;
    uint16_t           primary_route;
    smux_ack_tracker_t ack_map[SMUX_MAX_ROUTES];
} smux_route_table_t;

smux_route_table_t *smux_route_table_create(void);
void smux_route_table_destroy(smux_route_table_t *rt);
smux_status_t smux_route_add(smux_route_table_t *rt, uint16_t id, uint32_t capacity, uint32_t latency);
smux_status_t smux_route_remove(smux_route_table_t *rt, uint16_t id);
smux_route_t *smux_route_get(smux_route_table_t *rt, uint16_t id);
smux_route_t *smux_route_get_primary(smux_route_table_t *rt);
smux_status_t smux_route_set_primary(smux_route_table_t *rt, uint16_t id);
smux_status_t smux_route_assign_channel(smux_route_table_t *rt, uint16_t route_id, uint16_t ch_id);
smux_status_t smux_route_unassign_channel(smux_route_table_t *rt, uint16_t route_id, uint16_t ch_id);
uint16_t smux_route_select_best(smux_route_table_t *rt, uint32_t required_cap);
smux_status_t smux_route_migrate_channel(smux_route_table_t *rt, uint16_t ch_id,
                                          uint16_t from_route, uint16_t to_route);
smux_ack_tracker_t *smux_route_get_ack_map(smux_route_table_t *rt, uint16_t route_id);

#endif
