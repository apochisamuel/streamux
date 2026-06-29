#ifndef SMUX_CORE_STATUS_H
#define SMUX_CORE_STATUS_H

typedef enum {
    SMUX_OK                     =  0,
    SMUX_ERR_NOMEM              = -1,
    SMUX_ERR_INVALID            = -2,
    SMUX_ERR_BAD_MAGIC          = -3,
    SMUX_ERR_BAD_CRC            = -4,
    SMUX_ERR_BAD_STATE          = -5,
    SMUX_ERR_BAD_SEQ            = -6,
    SMUX_ERR_BAD_CHANNEL        = -7,
    SMUX_ERR_BAD_TYPE           = -8,
    SMUX_ERR_BAD_LENGTH         = -9,
    SMUX_ERR_BAD_VERSION        = -10,
    SMUX_ERR_CHANNEL_FULL       = -11,
    SMUX_ERR_WINDOW_FULL        = -12,
    SMUX_ERR_REASSEMBLY_FULL    = -13,
    SMUX_ERR_FRAG_ORDER         = -14,
    SMUX_ERR_FRAG_OVERFLOW      = -15,
    SMUX_ERR_NO_ROUTE           = -16,
    SMUX_ERR_ROUTE_FULL         = -17,
    SMUX_ERR_ENCRYPT            = -18,
    SMUX_ERR_DECRYPT            = -19,
    SMUX_ERR_COMPRESS           = -20,
    SMUX_ERR_DECOMPRESS         = -21,
    SMUX_ERR_TIMEOUT            = -22,
    SMUX_ERR_CLOSED             = -23,
    SMUX_ERR_DRAINING           = -24,
    SMUX_ERR_DUPLICATE          = -25,
    SMUX_ERR_REPLAY             = -26,
    SMUX_ERR_TRUNCATED          = -27,
    SMUX_ERR_OVERFLOW           = -28,
    SMUX_ERR_BATCH_FULL         = -29,
    SMUX_ERR_KEY_EXPIRED        = -30,
    SMUX_ERR_PRIORITY           = -31,
    SMUX_ERR_MIGRATING          = -32,
    SMUX_ERR_RATE_LIMITED       = -22,
    SMUX_ERR_TOO_MANY           = -23,
    SMUX_ERR_INTERNAL           = -99
} smux_status_t;

const char *smux_status_str(smux_status_t status);
int smux_status_is_fatal(smux_status_t status);
int smux_status_is_transient(smux_status_t status);

#endif
