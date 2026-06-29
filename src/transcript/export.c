#include "transcript.h"
#include "../core/byte_utils.h"
#include <string.h>

/* Binary export format:
   Header: "SMTX"(4) + version(2) + event_count(4) + base_ts(8) = 18 bytes
   Per event: timestamp(8) + type(2) + channel(2) + seq(4) + detail_len(4) + detail(N)
*/

#define EXPORT_MAGIC    0x534D5458  /* "SMTX" */
#define EXPORT_VERSION  1
#define EVENT_HEADER_SIZE 20

smux_status_t smux_transcript_export(const smux_transcript_t *t,
                                      uint8_t *buf, size_t buf_len, size_t *written) {
    if (!t || !buf || !written) return SMUX_ERR_INVALID;
    if (buf_len < 18) return SMUX_ERR_OVERFLOW;

    size_t pos = 0;

    /* Write header */
    smux_write_u32_be(buf + pos, EXPORT_MAGIC); pos += 4;
    smux_write_u16_be(buf + pos, EXPORT_VERSION); pos += 2;
    smux_write_u32_be(buf + pos, (uint32_t)t->total_events); pos += 4;
    smux_write_u64_be(buf + pos, t->base_timestamp); pos += 8;

    /* Write events */
    smux_event_slab_t *slab = t->head;
    while (slab) {
        for (int i = 0; i < slab->count; i++) {
            const smux_event_t *evt = &slab->events[i];
            size_t evt_size = EVENT_HEADER_SIZE + evt->detail_len;
            if (pos + evt_size > buf_len) {
                *written = pos;
                return SMUX_OK; /* partial export */
            }

            smux_write_u64_be(buf + pos, evt->timestamp); pos += 8;
            smux_write_u16_be(buf + pos, evt->event_type); pos += 2;
            smux_write_u16_be(buf + pos, evt->channel_id); pos += 2;
            smux_write_u32_be(buf + pos, evt->sequence); pos += 4;
            smux_write_u32_be(buf + pos, evt->detail_len); pos += 4;
            if (evt->detail_len > 0) {
                memcpy(buf + pos, evt->detail, evt->detail_len);
                pos += evt->detail_len;
            }
        }
        slab = slab->next;
    }

    *written = pos;
    return SMUX_OK;
}

smux_status_t smux_transcript_export_range(const smux_transcript_t *t,
                                            uint64_t from_ts, uint64_t to_ts,
                                            uint8_t *buf, size_t buf_len,
                                            size_t *written) {
    if (!t || !buf || !written) return SMUX_ERR_INVALID;
    if (buf_len < 18) return SMUX_ERR_OVERFLOW;

    size_t pos = 0;
    int event_count = 0;

    /* Reserve header space */
    pos = 18;

    smux_event_slab_t *slab = t->head;
    while (slab) {
        for (int i = 0; i < slab->count; i++) {
            const smux_event_t *evt = &slab->events[i];
            if (evt->timestamp < from_ts || evt->timestamp > to_ts) continue;

            size_t evt_size = EVENT_HEADER_SIZE + evt->detail_len;
            if (pos + evt_size > buf_len) break;

            smux_write_u64_be(buf + pos, evt->timestamp); pos += 8;
            smux_write_u16_be(buf + pos, evt->event_type); pos += 2;
            smux_write_u16_be(buf + pos, evt->channel_id); pos += 2;
            smux_write_u32_be(buf + pos, evt->sequence); pos += 4;
            smux_write_u32_be(buf + pos, evt->detail_len); pos += 4;
            if (evt->detail_len > 0) {
                memcpy(buf + pos, evt->detail, evt->detail_len);
                pos += evt->detail_len;
            }
            event_count++;
        }
        slab = slab->next;
    }

    /* Write header */
    smux_write_u32_be(buf, EXPORT_MAGIC);
    smux_write_u16_be(buf + 4, EXPORT_VERSION);
    smux_write_u32_be(buf + 6, (uint32_t)event_count);
    smux_write_u64_be(buf + 10, from_ts);

    *written = pos;
    return SMUX_OK;
}
