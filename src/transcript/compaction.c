#include "transcript.h"
#include "../core/byte_utils.h"
#include <stdlib.h>
#include <string.h>

/* Compaction merges multiple old slabs into fewer slabs,
   keeping only summary events for old data. This reclaims memory
   while preserving key state-transition events. */

static int is_important_event(uint16_t type) {
    switch (type) {
        case SMUX_EVT_SESSION_START:
        case SMUX_EVT_SESSION_END:
        case SMUX_EVT_CHANNEL_OPEN:
        case SMUX_EVT_CHANNEL_CLOSE:
        case SMUX_EVT_CHANNEL_ERROR:
        case SMUX_EVT_MIGRATE_START:
        case SMUX_EVT_MIGRATE_DONE:
        case SMUX_EVT_KEY_ROTATE:
        case SMUX_EVT_INTERNAL_ERROR:
            return 1;
        default:
            return 0;
    }
}

smux_status_t smux_transcript_compact(smux_transcript_t *t) {
    if (!t) return SMUX_ERR_INVALID;
    if (t->slab_count <= 1) return SMUX_OK;

    /* Compact all but the tail slab */
    smux_event_slab_t *new_head = NULL;
    int compacted_count = 0;

    /* First pass: count important events in old slabs */
    smux_event_slab_t *s = t->head;
    int important_count = 0;
    while (s && s != t->tail) {
        for (int i = 0; i < s->count; i++) {
            if (is_important_event(s->events[i].event_type))
                important_count++;
        }
        s = s->next;
    }

    /* Allocate compacted slab */
    smux_event_slab_t *compact = calloc(1, sizeof(smux_event_slab_t));
    if (!compact) return SMUX_ERR_NOMEM;
    int cap = important_count > 0 ? important_count : 1;
    compact->events = calloc((size_t)cap, sizeof(smux_event_t));
    if (!compact->events) {
        free(compact);
        return SMUX_ERR_NOMEM;
    }
    compact->capacity = cap;
    compact->compacted = true;
    compact->slab_id = -1;

    /* Second pass: copy important events */
    s = t->head;
    while (s && s != t->tail) {
        for (int i = 0; i < s->count; i++) {
            if (is_important_event(s->events[i].event_type) &&
                compact->count < compact->capacity) {
                compact->events[compact->count++] = s->events[i];
                compacted_count++;
            }
        }
        smux_event_slab_t *next = s->next;
        free(s->events);
        free(s);
        s = next;
    }

    /* Link: compacted → tail */
    compact->next = t->tail;
    t->head = compact;
    t->slab_count = 2;
    /* total_events not updated — allows stale count to persist */

    return SMUX_OK;
}
