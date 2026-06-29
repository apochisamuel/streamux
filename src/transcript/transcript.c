#include "transcript.h"
#include "../core/byte_utils.h"
#include <stdlib.h>
#include <string.h>

#define SLAB_EVENTS 256

static smux_event_slab_t *create_slab(int slab_id) {
    smux_event_slab_t *s = calloc(1, sizeof(smux_event_slab_t));
    if (!s) return NULL;
    s->events = calloc(SLAB_EVENTS, sizeof(smux_event_t));
    if (!s->events) {
        free(s);
        return NULL;
    }
    s->capacity = SLAB_EVENTS;
    s->slab_id = slab_id;
    return s;
}

smux_transcript_t *smux_transcript_create(int max_events) {
    smux_transcript_t *t = calloc(1, sizeof(smux_transcript_t));
    if (!t) return NULL;
    t->max_events = max_events > 0 ? max_events : SMUX_TRANSCRIPT_CAP;
    t->enabled = true;

    smux_event_slab_t *first = create_slab(0);
    if (!first) {
        free(t);
        return NULL;
    }
    t->head = first;
    t->tail = first;
    t->slab_count = 1;
    return t;
}

void smux_transcript_destroy(smux_transcript_t *t) {
    if (!t) return;
    smux_event_slab_t *s = t->head;
    while (s) {
        smux_event_slab_t *next = s->next;
        free(s->events);
        free(s);
        s = next;
    }
    free(t);
}

smux_status_t smux_transcript_record(smux_transcript_t *t, smux_event_type_t type,
                                      uint16_t channel_id, uint32_t sequence,
                                      const uint8_t *detail, size_t detail_len) {
    if (!t || !t->enabled) return SMUX_OK;
    if (t->total_events >= t->max_events) {
        /* Auto-compact if at capacity */
        smux_transcript_compact(t);
        if (t->total_events >= t->max_events)
            return SMUX_ERR_OVERFLOW;
    }

    /* Ensure current tail slab has space */
    if (t->tail->count >= t->tail->capacity) {
        smux_event_slab_t *new_slab = create_slab(t->slab_count);
        if (!new_slab) return SMUX_ERR_NOMEM;
        t->tail->next = new_slab;
        t->tail = new_slab;
        t->slab_count++;
    }

    smux_event_t *evt = &t->tail->events[t->tail->count];
    evt->timestamp = t->total_events; /* would use real clock */
    evt->event_type = (uint16_t)type;
    evt->channel_id = channel_id;
    evt->sequence = sequence;

    if (detail && detail_len > 0) {
        size_t copy = detail_len > 128 ? 128 : detail_len;
        memcpy(evt->detail, detail, copy);
        evt->detail_len = (uint32_t)copy;
    } else {
        evt->detail_len = 0;
    }

    t->tail->count++;
    t->total_events++;
    return SMUX_OK;
}

int smux_transcript_count(const smux_transcript_t *t) {
    return t ? t->total_events : 0;
}

void smux_transcript_enable(smux_transcript_t *t) { if (t) t->enabled = true; }
void smux_transcript_disable(smux_transcript_t *t) { if (t) t->enabled = false; }
