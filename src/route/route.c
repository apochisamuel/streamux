#include "route.h"
#include "../core/byte_utils.h"
#include <stdlib.h>
#include <string.h>

smux_route_table_t *smux_route_table_create(void) {
    smux_route_table_t *rt = calloc(1, sizeof(smux_route_table_t));
    if (!rt) return NULL;
    rt->primary_route = 0;
    return rt;
}

void smux_route_table_destroy(smux_route_table_t *rt) {
    free(rt);
}

smux_status_t smux_route_add(smux_route_table_t *rt, uint16_t id,
                              uint32_t capacity, uint32_t latency) {
    if (!rt || id >= SMUX_MAX_ROUTES) return SMUX_ERR_INVALID;
    if (rt->routes[id].active) return SMUX_ERR_DUPLICATE;
    if (rt->active_count >= SMUX_MAX_ROUTES) return SMUX_ERR_ROUTE_FULL;

    smux_route_t *r = &rt->routes[id];
    memset(r, 0, sizeof(*r));
    r->route_id = id;
    r->active = true;
    r->capacity = capacity;
    r->latency_us = latency;
    rt->active_count++;

    smux_ack_tracker_init(&rt->ack_map[id], 0, id);

    if (rt->active_count == 1) rt->primary_route = id;
    return SMUX_OK;
}

smux_status_t smux_route_remove(smux_route_table_t *rt, uint16_t id) {
    if (!rt || id >= SMUX_MAX_ROUTES) return SMUX_ERR_INVALID;
    if (!rt->routes[id].active) return SMUX_ERR_NO_ROUTE;

    rt->routes[id].active = false;
    rt->active_count--;

    if (rt->primary_route == id) {
        rt->primary_route = 0;
        for (int i = 0; i < SMUX_MAX_ROUTES; i++) {
            if (rt->routes[i].active) {
                rt->primary_route = (uint16_t)i;
                break;
            }
        }
    }
    return SMUX_OK;
}

smux_route_t *smux_route_get(smux_route_table_t *rt, uint16_t id) {
    if (!rt) return NULL;
    if (!rt->routes[id].active) return NULL;
    return &rt->routes[id];
}

smux_route_t *smux_route_get_primary(smux_route_table_t *rt) {
    if (!rt) return NULL;
    return smux_route_get(rt, rt->primary_route);
}

smux_status_t smux_route_set_primary(smux_route_table_t *rt, uint16_t id) {
    if (!rt || id >= SMUX_MAX_ROUTES) return SMUX_ERR_INVALID;
    if (!rt->routes[id].active) return SMUX_ERR_NO_ROUTE;
    rt->primary_route = id;
    return SMUX_OK;
}

smux_status_t smux_route_assign_channel(smux_route_table_t *rt, uint16_t route_id,
                                         uint16_t ch_id) {
    if (!rt) return SMUX_ERR_INVALID;
    smux_route_t *r = smux_route_get(rt, route_id);
    if (!r) return SMUX_ERR_NO_ROUTE;
    if (r->channels_assigned >= SMUX_MAX_CHANNELS) return SMUX_ERR_CHANNEL_FULL;

    for (uint32_t i = 0; i < r->channels_assigned; i++) {
        if (r->channel_list[i] == ch_id) return SMUX_ERR_DUPLICATE;
    }

    r->channel_list[r->channels_assigned++] = ch_id;
    return SMUX_OK;
}

smux_status_t smux_route_unassign_channel(smux_route_table_t *rt, uint16_t route_id,
                                           uint16_t ch_id) {
    if (!rt) return SMUX_ERR_INVALID;
    smux_route_t *r = smux_route_get(rt, route_id);
    if (!r) return SMUX_ERR_NO_ROUTE;

    for (uint32_t i = 0; i < r->channels_assigned; i++) {
        if (r->channel_list[i] == ch_id) {
            r->channels_assigned--;
            r->channel_list[i] = r->channel_list[r->channels_assigned];
            return SMUX_OK;
        }
    }
    return SMUX_ERR_BAD_CHANNEL;
}

uint16_t smux_route_select_best(smux_route_table_t *rt, uint32_t required_cap) {
    if (!rt) return 0;
    uint16_t best = rt->primary_route;
    uint32_t best_score = UINT32_MAX;

    for (int i = 0; i < SMUX_MAX_ROUTES; i++) {
        smux_route_t *r = &rt->routes[i];
        if (!r->active) continue;
        if (r->capacity < required_cap) continue;

        /* Score: lower latency + fewer channels = better */
        uint32_t score = r->latency_us + r->channels_assigned * 1000;
        if (score < best_score) {
            best_score = score;
            best = (uint16_t)i;
        }
    }
    return best;
}

smux_status_t smux_route_migrate_channel(smux_route_table_t *rt, uint16_t ch_id,
                                          uint16_t from_route, uint16_t to_route) {
    if (!rt) return SMUX_ERR_INVALID;

    smux_status_t rc = smux_route_unassign_channel(rt, from_route, ch_id);
    if (rc != SMUX_OK && rc != SMUX_ERR_BAD_CHANNEL) return rc;

    rc = smux_route_assign_channel(rt, to_route, ch_id);
    if (rc != SMUX_OK) return rc;

    /* Transfer pending ack state: note the ack map for the old route
       still has entries referencing this channel. The entries remain
       valid — they point to the packet data, which hasn't moved. */
    rt->routes[to_route].pending_acks += rt->routes[from_route].pending_acks;

    return SMUX_OK;
}

smux_ack_tracker_t *smux_route_get_ack_map(smux_route_table_t *rt, uint16_t route_id) {
    if (!rt || route_id >= SMUX_MAX_ROUTES) return NULL;
    return &rt->ack_map[route_id];
}
