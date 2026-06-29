#include "byte_utils.h"
#include <string.h>

uint16_t smux_read_u16_be(const uint8_t *buf) {
    return (uint16_t)((buf[0] << 8) | buf[1]);
}

uint32_t smux_read_u32_be(const uint8_t *buf) {
    return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8)  |  (uint32_t)buf[3];
}

uint64_t smux_read_u64_be(const uint8_t *buf) {
    uint64_t hi = smux_read_u32_be(buf);
    uint64_t lo = smux_read_u32_be(buf + 4);
    return (hi << 32) | lo;
}

void smux_write_u16_be(uint8_t *buf, uint16_t val) {
    buf[0] = (uint8_t)(val >> 8);
    buf[1] = (uint8_t)(val & 0xFF);
}

void smux_write_u32_be(uint8_t *buf, uint32_t val) {
    buf[0] = (uint8_t)(val >> 24);
    buf[1] = (uint8_t)(val >> 16);
    buf[2] = (uint8_t)(val >> 8);
    buf[3] = (uint8_t)(val);
}

void smux_write_u64_be(uint8_t *buf, uint64_t val) {
    smux_write_u32_be(buf, (uint32_t)(val >> 32));
    smux_write_u32_be(buf + 4, (uint32_t)(val & 0xFFFFFFFFu));
}

int smux_safe_add_u32(uint32_t a, uint32_t b, uint32_t *result) {
    if (a > UINT32_MAX - b) return -1;
    *result = a + b;
    return 0;
}

int smux_safe_mul_u32(uint32_t a, uint32_t b, uint32_t *result) {
    if (a != 0 && b > UINT32_MAX / a) return -1;
    *result = a * b;
    return 0;
}

uint32_t smux_align_up(uint32_t val, uint32_t align) {
    if (align == 0) return val;
    uint32_t rem = val % align;
    if (rem == 0) return val;
    return val + (align - rem);
}

uint32_t smux_min_u32(uint32_t a, uint32_t b) {
    return a < b ? a : b;
}

uint32_t smux_max_u32(uint32_t a, uint32_t b) {
    return a > b ? a : b;
}

uint32_t smux_clamp_u32(uint32_t val, uint32_t lo, uint32_t hi) {
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}

/* Sequence arithmetic using signed comparison on the diff.
   Handles wraparound correctly for sequences within 2^31 of each other. */
int smux_seq_lt(uint32_t a, uint32_t b) {
    return (int32_t)(a - b) < 0;
}

int smux_seq_le(uint32_t a, uint32_t b) {
    return a == b || smux_seq_lt(a, b);
}

int smux_seq_gt(uint32_t a, uint32_t b) {
    return (int32_t)(a - b) > 0;
}

uint32_t smux_seq_diff(uint32_t a, uint32_t b) {
    return a - b;
}

int smux_seq_in_window(uint32_t seq, uint32_t base, uint32_t window) {
    uint32_t dist = seq - base;
    return dist < window;
}

void smux_mem_xor(uint8_t *dst, const uint8_t *a, const uint8_t *b, size_t len) {
    for (size_t i = 0; i < len; i++) {
        dst[i] = a[i] ^ b[i];
    }
}

void smux_mem_zero(void *ptr, size_t len) {
    memset(ptr, 0, len);
}

int smux_mem_is_zero(const void *ptr, size_t len) {
    const uint8_t *p = (const uint8_t *)ptr;
    for (size_t i = 0; i < len; i++) {
        if (p[i] != 0) return 0;
    }
    return 1;
}

uint32_t smux_hash_bytes(const uint8_t *data, size_t len) {
    uint32_t h = 0x811c9dc5u;
    for (size_t i = 0; i < len; i++) {
        h ^= data[i];
        h *= 0x01000193u;
    }
    return h;
}

uint32_t smux_rotate_left(uint32_t val, int bits) {
    bits &= 31;
    return (val << bits) | (val >> (32 - bits));
}
