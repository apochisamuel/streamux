/*
 * Robustness validation harness: route migration and channel handoff.
 * Configures smaller windows, adds multiple routes, and exercises
 * the migration path including channel reassignment and pending
 * acknowledgment transfer.
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

    /* Use smaller windows to trigger edge cases sooner */
    smux_conn_set_window(conn, 256);
    smux_conn_set_clock(conn, 1000);

    /* Add secondary route for migration */
    smux_conn_add_route(conn, 1, 512, 100);

    /* Open channels on default route */
    smux_conn_channel_open(conn, 1);
    smux_conn_channel_open(conn, 2);

    /* Feed first portion */
    size_t third = size / 3;
    smux_conn_feed(conn, data, third);
    smux_conn_set_clock(conn, 2000);

    /* Trigger migration: move channels from route 0 to route 1 */
    smux_conn_migrate(conn, 0, 1);

    /* Feed second portion post-migration */
    smux_conn_feed(conn, data + third, third);
    smux_conn_set_clock(conn, 3000);

    /* Read from channels that were migrated */
    uint8_t readbuf[4096];
    size_t nread = 0;
    smux_conn_channel_read(conn, 1, readbuf, sizeof(readbuf), &nread);
    smux_conn_channel_read(conn, 2, readbuf, sizeof(readbuf), &nread);

    /* Feed final portion */
    smux_conn_feed(conn, data + 2 * third, size - 2 * third);
    smux_conn_set_clock(conn, 4000);

    /* Flush */
    uint8_t outbuf[8192];
    size_t written = 0;
    smux_conn_flush(conn, outbuf, sizeof(outbuf), &written);

    smux_conn_destroy(conn);
    return 0;
}
