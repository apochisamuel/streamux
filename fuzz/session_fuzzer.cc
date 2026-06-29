/*
 * Robustness validation harness: full session lifecycle.
 * Starts handshake, feeds data, reads from channels,
 * and exercises close/error paths.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include "stream/stream.h"
#include "core/types.h"
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 16 || size > 65536) return 0;

    smux_conn_t *conn = smux_conn_create();
    if (!conn) return 0;

    smux_conn_set_window(conn, SMUX_DEFAULT_WINDOW);
    smux_conn_set_clock(conn, 1000);

    /* Initiate handshake (generates HELLO in send buffer) */
    smux_conn_start(conn);

    /* Feed the fuzz data — may contain HELLO_ACK, DATA, CLOSE, etc. */
    smux_conn_feed(conn, data, size);
    smux_conn_set_clock(conn, 2000);

    /* Read all available channel data */
    uint8_t readbuf[4096];
    size_t nread = 0;
    for (uint16_t ch = 0; ch < SMUX_MAX_CHANNELS; ch++) {
        int state = smux_conn_channel_state(conn, ch);
        if (state != SMUX_CHAN_CLOSED) {
            smux_conn_channel_read(conn, ch, readbuf, sizeof(readbuf), &nread);
        }
    }

    /* Get diagnostics */
    char diagbuf[2048];
    smux_conn_get_stats(conn, diagbuf, sizeof(diagbuf));

    /* Export transcript */
    uint8_t transcript_buf[8192];
    size_t transcript_len = 0;
    smux_conn_export_transcript(conn, transcript_buf, sizeof(transcript_buf), &transcript_len);

    /* Flush pending output */
    uint8_t outbuf[8192];
    size_t written = 0;
    smux_conn_flush(conn, outbuf, sizeof(outbuf), &written);

    smux_conn_destroy(conn);
    return 0;
}
