#include "transcript.h"
#include <string.h>

smux_transcript_iter_t smux_transcript_iter_begin(smux_transcript_t *t) {
    smux_transcript_iter_t it;
    memset(&it, 0, sizeof(it));
    if (t) {
        it.transcript = t;
        it.current_slab = t->head;
        it.current_idx = 0;
    }
    return it;
}

smux_transcript_iter_t smux_transcript_iter_filtered(smux_transcript_t *t,
                                                      uint16_t channel_id) {
    smux_transcript_iter_t it = smux_transcript_iter_begin(t);
    it.filter_channel = channel_id;
    it.has_channel_filter = true;
    return it;
}

int smux_transcript_iter_next(smux_transcript_iter_t *it, smux_event_t *out) {
    if (!it || !out || !it->current_slab) return 0;

    while (it->current_slab) {
        while (it->current_idx < it->current_slab->count) {
            smux_event_t *evt = &it->current_slab->events[it->current_idx];
            it->current_idx++;

            /* Apply filters */
            if (it->has_channel_filter && evt->channel_id != it->filter_channel)
                continue;
            if (it->has_type_filter && evt->event_type != it->filter_event_type)
                continue;

            *out = *evt;
            return 1;
        }
        it->current_slab = it->current_slab->next;
        it->current_idx = 0;
    }
    return 0;
}

void smux_transcript_iter_reset(smux_transcript_iter_t *it) {
    if (!it || !it->transcript) return;
    it->current_slab = it->transcript->head;
    it->current_idx = 0;
}
