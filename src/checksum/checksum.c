#include "checksum.h"
#include "crc32.h"
#include "../core/byte_utils.h"
#include "../core/types.h"

#define ADLER_MOD 65521u
#define ADLER_NMAX 5552u

uint32_t smux_adler32(const uint8_t *data, size_t len) {
    return smux_adler32_update(1, data, len);
}

uint32_t smux_adler32_update(uint32_t adler, const uint8_t *data, size_t len) {
    uint32_t s1 = adler & 0xFFFF;
    uint32_t s2 = (adler >> 16) & 0xFFFF;

    while (len > 0) {
        size_t chunk = len > ADLER_NMAX ? ADLER_NMAX : len;
        len -= chunk;
        while (chunk >= 16) {
            for (int i = 0; i < 16; i++) {
                s1 += *data++;
                s2 += s1;
            }
            chunk -= 16;
        }
        while (chunk-- > 0) {
            s1 += *data++;
            s2 += s1;
        }
        s1 %= ADLER_MOD;
        s2 %= ADLER_MOD;
    }
    return (s2 << 16) | s1;
}

uint32_t smux_adler32_combine(uint32_t adler1, uint32_t adler2, size_t len2) {
    uint32_t s1_1 = adler1 & 0xFFFF;
    uint32_t s2_1 = (adler1 >> 16) & 0xFFFF;
    uint32_t s1_2 = adler2 & 0xFFFF;
    uint32_t s2_2 = (adler2 >> 16) & 0xFFFF;

    uint32_t s1 = (s1_1 + s1_2 - 1) % ADLER_MOD;
    uint32_t s2 = (s2_1 + s2_2 + s1_1 * (uint32_t)(len2 % ADLER_MOD) - 1) % ADLER_MOD;
    /* Handle modular subtraction underflow */
    if (s1 >= ADLER_MOD) s1 += ADLER_MOD;
    if (s2 >= ADLER_MOD) s2 += ADLER_MOD;
    return (s2 << 16) | s1;
}

uint32_t smux_frame_checksum(const uint8_t *header, size_t hdr_len,
                             const uint8_t *payload, size_t pay_len) {
    uint32_t crc = smux_crc32_init();
    crc = smux_crc32_update(crc, header, hdr_len);
    if (payload && pay_len > 0)
        crc = smux_crc32_update(crc, payload, pay_len);
    return smux_crc32_finalize(crc);
}

int smux_frame_verify(const uint8_t *raw, size_t raw_len) {
    if (raw_len < SMUX_MIN_FRAME_SIZE) return 0;

    uint32_t magic = smux_read_u32_be(raw);
    if (magic != SMUX_MAGIC) return 0;

    uint32_t payload_len = smux_read_u32_be(raw + 12);
    size_t expected = SMUX_HEADER_SIZE + payload_len + SMUX_CRC_SIZE;
    if (raw_len < expected) return 0;

    uint32_t stored_crc = smux_read_u32_be(raw + SMUX_HEADER_SIZE + payload_len);
    uint32_t computed = smux_frame_checksum(raw, SMUX_HEADER_SIZE, 
                                           raw + SMUX_HEADER_SIZE, payload_len);
    return computed == stored_crc;
}
