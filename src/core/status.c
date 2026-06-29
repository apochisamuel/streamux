#include "status.h"

static const struct {
    smux_status_t code;
    const char   *name;
    int           fatal;
} status_table[] = {
    { SMUX_OK,                  "ok",                   0 },
    { SMUX_ERR_NOMEM,           "out of memory",        1 },
    { SMUX_ERR_INVALID,         "invalid argument",     0 },
    { SMUX_ERR_BAD_MAGIC,       "bad magic",            0 },
    { SMUX_ERR_BAD_CRC,         "bad crc",              0 },
    { SMUX_ERR_BAD_STATE,       "bad state",            0 },
    { SMUX_ERR_BAD_SEQ,         "bad sequence",         0 },
    { SMUX_ERR_BAD_CHANNEL,     "bad channel",          0 },
    { SMUX_ERR_BAD_TYPE,        "bad frame type",       0 },
    { SMUX_ERR_BAD_LENGTH,      "bad length",           0 },
    { SMUX_ERR_BAD_VERSION,     "bad version",          1 },
    { SMUX_ERR_CHANNEL_FULL,    "channel table full",   0 },
    { SMUX_ERR_WINDOW_FULL,     "window full",          0 },
    { SMUX_ERR_REASSEMBLY_FULL, "reassembly full",      0 },
    { SMUX_ERR_FRAG_ORDER,      "fragment order",       0 },
    { SMUX_ERR_FRAG_OVERFLOW,   "fragment overflow",    0 },
    { SMUX_ERR_NO_ROUTE,        "no route",             0 },
    { SMUX_ERR_ROUTE_FULL,      "route table full",     0 },
    { SMUX_ERR_ENCRYPT,         "encryption error",     1 },
    { SMUX_ERR_DECRYPT,         "decryption error",     0 },
    { SMUX_ERR_COMPRESS,        "compression error",    0 },
    { SMUX_ERR_DECOMPRESS,      "decompression error",  0 },
    { SMUX_ERR_TIMEOUT,         "timeout",              0 },
    { SMUX_ERR_CLOSED,          "connection closed",    1 },
    { SMUX_ERR_DRAINING,        "draining",             0 },
    { SMUX_ERR_DUPLICATE,       "duplicate",            0 },
    { SMUX_ERR_REPLAY,          "replay detected",      0 },
    { SMUX_ERR_TRUNCATED,       "truncated frame",      0 },
    { SMUX_ERR_OVERFLOW,        "buffer overflow",      1 },
    { SMUX_ERR_BATCH_FULL,      "batch full",           0 },
    { SMUX_ERR_KEY_EXPIRED,     "key expired",          0 },
    { SMUX_ERR_PRIORITY,        "priority error",       0 },
    { SMUX_ERR_MIGRATING,       "migrating",            0 },
    { SMUX_ERR_INTERNAL,        "internal error",       1 },
};

#define STATUS_COUNT (sizeof(status_table) / sizeof(status_table[0]))

const char *smux_status_str(smux_status_t status) {
    for (unsigned i = 0; i < STATUS_COUNT; i++) {
        if (status_table[i].code == status)
            return status_table[i].name;
    }
    return "unknown error";
}

int smux_status_is_fatal(smux_status_t status) {
    for (unsigned i = 0; i < STATUS_COUNT; i++) {
        if (status_table[i].code == status)
            return status_table[i].fatal;
    }
    return 1;
}

int smux_status_is_transient(smux_status_t status) {
    switch (status) {
        case SMUX_ERR_WINDOW_FULL:
        case SMUX_ERR_TIMEOUT:
        case SMUX_ERR_DRAINING:
        case SMUX_ERR_MIGRATING:
        case SMUX_ERR_BATCH_FULL:
            return 1;
        default:
            return 0;
    }
}
