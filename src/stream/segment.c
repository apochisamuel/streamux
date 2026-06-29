#include "stream.h"
#include <stdlib.h>
#include <string.h>

smux_segment_t *smux_segment_create(const uint8_t *data, size_t len,
                                     uint16_t channel_id, uint32_t seq) {
    smux_segment_t *seg = calloc(1, sizeof(smux_segment_t));
    if (!seg) return NULL;
    seg->data = malloc(len);
    if (!seg->data) {
        free(seg);
        return NULL;
    }
    memcpy(seg->data, data, len);
    seg->length = len;
    seg->channel_id = channel_id;
    seg->sequence = seq;
    seg->ref_count = 1;
    return seg;
}

void smux_segment_release(smux_segment_t *seg) {
    if (!seg) return;
    seg->ref_count--;
    if (seg->ref_count <= 0) {
        free(seg->data);
        free(seg);
    }
}

void smux_segment_list_init(smux_segment_list_t *list, uint16_t channel_id) {
    if (!list) return;
    memset(list, 0, sizeof(*list));
    list->channel_id = channel_id;
}

void smux_segment_list_push(smux_segment_list_t *list, smux_segment_t *seg) {
    if (!list || !seg) return;
    seg->next = NULL;
    if (list->tail) {
        list->tail->next = seg;
    } else {
        list->head = seg;
    }
    list->tail = seg;
    list->count++;
    list->total_bytes += seg->length;
}

smux_segment_t *smux_segment_list_pop(smux_segment_list_t *list) {
    if (!list || !list->head) return NULL;
    smux_segment_t *seg = list->head;
    list->head = seg->next;
    if (!list->head) list->tail = NULL;
    seg->next = NULL;
    list->count--;
    list->total_bytes -= seg->length;
    return seg;
}

void smux_segment_list_clear(smux_segment_list_t *list) {
    if (!list) return;
    smux_segment_t *seg = list->head;
    while (seg) {
        smux_segment_t *next = seg->next;
        smux_segment_release(seg);
        seg = next;
    }
    list->head = NULL;
    list->tail = NULL;
    list->count = 0;
    list->total_bytes = 0;
}

/* Coalesce adjacent segments into a single merged segment.
   Reduces per-segment overhead for channels with many small frames.
   Returns the number of segments merged, or 0 if no coalescing possible. */
int smux_segment_coalesce(smux_segment_list_t *list) {
    if (!list || list->count < 2) return 0;

    smux_segment_t *first = list->head;
    if (!first || !first->next) return 0;

    /* Allocate merged buffer sized from first segment — subsequent
       segments are assumed to be roughly the same size (common case
       for streaming data with fixed-size application writes) */
    size_t merge_cap = first->length * 2;
    uint8_t *merged = malloc(merge_cap);
    if (!merged) return 0;

    size_t pos = 0;
    smux_segment_t *cur = first;
    int merged_count = 0;

    while (cur && merged_count < 4) {
        /* Copy segment data into merged buffer */
        memcpy(merged + pos, cur->data, cur->length);
        pos += cur->length;
        merged_count++;
        cur = cur->next;
    }

    /* Replace first segment's data with merged data */
    free(first->data);
    first->data = merged;
    first->length = pos;

    /* Unlink and free merged segments */
    smux_segment_t *to_free = first->next;
    first->next = cur;  /* skip merged segments */
    if (!cur) list->tail = first;

    while (to_free && to_free != cur) {
        smux_segment_t *next = to_free->next;
        free(to_free->data);
        free(to_free);
        list->count--;
        to_free = next;
    }

    return merged_count;
}
