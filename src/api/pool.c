#include "api.h"
#include <stdlib.h>
#include <string.h>

smux_conn_pool_t *smux_conn_pool_create(int capacity, const smux_config_t *cfg) {
    smux_conn_pool_t *pool = calloc(1, sizeof(smux_conn_pool_t));
    if (!pool) return NULL;

    pool->pool = calloc((size_t)capacity, sizeof(smux_managed_t *));
    pool->free_list = calloc((size_t)capacity, sizeof(int));
    if (!pool->pool || !pool->free_list) {
        free(pool->pool);
        free(pool->free_list);
        free(pool);
        return NULL;
    }

    pool->capacity = capacity;
    if (cfg) {
        pool->default_config = *cfg;
    } else {
        smux_config_default(&pool->default_config);
    }

    /* Pre-allocate all connections */
    for (int i = 0; i < capacity; i++) {
        pool->pool[i] = smux_managed_create(&pool->default_config);
        if (!pool->pool[i]) {
            /* Cleanup on failure */
            for (int j = 0; j < i; j++) {
                smux_managed_destroy(pool->pool[j]);
            }
            free(pool->pool);
            free(pool->free_list);
            free(pool);
            return NULL;
        }
        pool->free_list[pool->free_count++] = i;
    }

    pool->size = capacity;
    return pool;
}

void smux_conn_pool_destroy(smux_conn_pool_t *pool) {
    if (!pool) return;
    for (int i = 0; i < pool->size; i++) {
        smux_managed_destroy(pool->pool[i]);
    }
    free(pool->pool);
    free(pool->free_list);
    free(pool);
}

smux_managed_t *smux_conn_pool_acquire(smux_conn_pool_t *pool) {
    if (!pool || pool->free_count == 0) return NULL;
    int idx = pool->free_list[--pool->free_count];
    return pool->pool[idx];
}

int smux_conn_pool_release(smux_conn_pool_t *pool, smux_managed_t *m) {
    if (!pool || !m) return SMUX_ERR_INVALID;
    /* Find index */
    for (int i = 0; i < pool->size; i++) {
        if (pool->pool[i] == m) {
            /* Reset the connection for reuse */
            smux_conn_destroy(m->conn);
            m->conn = smux_conn_create();
            if (!m->conn) return SMUX_ERR_NOMEM;
            smux_conn_set_window(m->conn, pool->default_config.default_window);
            m->handshake_done = false;
            m->open_channels = 0;

            pool->free_list[pool->free_count++] = i;
            return SMUX_OK;
        }
    }
    return SMUX_ERR_INVALID;
}

int smux_conn_pool_active_count(const smux_conn_pool_t *pool) {
    if (!pool) return 0;
    return pool->size - pool->free_count;
}
