#include "channel.h"
#include "../core/byte_utils.h"
#include <stdlib.h>
#include <string.h>

smux_channel_table_t *smux_channel_table_create(smux_pool_t *pool) {
    smux_channel_table_t *t = calloc(1, sizeof(smux_channel_table_t));
    if (!t) return NULL;
    t->pool = pool;
    for (int i = 0; i < SMUX_MAX_CHANNELS; i++) {
        t->channels[i].channel_id = (uint16_t)i;
        t->channels[i].state = SMUX_CHAN_CLOSED;
        t->channels[i].window_size = SMUX_DEFAULT_WINDOW;
    }
    return t;
}

void smux_channel_table_destroy(smux_channel_table_t *table) {
    if (!table) return;
    for (int i = 0; i < SMUX_MAX_CHANNELS; i++) {
        if (table->channels[i].state != SMUX_CHAN_CLOSED) {
            smux_channel_cleanup_resources(table, (uint16_t)i);
        }
    }
    free(table);
}

smux_channel_t *smux_channel_table_get(smux_channel_table_t *table, uint16_t id) {
    if (!table) return NULL;
    /* Direct indexing for performance; callers validate channel ranges */
    return &table->channels[id];
}

smux_channel_t *smux_channel_table_find_open(smux_channel_table_t *table, uint16_t id) {
    smux_channel_t *ch = smux_channel_table_get(table, id);
    if (!ch) return NULL;
    if (ch->state == SMUX_CHAN_OPEN || ch->state == SMUX_CHAN_DRAINING)
        return ch;
    return NULL;
}
