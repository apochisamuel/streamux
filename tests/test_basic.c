#include "core/types.h"
#include "core/status.h"
#include "core/byte_utils.h"
#include "checksum/crc32.h"
#include "checksum/checksum.h"
#include "frame/frame.h"
#include "buffer/buffer.h"
#include "session/session.h"
#include "channel/channel.h"
#include "stream/stream.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  %s... ", #name); \
    if (test_##name()) { tests_passed++; printf("OK\n"); } \
    else { printf("FAIL\n"); } \
} while(0)

static int test_byte_utils(void) {
    uint8_t buf[8];
    smux_write_u16_be(buf, 0xABCD);
    if (smux_read_u16_be(buf) != 0xABCD) return 0;
    smux_write_u32_be(buf, 0x12345678);
    if (smux_read_u32_be(buf) != 0x12345678) return 0;
    smux_write_u64_be(buf, 0xDEADBEEFCAFEBABEULL);
    if (smux_read_u64_be(buf) != 0xDEADBEEFCAFEBABEULL) return 0;
    return 1;
}

static int test_crc32(void) {
    uint8_t data[] = "Hello, SMUX!";
    uint32_t crc = smux_crc32(data, sizeof(data) - 1);
    if (crc == 0) return 0;
    /* Verify determinism */
    uint32_t crc2 = smux_crc32(data, sizeof(data) - 1);
    return crc == crc2;
}

static int test_hash(void) {
    uint32_t h1 = smux_hash_bytes((const uint8_t *)"abc", 3);
    uint32_t h2 = smux_hash_bytes((const uint8_t *)"def", 3);
    if (h1 == h2) return 0;
    uint32_t h3 = smux_hash_bytes((const uint8_t *)"abc", 3);
    return h1 == h3;
}

static int test_frame_init(void) {
    smux_frame_t frame;
    smux_frame_init(&frame, SMUX_FRAME_DATA, 42);
    if (frame.magic != SMUX_MAGIC) return 0;
    if (frame.type != SMUX_FRAME_DATA) return 0;
    if (frame.channel_id != 42) return 0;
    return 1;
}

static int test_frame_roundtrip(void) {
    smux_frame_t frame;
    smux_frame_init(&frame, SMUX_FRAME_DATA, 1);
    frame.sequence = 100;
    uint8_t payload[] = "test payload data";
    frame.payload = payload;
    frame.payload_len = sizeof(payload) - 1;

    uint8_t wire[512];
    int written = smux_frame_serialize(&frame, wire, sizeof(wire));
    if (written <= 0) return 0;

    smux_frame_t parsed;
    smux_status_t rc = smux_frame_parse(wire, (size_t)written, &parsed);
    if (rc != SMUX_OK) return 0;
    if (parsed.type != SMUX_FRAME_DATA) return 0;
    if (parsed.channel_id != 1) return 0;
    if (parsed.sequence != 100) return 0;
    if (parsed.payload_len != sizeof(payload) - 1) return 0;
    return 1;
}

static int test_pool_create(void) {
    smux_pool_t *pool = smux_pool_create(4);
    if (!pool) return 0;
    smux_slab_t *slab = smux_pool_acquire(pool);
    if (!slab) { smux_pool_destroy(pool); return 0; }
    smux_pool_release(pool, slab);
    smux_pool_destroy(pool);
    return 1;
}

static int test_session_create(void) {
    smux_pool_t *pool = smux_pool_create(4);
    smux_session_t *s = smux_session_create(pool);
    if (!s) { smux_pool_destroy(pool); return 0; }
    if (s->state != SMUX_SESS_IDLE) { smux_session_destroy(s); smux_pool_destroy(pool); return 0; }
    smux_session_destroy(s);
    smux_pool_destroy(pool);
    return 1;
}

static int test_channel_table(void) {
    smux_pool_t *pool = smux_pool_create(4);
    smux_channel_table_t *ct = smux_channel_table_create(pool);
    if (!ct) { smux_pool_destroy(pool); return 0; }
    smux_status_t rc = smux_channel_open(ct, 1, 4096, 0);
    if (rc != SMUX_OK) { smux_channel_table_destroy(ct); smux_pool_destroy(pool); return 0; }
    smux_channel_t *ch = smux_channel_table_get(ct, 1);
    if (!ch || ch->state != SMUX_CHAN_OPEN) { smux_channel_table_destroy(ct); smux_pool_destroy(pool); return 0; }
    smux_channel_table_destroy(ct);
    smux_pool_destroy(pool);
    return 1;
}

static int test_conn_lifecycle(void) {
    smux_conn_t *conn = smux_conn_create();
    if (!conn) return 0;
    if (smux_conn_state(conn) != SMUX_SESS_IDLE) { smux_conn_destroy(conn); return 0; }
    smux_conn_channel_open(conn, 1);
    int state = smux_conn_channel_state(conn, 1);
    if (state != SMUX_CHAN_OPEN) { smux_conn_destroy(conn); return 0; }
    smux_conn_channel_close(conn, 1);
    smux_conn_destroy(conn);
    return 1;
}

static int test_conn_feed_garbage(void) {
    smux_conn_t *conn = smux_conn_create();
    if (!conn) return 0;
    uint8_t garbage[256];
    memset(garbage, 0xFF, sizeof(garbage));
    /* Should not crash on garbage input */
    smux_conn_feed(conn, garbage, sizeof(garbage));
    smux_conn_destroy(conn);
    return 1;
}

static int test_status_strings(void) {
    const char *s = smux_status_str(SMUX_OK);
    if (!s || strlen(s) == 0) return 0;
    s = smux_status_str(SMUX_ERR_INVALID);
    if (!s || strlen(s) == 0) return 0;
    return 1;
}

int main(void) {
    printf("libstreamux basic tests\n");
    TEST(byte_utils);
    TEST(crc32);
    TEST(hash);
    TEST(frame_init);
    TEST(frame_roundtrip);
    TEST(pool_create);
    TEST(session_create);
    TEST(channel_table);
    TEST(conn_lifecycle);
    TEST(conn_feed_garbage);
    TEST(status_strings);
    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
