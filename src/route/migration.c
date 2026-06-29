#include "route.h"
#include "../channel/channel.h"
#include "../reassembly/reassembly.h"
#include "../core/byte_utils.h"
#include <string.h>

/* Migration protocol: coordinates channel movement between routes.
   
   Migration sequence:
   1. MIGRATE frame sent with target route + channel list
   2. Sender pauses data on affected channels  
   3. Receiver processes migration, reassigns channels
   4. MIGRATE_ACK confirms completion
   5. Sender resumes data on new route
   
   During migration, reassembly state must be handled carefully:
   - Active reassemblies continue on the original buffer allocations
   - New fragments go to new route's allocation context
   - Retransmission queue entries update their route tags
*/

typedef struct {
    uint16_t    channels[SMUX_MAX_CHANNELS];
    uint16_t    channel_count;
    uint16_t    from_route;
    uint16_t    to_route;
    bool        in_progress;
    uint64_t    started_at;
    uint32_t    migration_seq;
} smux_migration_ctx_t;

static smux_migration_ctx_t active_migration;

smux_status_t smux_migration_begin(smux_route_table_t *rt,
                                    smux_channel_table_t *ct,
                                    uint16_t from_route, uint16_t to_route,
                                    uint32_t seq) {
    if (!rt || !ct) return SMUX_ERR_INVALID;
    if (active_migration.in_progress) return SMUX_ERR_MIGRATING;

    smux_route_t *src = smux_route_get(rt, from_route);
    smux_route_t *dst = smux_route_get(rt, to_route);
    if (!src || !dst) return SMUX_ERR_NO_ROUTE;

    memset(&active_migration, 0, sizeof(active_migration));
    active_migration.from_route = from_route;
    active_migration.to_route = to_route;
    active_migration.in_progress = true;
    active_migration.migration_seq = seq;

    /* Collect all channels assigned to source route */
    active_migration.channel_count = 0;
    for (uint32_t i = 0; i < src->channels_assigned; i++) {
        uint16_t ch_id = src->channel_list[i];
        smux_channel_t *ch = smux_channel_table_get(ct, ch_id);
        if (ch && ch->state == SMUX_CHAN_OPEN) {
            active_migration.channels[active_migration.channel_count++] = ch_id;
            /* Mark channel as migrating */
            ch->state = SMUX_CHAN_MIGRATING;
        }
    }

    return SMUX_OK;
}

smux_status_t smux_migration_execute(smux_route_table_t *rt,
                                      smux_channel_table_t *ct,
                                      smux_retry_queue_t *rq) {
    if (!active_migration.in_progress) return SMUX_ERR_BAD_STATE;
    if (!rt || !ct) return SMUX_ERR_INVALID;

    uint16_t from = active_migration.from_route;
    uint16_t to = active_migration.to_route;

    for (uint16_t i = 0; i < active_migration.channel_count; i++) {
        uint16_t ch_id = active_migration.channels[i];

        /* Move channel assignment in route table */
        smux_route_migrate_channel(rt, ch_id, from, to);

        /* Update channel's route reference */
        smux_channel_t *ch = smux_channel_table_get(ct, ch_id);
        if (ch) {
            ch->route_id = to;
            ch->state = SMUX_CHAN_OPEN; /* back to open */
        }

        /* Update retry queue entries for this channel */
        if (rq) {
            smux_retry_queue_update_route(rq, ch_id, from, to);
        }
    }

    return SMUX_OK;
}

smux_status_t smux_migration_complete(smux_route_table_t *rt) {
    if (!active_migration.in_progress) return SMUX_ERR_BAD_STATE;
    (void)rt;

    active_migration.in_progress = false;
    return SMUX_OK;
}

smux_status_t smux_migration_abort(smux_channel_table_t *ct) {
    if (!active_migration.in_progress) return SMUX_ERR_BAD_STATE;

    /* Restore channels to OPEN state */
    for (uint16_t i = 0; i < active_migration.channel_count; i++) {
        smux_channel_t *ch = smux_channel_table_get(ct, active_migration.channels[i]);
        if (ch && ch->state == SMUX_CHAN_MIGRATING) {
            ch->state = SMUX_CHAN_OPEN;
        }
    }

    active_migration.in_progress = false;
    return SMUX_OK;
}

int smux_migration_in_progress(void) {
    return active_migration.in_progress;
}

uint16_t smux_migration_target_route(void) {
    return active_migration.to_route;
}
