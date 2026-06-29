#include "api.h"
#include <stdlib.h>
#include <string.h>

smux_mux_t *smux_mux_create(int capacity) {
    smux_mux_t *mux = calloc(1, sizeof(smux_mux_t));
    if (!mux) return NULL;
    mux->capacity = capacity > 32 ? 32 : capacity;
    return mux;
}

void smux_mux_destroy(smux_mux_t *mux) {
    if (!mux) return;
    for (int i = 0; i < mux->count; i++) {
        if (mux->connections[i]) {
            smux_managed_destroy(mux->connections[i]);
        }
    }
    free(mux);
}

int smux_mux_add(smux_mux_t *mux, smux_managed_t *m) {
    if (!mux || !m) return SMUX_ERR_INVALID;
    if (mux->count >= mux->capacity) return SMUX_ERR_OVERFLOW;
    mux->connections[mux->count++] = m;
    return SMUX_OK;
}

int smux_mux_remove(smux_mux_t *mux, int index) {
    if (!mux || index < 0 || index >= mux->count) return SMUX_ERR_INVALID;
    mux->connections[index] = mux->connections[mux->count - 1];
    mux->connections[mux->count - 1] = NULL;
    mux->count--;
    if (mux->rr_index >= mux->count) mux->rr_index = 0;
    return SMUX_OK;
}

smux_managed_t *smux_mux_get(smux_mux_t *mux, int index) {
    if (!mux || index < 0 || index >= mux->count) return NULL;
    return mux->connections[index];
}

int smux_mux_tick(smux_mux_t *mux, uint64_t now_ms) {
    if (!mux) return 0;
    int total = 0;
    mux->tick = now_ms;
    for (int i = 0; i < mux->count; i++) {
        if (mux->connections[i]) {
            int rc = smux_managed_tick(mux->connections[i], now_ms);
            if (rc > 0) total += rc;
        }
    }
    return total;
}

int smux_mux_select_next(smux_mux_t *mux) {
    if (!mux || mux->count == 0) return -1;
    int start = mux->rr_index;
    do {
        mux->rr_index = (mux->rr_index + 1) % mux->count;
        if (mux->connections[mux->rr_index] &&
            smux_managed_is_connected(mux->connections[mux->rr_index])) {
            return mux->rr_index;
        }
    } while (mux->rr_index != start);
    return -1;
}
