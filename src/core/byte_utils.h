#ifndef SMUX_CORE_BYTE_UTILS_H
#define SMUX_CORE_BYTE_UTILS_H

#include <stdint.h>
#include <stddef.h>

uint16_t smux_read_u16_be(const uint8_t *buf);
uint32_t smux_read_u32_be(const uint8_t *buf);
uint64_t smux_read_u64_be(const uint8_t *buf);
void smux_write_u16_be(uint8_t *buf, uint16_t val);
void smux_write_u32_be(uint8_t *buf, uint32_t val);
void smux_write_u64_be(uint8_t *buf, uint64_t val);

int smux_safe_add_u32(uint32_t a, uint32_t b, uint32_t *result);
int smux_safe_mul_u32(uint32_t a, uint32_t b, uint32_t *result);
uint32_t smux_align_up(uint32_t val, uint32_t align);
uint32_t smux_min_u32(uint32_t a, uint32_t b);
uint32_t smux_max_u32(uint32_t a, uint32_t b);
uint32_t smux_clamp_u32(uint32_t val, uint32_t lo, uint32_t hi);

int smux_seq_lt(uint32_t a, uint32_t b);
int smux_seq_le(uint32_t a, uint32_t b);
int smux_seq_gt(uint32_t a, uint32_t b);
uint32_t smux_seq_diff(uint32_t a, uint32_t b);
int smux_seq_in_window(uint32_t seq, uint32_t base, uint32_t window);

void smux_mem_xor(uint8_t *dst, const uint8_t *a, const uint8_t *b, size_t len);
void smux_mem_zero(void *ptr, size_t len);
int smux_mem_is_zero(const void *ptr, size_t len);
uint32_t smux_hash_bytes(const uint8_t *data, size_t len);
uint32_t smux_rotate_left(uint32_t val, int bits);

#endif
