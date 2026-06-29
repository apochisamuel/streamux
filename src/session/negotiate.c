#include "session.h"
#include "../core/byte_utils.h"
#include <string.h>

/* Session negotiation: version selection, capability exchange,
   and window size negotiation between peers. */

typedef struct {
    uint16_t    version_major;
    uint16_t    version_minor;
    uint32_t    max_window;
    uint32_t    max_channels;
    uint32_t    max_frame_size;
    uint8_t     capabilities;
    bool        supports_compression;
    bool        supports_encryption;
    bool        supports_migration;
    bool        supports_batching;
    bool        supports_priority;
} smux_negotiation_params_t;

#define SMUX_CAP_COMPRESS   0x01
#define SMUX_CAP_ENCRYPT    0x02
#define SMUX_CAP_MIGRATE    0x04
#define SMUX_CAP_BATCH      0x08
#define SMUX_CAP_PRIORITY   0x10

void smux_negotiate_default(smux_negotiation_params_t *params) {
    if (!params) return;
    memset(params, 0, sizeof(*params));
    params->version_major = SMUX_VERSION_MAJOR;
    params->version_minor = SMUX_VERSION_MINOR;
    params->max_window = SMUX_DEFAULT_WINDOW;
    params->max_channels = SMUX_MAX_CHANNELS;
    params->max_frame_size = SMUX_MAX_FRAME_SIZE;
    params->supports_compression = true;
    params->supports_encryption = true;
    params->supports_migration = true;
    params->supports_batching = true;
    params->supports_priority = true;
    params->capabilities = SMUX_CAP_COMPRESS | SMUX_CAP_ENCRYPT |
                           SMUX_CAP_MIGRATE | SMUX_CAP_BATCH | SMUX_CAP_PRIORITY;
}

/* Encode negotiation parameters into HELLO payload */
int smux_negotiate_encode(const smux_negotiation_params_t *params,
                           uint8_t *buf, size_t len) {
    if (!params || !buf || len < 20) return SMUX_ERR_OVERFLOW;

    smux_write_u16_be(buf, params->version_major);
    smux_write_u16_be(buf + 2, params->version_minor);
    smux_write_u32_be(buf + 4, params->max_window);
    smux_write_u32_be(buf + 8, params->max_channels);
    smux_write_u32_be(buf + 12, params->max_frame_size);
    buf[16] = params->capabilities;
    buf[17] = 0; /* reserved */
    buf[18] = 0;
    buf[19] = 0;

    return 20;
}

/* Decode negotiation parameters from received HELLO */
int smux_negotiate_decode(const uint8_t *buf, size_t len,
                           smux_negotiation_params_t *params) {
    if (!buf || !params || len < 20) return SMUX_ERR_BAD_LENGTH;

    params->version_major = smux_read_u16_be(buf);
    params->version_minor = smux_read_u16_be(buf + 2);
    params->max_window = smux_read_u32_be(buf + 4);
    params->max_channels = smux_read_u32_be(buf + 8);
    params->max_frame_size = smux_read_u32_be(buf + 12);
    params->capabilities = buf[16];

    params->supports_compression = !!(params->capabilities & SMUX_CAP_COMPRESS);
    params->supports_encryption = !!(params->capabilities & SMUX_CAP_ENCRYPT);
    params->supports_migration = !!(params->capabilities & SMUX_CAP_MIGRATE);
    params->supports_batching = !!(params->capabilities & SMUX_CAP_BATCH);
    params->supports_priority = !!(params->capabilities & SMUX_CAP_PRIORITY);

    return SMUX_OK;
}

/* Negotiate between local and remote parameters */
int smux_negotiate_resolve(const smux_negotiation_params_t *local,
                            const smux_negotiation_params_t *remote,
                            smux_negotiation_params_t *result) {
    if (!local || !remote || !result) return SMUX_ERR_INVALID;

    /* Version: use minimum of both */
    result->version_major = local->version_major < remote->version_major ?
                            local->version_major : remote->version_major;
    if (local->version_major == remote->version_major) {
        result->version_minor = local->version_minor < remote->version_minor ?
                                local->version_minor : remote->version_minor;
    } else {
        result->version_minor = result->version_major == local->version_major ?
                                local->version_minor : remote->version_minor;
    }

    /* Window: minimum of both */
    result->max_window = local->max_window < remote->max_window ?
                         local->max_window : remote->max_window;

    /* Channels: minimum */
    result->max_channels = local->max_channels < remote->max_channels ?
                           local->max_channels : remote->max_channels;

    /* Frame size: minimum */
    result->max_frame_size = local->max_frame_size < remote->max_frame_size ?
                             local->max_frame_size : remote->max_frame_size;

    /* Capabilities: intersection (both must support) */
    result->capabilities = local->capabilities & remote->capabilities;
    result->supports_compression = !!(result->capabilities & SMUX_CAP_COMPRESS);
    result->supports_encryption = !!(result->capabilities & SMUX_CAP_ENCRYPT);
    result->supports_migration = !!(result->capabilities & SMUX_CAP_MIGRATE);
    result->supports_batching = !!(result->capabilities & SMUX_CAP_BATCH);
    result->supports_priority = !!(result->capabilities & SMUX_CAP_PRIORITY);

    return SMUX_OK;
}

/* Apply negotiated parameters to session */
int smux_negotiate_apply(smux_session_t *session,
                          const smux_negotiation_params_t *params) {
    if (!session || !params) return SMUX_ERR_INVALID;

    session->negotiated_window = params->max_window;
    session->remote_version_major = params->version_major;
    session->remote_version_minor = params->version_minor;
    session->compression_enabled = params->supports_compression;
    session->encryption_enabled = params->supports_encryption;
    /* batch support tracked via capabilities */

    return SMUX_OK;
}

/* Validate version compatibility */
int smux_negotiate_version_ok(uint16_t local_major, uint16_t remote_major) {
    /* Same major version required */
    if (local_major != remote_major) return 0;
    return 1;
}

/* Check if window size is acceptable */
int smux_negotiate_window_ok(uint32_t proposed, uint32_t min_acceptable) {
    return proposed >= min_acceptable;
}
