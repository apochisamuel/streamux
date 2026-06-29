#include "types.h"
#include "status.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>

/* Structured logging system for libstreamux.
   Supports level filtering, per-subsystem tags, and ring buffer history. */

typedef enum {
    SMUX_LOG_TRACE = 0,
    SMUX_LOG_DEBUG = 1,
    SMUX_LOG_INFO  = 2,
    SMUX_LOG_WARN  = 3,
    SMUX_LOG_ERROR = 4,
    SMUX_LOG_FATAL = 5,
    SMUX_LOG_OFF   = 6
} smux_log_level_t;

typedef struct {
    uint64_t    timestamp;
    uint8_t     level;
    uint16_t    subsystem;
    uint16_t    channel_id;
    uint32_t    sequence;
    char        message[256];
} smux_log_entry_t;

typedef struct {
    smux_log_entry_t entries[512];
    int              head;
    int              count;
    int              capacity;
    smux_log_level_t min_level;
    uint32_t         subsystem_mask;
    smux_log_fn      output_fn;
    void            *output_ctx;
    uint64_t         total_entries;
} smux_logger_t;

/* Subsystem identifiers for filtering */
#define SMUX_SUB_CORE       0x0001
#define SMUX_SUB_FRAME      0x0002
#define SMUX_SUB_SESSION    0x0004
#define SMUX_SUB_CHANNEL    0x0008
#define SMUX_SUB_REASSEMBLY 0x0010
#define SMUX_SUB_FLOW       0x0020
#define SMUX_SUB_RECEIPT    0x0040
#define SMUX_SUB_ROUTE      0x0080
#define SMUX_SUB_COMPRESS   0x0100
#define SMUX_SUB_CRYPTO     0x0200
#define SMUX_SUB_SCHEDULER  0x0400
#define SMUX_SUB_TRANSCRIPT 0x0800
#define SMUX_SUB_STREAM     0x1000
#define SMUX_SUB_API        0x2000
#define SMUX_SUB_ALL        0xFFFF

static const char *level_names[] = {
    "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL", "OFF"
};

static const char *subsystem_names[] = {
    "core", "frame", "session", "channel", "reassembly", "flow",
    "receipt", "route", "compress", "crypto", "scheduler", "transcript",
    "stream", "api"
};

smux_logger_t *smux_logger_create(smux_log_level_t min_level) {
    smux_logger_t *log = calloc(1, sizeof(smux_logger_t));
    if (!log) return NULL;
    log->capacity = 512;
    log->min_level = min_level;
    log->subsystem_mask = SMUX_SUB_ALL;
    return log;
}

void smux_logger_destroy(smux_logger_t *log) {
    free(log);
}

void smux_logger_set_output(smux_logger_t *log, smux_log_fn fn, void *ctx) {
    if (!log) return;
    log->output_fn = fn;
    log->output_ctx = ctx;
}

void smux_logger_set_level(smux_logger_t *log, smux_log_level_t level) {
    if (log) log->min_level = level;
}

void smux_logger_set_subsystem_mask(smux_logger_t *log, uint32_t mask) {
    if (log) log->subsystem_mask = mask;
}

static int subsystem_bit_to_index(uint16_t sub) {
    for (int i = 0; i < 14; i++) {
        if (sub == (1u << i)) return i;
    }
    return 0;
}

void smux_log(smux_logger_t *log, smux_log_level_t level, uint16_t subsystem,
              uint16_t channel_id, uint32_t sequence, const char *fmt, ...) {
    if (!log) return;
    if (level < log->min_level) return;
    if (!(log->subsystem_mask & subsystem)) return;

    smux_log_entry_t *entry = &log->entries[log->head];
    entry->timestamp = log->total_entries; /* simplified clock */
    entry->level = (uint8_t)level;
    entry->subsystem = subsystem;
    entry->channel_id = channel_id;
    entry->sequence = sequence;

    va_list args;
    va_start(args, fmt);
    vsnprintf(entry->message, sizeof(entry->message), fmt, args);
    va_end(args);

    log->head = (log->head + 1) % log->capacity;
    if (log->count < log->capacity) log->count++;
    log->total_entries++;

    /* Forward to output callback if set */
    if (log->output_fn) {
        char full_msg[384];
        int sub_idx = subsystem_bit_to_index(subsystem);
        int n = snprintf(full_msg, sizeof(full_msg), "[%s][%s] ch=%u seq=%u: %s",
                        level_names[level], subsystem_names[sub_idx],
                        channel_id, sequence, entry->message);
        if (n > 0) {
            log->output_fn(log->output_ctx, (int)level, full_msg);
        }
    }
}

int smux_logger_dump(const smux_logger_t *log, char *buf, size_t buf_len) {
    if (!log || !buf || buf_len == 0) return 0;
    int written = 0;
    int remaining = (int)buf_len;

    int start = log->count < log->capacity ? 0 :
                (log->head) % log->capacity;
    for (int i = 0; i < log->count && remaining > 0; i++) {
        int idx = (start + i) % log->capacity;
        const smux_log_entry_t *e = &log->entries[idx];
        int sub_idx = subsystem_bit_to_index(e->subsystem);

        int n = snprintf(buf + written, (size_t)remaining,
                        "%05llu [%s][%s] ch=%u seq=%u: %s\n",
                        (unsigned long long)e->timestamp,
                        level_names[e->level],
                        subsystem_names[sub_idx],
                        e->channel_id, e->sequence,
                        e->message);
        if (n < 0 || n >= remaining) break;
        written += n;
        remaining -= n;
    }
    return written;
}

int smux_logger_dump_level(const smux_logger_t *log, smux_log_level_t level,
                            char *buf, size_t buf_len) {
    if (!log || !buf || buf_len == 0) return 0;
    int written = 0;
    int remaining = (int)buf_len;

    int start = log->count < log->capacity ? 0 : log->head;
    for (int i = 0; i < log->count && remaining > 0; i++) {
        int idx = (start + i) % log->capacity;
        const smux_log_entry_t *e = &log->entries[idx];
        if (e->level < (uint8_t)level) continue;

        int sub_idx = subsystem_bit_to_index(e->subsystem);
        int n = snprintf(buf + written, (size_t)remaining,
                        "[%s][%s] %s\n",
                        level_names[e->level],
                        subsystem_names[sub_idx],
                        e->message);
        if (n < 0 || n >= remaining) break;
        written += n;
        remaining -= n;
    }
    return written;
}

int smux_logger_count_level(const smux_logger_t *log, smux_log_level_t level) {
    if (!log) return 0;
    int count = 0;
    int start = log->count < log->capacity ? 0 : log->head;
    for (int i = 0; i < log->count; i++) {
        int idx = (start + i) % log->capacity;
        if (log->entries[idx].level == (uint8_t)level) count++;
    }
    return count;
}
