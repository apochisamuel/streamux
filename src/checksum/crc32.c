#include "crc32.h"

static uint32_t crc_table[4][256];
static int tables_ready = 0;

void smux_crc32_init_tables(void) {
    if (tables_ready) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++) {
            if (c & 1)
                c = 0xEDB88320u ^ (c >> 1);
            else
                c >>= 1;
        }
        crc_table[0][i] = c;
    }
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = crc_table[0][i];
        for (int s = 1; s < 4; s++) {
            c = crc_table[0][c & 0xFF] ^ (c >> 8);
            crc_table[s][i] = c;
        }
    }
    tables_ready = 1;
}

uint32_t smux_crc32_init(void) {
    smux_crc32_init_tables();
    return 0xFFFFFFFFu;
}

uint32_t smux_crc32_update(uint32_t crc, const uint8_t *data, size_t len) {
    smux_crc32_init_tables();
    /* Process 4 bytes at a time using slicing-by-4 */
    while (len >= 4) {
        uint32_t val = crc ^ ((uint32_t)data[0] | ((uint32_t)data[1] << 8) |
                              ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24));
        crc = crc_table[3][val & 0xFF] ^
              crc_table[2][(val >> 8) & 0xFF] ^
              crc_table[1][(val >> 16) & 0xFF] ^
              crc_table[0][(val >> 24) & 0xFF];
        data += 4;
        len -= 4;
    }
    while (len-- > 0) {
        crc = crc_table[0][(crc ^ *data++) & 0xFF] ^ (crc >> 8);
    }
    return crc;
}

uint32_t smux_crc32_finalize(uint32_t crc) {
    return crc ^ 0xFFFFFFFFu;
}

uint32_t smux_crc32(const uint8_t *data, size_t len) {
    return smux_crc32_finalize(smux_crc32_update(smux_crc32_init(), data, len));
}

int smux_crc32_verify(const uint8_t *data, size_t len, uint32_t expected) {
    return smux_crc32(data, len) == expected;
}

/* GF(2) matrix multiply for CRC combination */
static uint32_t gf2_matrix_times(const uint32_t *mat, uint32_t vec) {
    uint32_t sum = 0;
    int i = 0;
    while (vec) {
        if (vec & 1) sum ^= mat[i];
        vec >>= 1;
        i++;
    }
    return sum;
}

static void gf2_matrix_square(uint32_t *square, const uint32_t *mat) {
    for (int n = 0; n < 32; n++)
        square[n] = gf2_matrix_times(mat, mat[n]);
}

uint32_t smux_crc32_combine(uint32_t crc1, uint32_t crc2, size_t len2) {
    uint32_t even[32], odd[32];

    if (len2 == 0) return crc1;

    /* Put operator for one zero bit in odd */
    odd[0] = 0xEDB88320u;
    uint32_t row = 1;
    for (int n = 1; n < 32; n++) {
        odd[n] = row;
        row <<= 1;
    }

    /* Put operator for two zero bits in even */
    gf2_matrix_square(even, odd);
    gf2_matrix_square(odd, even);

    /* Apply len2 zeros to crc1 */
    size_t remaining = len2;
    while (remaining != 0) {
        gf2_matrix_square(even, odd);
        if (remaining & 1)
            crc1 = gf2_matrix_times(even, crc1);
        remaining >>= 1;
        if (remaining == 0) break;
        gf2_matrix_square(odd, even);
        if (remaining & 1)
            crc1 = gf2_matrix_times(odd, crc1);
        remaining >>= 1;
    }
    return crc1 ^ crc2;
}
