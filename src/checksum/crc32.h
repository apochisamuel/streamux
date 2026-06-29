#ifndef SMUX_CHECKSUM_CRC32_H
#define SMUX_CHECKSUM_CRC32_H

#include <stdint.h>
#include <stddef.h>

uint32_t smux_crc32_init(void);
uint32_t smux_crc32_update(uint32_t crc, const uint8_t *data, size_t len);
uint32_t smux_crc32_finalize(uint32_t crc);
uint32_t smux_crc32(const uint8_t *data, size_t len);
int smux_crc32_verify(const uint8_t *data, size_t len, uint32_t expected);

/* Slicing-by-4 acceleration tables */
void smux_crc32_init_tables(void);

/* Combine two CRC values without re-scanning the data.
   Uses the GF(2) matrix-squaring method. */
uint32_t smux_crc32_combine(uint32_t crc1, uint32_t crc2, size_t len2);

#endif
