#include "compress.h"
#include <stdlib.h>
#include <string.h>

void smux_dict_init(smux_dict_t *dict, uint32_t size) {
    if (!dict) return;
    memset(dict, 0, sizeof(*dict));
    dict->window_size = size;
    dict->window = calloc(1, size);
    if (dict->window) {
        dict->initialized = true;
        memset(dict->hash_head, -1, sizeof(dict->hash_head));
        memset(dict->hash_chain, -1, sizeof(dict->hash_chain));
    }
}

void smux_dict_reset(smux_dict_t *dict) {
    if (!dict || !dict->initialized) return;
    memset(dict->window, 0, dict->window_size);
    dict->window_pos = 0;
    dict->window_fill = 0;
    dict->hash_val = 0;
    memset(dict->hash_head, -1, sizeof(dict->hash_head));
    memset(dict->hash_chain, -1, sizeof(dict->hash_chain));
}

uint32_t smux_dict_hash3(const uint8_t *data) {
    /* 3-byte rolling hash for LZ match finder */
    uint32_t h = ((uint32_t)data[0] << 10) ^ ((uint32_t)data[1] << 5) ^ data[2];
    return h & SMUX_LZ_HASH_MASK;
}

void smux_dict_insert_hash(smux_dict_t *dict, uint32_t pos) {
    if (!dict || !dict->initialized) return;
    if (pos + 2 >= dict->window_fill) return;

    uint32_t h = smux_dict_hash3(dict->window + pos);
    uint32_t idx = pos % SMUX_DICT_SIZE;
    dict->hash_chain[idx] = dict->hash_head[h];
    dict->hash_head[h] = (int16_t)idx;
}

void smux_dict_update(smux_dict_t *dict, const uint8_t *data, size_t len) {
    if (!dict || !dict->initialized || !data) return;

    for (size_t i = 0; i < len; i++) {
        uint32_t pos = dict->window_pos;
        dict->window[pos % dict->window_size] = data[i];

        if (dict->window_fill >= 2 && pos >= 2) {
            smux_dict_insert_hash(dict, pos - 2);
        }

        dict->window_pos++;
        if (dict->window_fill < dict->window_size)
            dict->window_fill++;
    }
}

int smux_dict_find_match(smux_dict_t *dict, const uint8_t *data, size_t len,
                          uint32_t *match_offset, uint32_t *match_length) {
    if (!dict || !dict->initialized || !data || len < SMUX_LZ_MIN_MATCH)
        return 0;
    if (dict->window_fill < SMUX_LZ_MIN_MATCH) return 0;

    uint32_t h = smux_dict_hash3(data);
    int16_t chain_idx = dict->hash_head[h];
    uint32_t best_len = SMUX_LZ_MIN_MATCH - 1;
    uint32_t best_off = 0;
    int chain_count = 0;

    while (chain_idx >= 0 && chain_count < SMUX_LZ_CHAIN_LEN) {
        uint32_t pos = (uint32_t)chain_idx;
        uint32_t cur_pos = dict->window_pos % dict->window_size;

        /* Calculate offset from current position */
        uint32_t offset;
        if (cur_pos > pos) {
            offset = cur_pos - pos;
        } else {
            offset = dict->window_size - pos + cur_pos;
        }

        if (offset == 0 || offset > dict->window_fill) goto next;

        /* Compare bytes */
        uint32_t match = 0;
        size_t max_match = len < SMUX_LZ_MAX_MATCH ? len : SMUX_LZ_MAX_MATCH;
        while (match < max_match) {
            uint32_t dict_pos = (pos + match) % dict->window_size;
            if (dict->window[dict_pos] != data[match]) break;
            match++;
        }

        if (match > best_len) {
            best_len = match;
            best_off = offset;
            if (best_len >= SMUX_LZ_MAX_MATCH) break;
        }

next:
        chain_idx = dict->hash_chain[pos % SMUX_DICT_SIZE];
        chain_count++;
    }

    if (best_len >= SMUX_LZ_MIN_MATCH) {
        *match_offset = best_off;
        *match_length = best_len;
        return 1;
    }
    return 0;
}
