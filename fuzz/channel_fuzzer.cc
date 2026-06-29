/*
 * Robustness validation harness: channel state transitions.
 * Feeds data twice to exercise channel lifecycle, retransmission,
 * and reassembly state after partial delivery.
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

    /* Set up default window */
    smux_conn_set_window(conn, SMUX_DEFAULT_WINDOW);
    smux_conn_set_clock(conn, 1000);

    /* Pre-open channels 1-4 for state transition coverage */
    smux_conn_channel_open(conn, 1);
    smux_conn_channel_open(conn, 2);
    smux_conn_channel_open(conn, 3);
    smux_conn_channel_open(conn, 4);

    /* First feed: establish state */
    size_t half = size / 2;
    smux_conn_feed(conn, data, half);
    smux_conn_set_clock(conn, 2000);

    /* Read from channels between feeds to exercise reader views */
    uint8_t readbuf[4096];
    size_t nread = 0;
    for (uint16_t ch = 1; ch <= 4; ch++) {
        smux_conn_channel_read(conn, ch, readbuf, sizeof(readbuf), &nread);
    }

    /* Second feed: exercise retransmission and state after partial reads */
    smux_conn_feed(conn, data + half, size - half);
    smux_conn_set_clock(conn, 3000);

    /* Read again post-second-feed */
    for (uint16_t ch = 1; ch <= 4; ch++) {
        smux_conn_channel_read(conn, ch, readbuf, sizeof(readbuf), &nread);
    }

    /* Flush output */
    uint8_t outbuf[8192];
    size_t written = 0;
    smux_conn_flush(conn, outbuf, sizeof(outbuf), &written);

    smux_conn_destroy(conn);
    return 0;
}
