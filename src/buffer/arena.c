#include "buffer.h"
#include "../core/byte_utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Buffer recycling bin: avoids frequent malloc/free for common sizes.
   Maintains freelists for power-of-2 sized buffers. */

#define RECYCLE_BINS    8   /* 64, 128, 256, 512, 1K, 2K, 4K, 8K */
#define RECYCLE_MIN     64
#define RECYCLE_MAX     8192
#define RECYCLE_PER_BIN 16

typedef struct smux_recycle_entry {
    uint8_t                     *data;
    size_t                       capacity;
    struct smux_recycle_entry   *next;
} smux_recycle_entry_t;

typedef struct {
    smux_recycle_entry_t *bins[RECYCLE_BINS];
    int                   counts[RECYCLE_BINS];
    uint64_t              hits;
    uint64_t              misses;
    uint64_t              total_allocs;
    uint64_t              total_frees;
    size_t                bytes_cached;
} smux_buffer_recycler_t;

static int size_to_bin(size_t size) {
    if (size <= 64) return 0;
    if (size <= 128) return 1;
    if (size <= 256) return 2;
    if (size <= 512) return 3;
    if (size <= 1024) return 4;
    if (size <= 2048) return 5;
    if (size <= 4096) return 6;
    if (size <= 8192) return 7;
    return -1;
}

static size_t bin_to_size(int bin) {
    return (size_t)RECYCLE_MIN << bin;
}

smux_buffer_recycler_t *smux_recycler_create(void) {
    return calloc(1, sizeof(smux_buffer_recycler_t));
}

void smux_recycler_destroy(smux_buffer_recycler_t *r) {
    if (!r) return;
    for (int i = 0; i < RECYCLE_BINS; i++) {
        smux_recycle_entry_t *e = r->bins[i];
        while (e) {
            smux_recycle_entry_t *next = e->next;
            free(e->data);
            free(e);
            e = next;
        }
    }
    free(r);
}

uint8_t *smux_recycler_alloc(smux_buffer_recycler_t *r, size_t size) {
    if (!r) return malloc(size);
    r->total_allocs++;

    int bin = size_to_bin(size);
    if (bin >= 0 && r->bins[bin]) {
        /* Recycle from bin */
        smux_recycle_entry_t *e = r->bins[bin];
        r->bins[bin] = e->next;
        r->counts[bin]--;
        r->bytes_cached -= e->capacity;
        r->hits++;

        uint8_t *data = e->data;
        free(e);
        return data;
    }

    /* Fresh allocation */
    r->misses++;
    size_t actual = bin >= 0 ? bin_to_size(bin) : size;
    return malloc(actual);
}

void smux_recycler_free(smux_buffer_recycler_t *r, uint8_t *data, size_t size) {
    if (!r || !data) {
        free(data);
        return;
    }
    r->total_frees++;

    int bin = size_to_bin(size);
    if (bin >= 0 && r->counts[bin] < RECYCLE_PER_BIN) {
        /* Return to bin */
        smux_recycle_entry_t *e = malloc(sizeof(smux_recycle_entry_t));
        if (e) {
            e->data = data;
            e->capacity = bin_to_size(bin);
            e->next = r->bins[bin];
            r->bins[bin] = e;
            r->counts[bin]++;
            r->bytes_cached += e->capacity;
            return;
        }
    }

    /* Bin full or oversized */
    free(data);
}

float smux_recycler_hit_rate(const smux_buffer_recycler_t *r) {
    if (!r || r->total_allocs == 0) return 0.0f;
    return (float)r->hits / (float)r->total_allocs;
}

size_t smux_recycler_cached_bytes(const smux_buffer_recycler_t *r) {
    return r ? r->bytes_cached : 0;
}

int smux_recycler_report(const smux_buffer_recycler_t *r, char *buf, size_t len) {
    if (!r || !buf || len == 0) return 0;
    int written = snprintf(buf, len,
        "Buffer recycler: allocs=%llu frees=%llu hits=%llu misses=%llu (%.1f%%)\n"
        "Cached: %zu bytes across bins\n",
        (unsigned long long)r->total_allocs, (unsigned long long)r->total_frees,
        (unsigned long long)r->hits, (unsigned long long)r->misses,
        smux_recycler_hit_rate(r) * 100.0f,
        r->bytes_cached);

    int remaining = (int)len - written;
    for (int i = 0; i < RECYCLE_BINS && remaining > 0; i++) {
        int n = snprintf(buf + written, (size_t)remaining,
                        "  [%zu B]: %d entries\n", bin_to_size(i), r->counts[i]);
        if (n > 0 && n < remaining) {
            written += n;
            remaining -= n;
        }
    }
    return written;
}
