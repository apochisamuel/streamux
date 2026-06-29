#include "../core/types.h"
#include "../core/byte_utils.h"
#include "../core/status.h"
#include "../frame/frame.h"
#include "../stream/stream.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* CLI main entry point: dispatches subcommands for frame
   inspection, encoding, decoding, and connection replay.
   Built as a standalone tool for protocol debugging. */

extern int smux_cli_inspect(const uint8_t *data, size_t len);
extern int smux_cli_decode(const uint8_t *data, size_t len,
                            uint8_t *payload_out, size_t out_cap, size_t *out_written,
                            int verbose);
extern int smux_cli_validate(const uint8_t *data, size_t len);
extern int smux_cli_replay(const uint8_t *data, size_t len, int verbose);
extern int smux_cli_replay_file(const char *filename, int verbose);

static void print_usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s <command> [options] [file]\n\n"
        "Commands:\n"
        "  inspect  <file>      Parse and display frame details\n"
        "  decode   <file>      Extract payload data from frames\n"
        "  validate <file>      Check frame integrity (CRC, structure)\n"
        "  replay   <file>      Replay frames through a connection\n"
        "  version              Print library version\n"
        "  stats    <file>      Print frame statistics summary\n\n"
        "Options:\n"
        "  -v, --verbose        Verbose output\n"
        "  -o <file>            Output file for decoded payload\n"
        "  -h, --help           Show this help message\n",
        prog);
}

static uint8_t *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open: %s\n", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0 || fsize > 10 * 1024 * 1024) {
        fclose(f);
        fprintf(stderr, "Invalid file size: %ld\n", fsize);
        return NULL;
    }

    uint8_t *data = malloc((size_t)fsize);
    if (!data) {
        fclose(f);
        return NULL;
    }

    *out_len = fread(data, 1, (size_t)fsize, f);
    fclose(f);
    return data;
}

static int cmd_inspect(const char *path) {
    size_t len = 0;
    uint8_t *data = read_file(path, &len);
    if (!data) return 1;
    int rc = smux_cli_inspect(data, len);
    free(data);
    return rc;
}

static int cmd_decode(const char *path, const char *out_path, int verbose) {
    size_t len = 0;
    uint8_t *data = read_file(path, &len);
    if (!data) return 1;

    size_t out_cap = len * 2;
    uint8_t *payload = malloc(out_cap);
    if (!payload) {
        free(data);
        return 1;
    }

    size_t written = 0;
    int frames = smux_cli_decode(data, len, payload, out_cap, &written, verbose);
    free(data);

    if (frames > 0 && out_path) {
        FILE *f = fopen(out_path, "wb");
        if (f) {
            fwrite(payload, 1, written, f);
            fclose(f);
            printf("Wrote %zu bytes to %s\n", written, out_path);
        }
    }

    free(payload);
    return frames > 0 ? 0 : 1;
}

static int cmd_validate(const char *path) {
    size_t len = 0;
    uint8_t *data = read_file(path, &len);
    if (!data) return 1;
    int rc = smux_cli_validate(data, len);
    free(data);
    return rc;
}

static int cmd_replay(const char *path, int verbose) {
    return smux_cli_replay_file(path, verbose);
}

static int cmd_stats(const char *path) {
    size_t len = 0;
    uint8_t *data = read_file(path, &len);
    if (!data) return 1;

    /* Count frame types */
    int type_counts[256];
    memset(type_counts, 0, sizeof(type_counts));
    int total = 0;
    size_t offset = 0;

    while (offset + SMUX_MIN_FRAME_SIZE <= len) {
        uint32_t magic = smux_read_u32_be(data + offset);
        if (magic != SMUX_MAGIC) { offset++; continue; }

        smux_frame_t frame;
        smux_status_t rc = smux_frame_parse(data + offset, len - offset, &frame);
        if (rc == SMUX_OK) {
            type_counts[frame.type]++;
            total++;
            offset += smux_frame_wire_size(&frame);
        } else {
            offset++;
        }
    }

    printf("Frame statistics for %s (%zu bytes):\n\n", path, len);
    printf("Total frames: %d\n\n", total);

    for (int t = 0; t < 256; t++) {
        if (type_counts[t] > 0) {
            smux_frame_t dummy;
            dummy.type = (uint8_t)t;
            printf("  %-16s: %d\n", smux_frame_type_name(dummy.type), type_counts[t]);
        }
    }

    free(data);
    return 0;
}

static void cmd_version(void) {
    printf("libstreamux %u.%u\n", SMUX_VERSION_MAJOR, SMUX_VERSION_MINOR);
    printf("Max channels: %d\n", SMUX_MAX_CHANNELS);
    printf("Max frame size: %d\n", SMUX_MAX_FRAME_SIZE);
    printf("Default window: %d\n", SMUX_DEFAULT_WINDOW);
}

int smux_cli_main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *cmd = argv[1];
    int verbose = 0;
    const char *out_path = NULL;
    const char *in_path = NULL;

    /* Parse options */
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            in_path = argv[i];
        }
    }

    if (strcmp(cmd, "version") == 0) {
        cmd_version();
        return 0;
    }

    if (!in_path) {
        fprintf(stderr, "Error: no input file specified\n");
        print_usage(argv[0]);
        return 1;
    }

    if (strcmp(cmd, "inspect") == 0)  return cmd_inspect(in_path);
    if (strcmp(cmd, "decode") == 0)   return cmd_decode(in_path, out_path, verbose);
    if (strcmp(cmd, "validate") == 0) return cmd_validate(in_path);
    if (strcmp(cmd, "replay") == 0)   return cmd_replay(in_path, verbose);
    if (strcmp(cmd, "stats") == 0)    return cmd_stats(in_path);

    fprintf(stderr, "Unknown command: %s\n", cmd);
    print_usage(argv[0]);
    return 1;
}
