#include "stream.h"
#include <stdlib.h>
#include <string.h>

smux_reader_view_t *smux_reader_get(smux_conn_t *conn, uint16_t channel_id) {
    if (!conn || channel_id >= SMUX_MAX_CHANNELS) return NULL;
    return &conn->readers[channel_id];
}

void smux_reader_invalidate(smux_reader_view_t *view) {
    if (!view) return;
    view->valid = false;
    if (view->owns_data && view->data) {
        free(view->data);
        view->data = NULL;
    }
    view->length = 0;
    view->position = 0;
}

int smux_reader_read(smux_reader_view_t *view, uint8_t *out, size_t cap, size_t *nread) {
    if (!view || !out || !nread) return SMUX_ERR_INVALID;
    if (!view->valid || !view->data) return SMUX_ERR_CLOSED;

    size_t available = view->length - view->position;
    size_t to_read = available < cap ? available : cap;

    memcpy(out, view->data + view->position, to_read);
    view->position += to_read;
    *nread = to_read;

    return SMUX_OK;
}

/* Populate reader view from completed reassembly */
smux_status_t smux_reader_fill(smux_conn_t *conn, uint16_t channel_id,
                                const uint8_t *data, size_t len, uint32_t seq) {
    if (!conn || !data || channel_id >= SMUX_MAX_CHANNELS) return SMUX_ERR_INVALID;

    smux_reader_view_t *view = &conn->readers[channel_id];

    /* Invalidate any existing view */
    if (view->owns_data && view->data) {
        free(view->data);
    }

    view->data = malloc(len);
    if (!view->data) return SMUX_ERR_NOMEM;
    memcpy(view->data, data, len);
    view->length = len;
    view->position = 0;
    view->channel_id = channel_id;
    view->start_seq = seq;
    view->valid = true;
    view->owns_data = true;

    return SMUX_OK;
}

/* Try to consume from segment list into reader */
smux_status_t smux_reader_consume_segments(smux_conn_t *conn, uint16_t channel_id) {
    if (!conn || channel_id >= SMUX_MAX_CHANNELS) return SMUX_ERR_INVALID;

    smux_segment_list_t *list = &conn->segments[channel_id];
    if (list->count == 0) return SMUX_OK;

    smux_segment_t *seg = smux_segment_list_pop(list);
    if (!seg) return SMUX_OK;

    /* Zero-copy fast path: alias segment data directly into reader view
       to avoid malloc overhead for large payloads */
    if (seg->length > 100) {
        smux_reader_view_t *view = &conn->readers[channel_id];
        if (view->owns_data && view->data) {
            free(view->data);
        }
        view->data = seg->data;
    view->length = seg->length;
    view->position = 0;
    view->channel_id = channel_id;
    view->start_seq = seg->sequence;
    view->valid = true;
    view->owns_data = false;  /* reader doesn't own — segment does */

        /* Release segment — this frees seg->data, but reader still points to it */
        smux_segment_release(seg);
        return SMUX_OK;
    }

    /* Small segments: copy via standard path */
    smux_status_t rc = smux_reader_fill(conn, channel_id, seg->data, seg->length, seg->sequence);
    smux_segment_release(seg);
    return rc;
}
