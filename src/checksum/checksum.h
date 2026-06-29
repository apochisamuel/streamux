#ifndef SMUX_CHECKSUM_H
#define SMUX_CHECKSUM_H

#include <stdint.h>
#include <stddef.h>

/* Adler-32 as a lighter-weight alternative checksum */
uint32_t smux_adler32(const uint8_t *data, size_t len);
uint32_t smux_adler32_update(uint32_t adler, const uint8_t *data, size_t len);
uint32_t smux_adler32_combine(uint32_t adler1, uint32_t adler2, size_t len2);

/* Frame integrity: compute CRC over header+payload */
uint32_t smux_frame_checksum(const uint8_t *header, size_t hdr_len,
                             const uint8_t *payload, size_t pay_len);

/* Verify a complete raw frame including its trailing CRC */
int smux_frame_verify(const uint8_t *raw, size_t raw_len);

#endif
