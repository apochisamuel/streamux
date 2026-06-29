/*
 * Robustness validation harness: encryption and compression paths.
 * Enables crypto and compression adapters before feeding data,
 * exercising the decrypt/decompress pipeline under malformed input.
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
    if (size < 24 || size > 65536) return 0;

    smux_conn_t *conn = smux_conn_create();
    if (!conn) return 0;

    smux_conn_set_window(conn, SMUX_DEFAULT_WINDOW);
    smux_conn_set_clock(conn, 1000);

    /* Enable encryption with a key derived from first 16 bytes */
    smux_conn_set_key(conn, data, 16);

    /* Enable compression */
    smux_conn_enable_compression(conn);

    /* Start session */
    smux_conn_start(conn);

    /* Open channels with different priorities */
    smux_conn_channel_open(conn, 1);
    smux_conn_channel_set_priority(conn, 1, 2);
    smux_conn_channel_open(conn, 2);
    smux_conn_channel_set_priority(conn, 2, 5);

    /* Feed remaining data through the decrypt/decompress pipeline */
    smux_conn_feed(conn, data + 16, size - 16);
    smux_conn_set_clock(conn, 2000);

    /* Read decrypted/decompressed data */
    uint8_t readbuf[4096];
    size_t nread = 0;
    smux_conn_channel_read(conn, 1, readbuf, sizeof(readbuf), &nread);
    smux_conn_channel_read(conn, 2, readbuf, sizeof(readbuf), &nread);

    /* Flush */
    uint8_t outbuf[8192];
    size_t written = 0;
    smux_conn_flush(conn, outbuf, sizeof(outbuf), &written);

    smux_conn_destroy(conn);
    return 0;
}
