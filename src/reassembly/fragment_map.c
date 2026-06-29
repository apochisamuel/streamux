#include "reassembly.h"
#include "../core/byte_utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>


/* Fragment map: fine-grained tracking of fragment arrival order
   and overlap detection for robust reassembly. */

typedef struct {
    uint32_t    offset;
    uint32_t    length;
    uint32_t    received_at;  /* sequence counter */
    bool        valid;
} smux_frag_range_t;

typedef struct {
    smux_frag_range_t ranges[128];
    int               count;
    uint32_t          total_received;
    uint32_t          expected_total;
    uint32_t          overlaps;
    uint32_t          gaps;
    uint16_t          channel_id;
    uint16_t          message_id;
    bool              complete;
} smux_fragment_map_t;

/* Forward declaration */
static void smux_fragmap_check_complete(smux_fragment_map_t *fm);

smux_fragment_map_t *smux_fragmap_create(uint16_t channel_id, uint16_t message_id,
                                          uint32_t expected_total) {
    smux_fragment_map_t *fm = calloc(1, sizeof(smux_fragment_map_t));
    if (!fm) return NULL;
    fm->channel_id = channel_id;
    fm->message_id = message_id;
    fm->expected_total = expected_total;
    return fm;
}

void smux_fragmap_destroy(smux_fragment_map_t *fm) {
    free(fm);
}

/* Record a received fragment range */
int smux_fragmap_add(smux_fragment_map_t *fm, uint32_t offset, uint32_t length) {
    if (!fm || fm->count >= 128) return SMUX_ERR_OVERFLOW;

    /* Check for overlaps with existing ranges */
    for (int i = 0; i < fm->count; i++) {
        if (!fm->ranges[i].valid) continue;
        smux_frag_range_t *r = &fm->ranges[i];
        uint32_t r_end = r->offset + r->length;
        uint32_t new_end = offset + length;

        if (offset < r_end && new_end > r->offset) {
            fm->overlaps++;
            /* Merge overlapping ranges */
            uint32_t merged_start = offset < r->offset ? offset : r->offset;
            uint32_t merged_end = new_end > r_end ? new_end : r_end;
            r->offset = merged_start;
            r->length = merged_end - merged_start;
            fm->total_received += length;
            smux_fragmap_check_complete(fm);
            return SMUX_OK;
        }
    }

    /* No overlap: add new range */
    smux_frag_range_t *r = &fm->ranges[fm->count];
    r->offset = offset;
    r->length = length;
    r->received_at = fm->total_received;
    r->valid = true;
    fm->count++;
    fm->total_received += length;

    /* Sort ranges by offset for gap detection */
    for (int i = fm->count - 1; i > 0; i--) {
        if (fm->ranges[i].offset < fm->ranges[i - 1].offset) {
            smux_frag_range_t tmp = fm->ranges[i];
            fm->ranges[i] = fm->ranges[i - 1];
            fm->ranges[i - 1] = tmp;
        } else break;
    }

    smux_fragmap_check_complete(fm);
    return SMUX_OK;
}

/* Check if all fragments have been received (no gaps) */
static void smux_fragmap_check_complete(smux_fragment_map_t *fm) {
    if (!fm || fm->count == 0) return;

    fm->gaps = 0;
    uint32_t covered = 0;

    for (int i = 0; i < fm->count; i++) {
        if (!fm->ranges[i].valid) continue;
        if (fm->ranges[i].offset > covered) {
            fm->gaps++;
        }
        uint32_t range_end = fm->ranges[i].offset + fm->ranges[i].length;
        if (range_end > covered) covered = range_end;
    }

    fm->complete = (fm->gaps == 0 && covered >= fm->expected_total);
}

int smux_fragmap_is_complete(const smux_fragment_map_t *fm) {
    return fm ? fm->complete : 0;
}

/* Get coverage fraction */
float smux_fragmap_coverage(const smux_fragment_map_t *fm) {
    if (!fm || fm->expected_total == 0) return 0.0f;

    /* Compute non-overlapping coverage */
    uint32_t covered = 0;
    for (int i = 0; i < fm->count; i++) {
        if (!fm->ranges[i].valid) continue;
        covered += fm->ranges[i].length;
    }
    /* Subtract overlaps */
    if (covered > fm->expected_total) covered = fm->expected_total;
    return (float)covered / (float)fm->expected_total;
}

/* Get list of missing ranges (gaps) */
int smux_fragmap_get_gaps(const smux_fragment_map_t *fm,
                           uint32_t *gap_offsets, uint32_t *gap_lengths,
                           int max_gaps) {
    if (!fm || !gap_offsets || !gap_lengths) return 0;

    int gap_count = 0;
    uint32_t expected_start = 0;

    for (int i = 0; i < fm->count && gap_count < max_gaps; i++) {
        if (!fm->ranges[i].valid) continue;
        if (fm->ranges[i].offset > expected_start) {
            gap_offsets[gap_count] = expected_start;
            gap_lengths[gap_count] = fm->ranges[i].offset - expected_start;
            gap_count++;
        }
        uint32_t range_end = fm->ranges[i].offset + fm->ranges[i].length;
        if (range_end > expected_start) expected_start = range_end;
    }

    /* Check for gap at end */
    if (expected_start < fm->expected_total && gap_count < max_gaps) {
        gap_offsets[gap_count] = expected_start;
        gap_lengths[gap_count] = fm->expected_total - expected_start;
        gap_count++;
    }

    return gap_count;
}

int smux_fragmap_report(const smux_fragment_map_t *fm, char *buf, size_t len) {
    if (!fm || !buf || len == 0) return 0;
    return snprintf(buf, len,
        "Fragment map ch=%u msg=%u: %d ranges, coverage=%.1f%%\n"
        "  Total received: %u / %u bytes\n"
        "  Overlaps: %u  Gaps: %u  Complete: %s\n",
        fm->channel_id, fm->message_id,
        fm->count, smux_fragmap_coverage(fm) * 100.0f,
        fm->total_received, fm->expected_total,
        fm->overlaps, fm->gaps,
        fm->complete ? "yes" : "no"
    );
}
