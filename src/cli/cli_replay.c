#include "../core/types.h"
#include "../core/byte_utils.h"
#include "../frame/frame.h"
#include "../stream/stream.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* CLI replay: feeds binary input through a connection and reports results */

int smux_cli_replay(const uint8_t *data, size_t len, int verbose) {
    if (!data || len == 0) return -1;

    smux_conn_t *conn = smux_conn_create();
    if (!conn) {
        fprintf(stderr, "Failed to create connection\n");
        return -1;
    }

    /* Feed data in chunks to simulate incremental reception */
    size_t chunk_size = 4096;
    size_t offset = 0;
    int total_frames = 0;

    while (offset < len) {
        size_t remaining = len - offset;
        size_t feed_size = remaining < chunk_size ? remaining : chunk_size;

        conn->clock_ms += 100; /* simulate time passing */
        int frames = smux_conn_feed(conn, data + offset, feed_size);
        if (frames > 0) total_frames += frames;
        offset += feed_size;

        if (verbose && frames > 0) {
            printf("[+%zu] Fed %zu bytes, %d frames processed\n",
                   offset, feed_size, frames);
        }
    }

    /* Print session state */
    printf("\n--- Replay Results ---\n");
    printf("Session state: %d\n", smux_conn_state(conn));
    printf("Total frames processed: %d\n", total_frames);

    /* Print diagnostics */
    char diag_buf[4096];
    int diag_len = smux_conn_get_stats(conn, diag_buf, sizeof(diag_buf));
    if (diag_len > 0) {
        printf("\nDiagnostics:\n%.*s", diag_len, diag_buf);
    }

    /* Print any pending output */
    size_t pending = smux_conn_pending(conn);
    if (pending > 0) {
        printf("Pending output: %zu bytes\n", pending);
    }

    /* Check for data available on channels */
    for (int ch = 1; ch < SMUX_MAX_CHANNELS; ch++) {
        int state = smux_conn_channel_state(conn, (uint16_t)ch);
        if (state != SMUX_CHAN_CLOSED) {
            uint8_t read_buf[4096];
            size_t nread = 0;
            int rc = smux_conn_channel_read(conn, (uint16_t)ch,
                                             read_buf, sizeof(read_buf), &nread);
            if (rc == SMUX_OK && nread > 0) {
                printf("Channel %d: %zu bytes available\n", ch, nread);
                if (verbose && nread <= 256) {
                    for (size_t i = 0; i < nread; i++)
                        printf("%02x ", read_buf[i]);
                    printf("\n");
                }
            }
        }
    }

    smux_conn_destroy(conn);
    return 0;
}

/* Read file and replay */
int smux_cli_replay_file(const char *filename, int verbose) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open: %s\n", filename);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0 || fsize > 10 * 1024 * 1024) {
        fclose(f);
        fprintf(stderr, "Invalid file size: %ld\n", fsize);
        return -1;
    }

    uint8_t *data = malloc((size_t)fsize);
    if (!data) {
        fclose(f);
        return -1;
    }

    size_t read_bytes = fread(data, 1, (size_t)fsize, f);
    fclose(f);

    int result = smux_cli_replay(data, read_bytes, verbose);
    free(data);
    return result;
}
