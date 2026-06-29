#ifndef SMUX_FRAME_H
#define SMUX_FRAME_H

#include "../core/types.h"
#include "../core/status.h"

/* Parse a raw byte buffer into a frame structure.
   Returns SMUX_OK on success. The frame payload pointer
   references into the raw buffer (zero-copy). */
smux_status_t smux_frame_parse(const uint8_t *raw, size_t raw_len,
                                smux_frame_t *frame);

/* Validate frame semantics after parsing (type-specific checks) */
smux_status_t smux_frame_validate(const smux_frame_t *frame);

/* Serialize a frame to a buffer. Returns bytes written or negative error.
   Caller provides buf with at least smux_frame_wire_size(frame) bytes. */
int smux_frame_serialize(const smux_frame_t *frame, uint8_t *buf, size_t buf_len);

/* Compute the wire size of a frame including header, extensions, payload, CRC */
size_t smux_frame_wire_size(const smux_frame_t *frame);

/* Initialize a frame structure with defaults */
void smux_frame_init(smux_frame_t *frame, uint8_t type, uint16_t channel_id);

/* Extension handling */
int smux_frame_add_extension(smux_frame_t *frame, uint16_t type,
                             const uint8_t *data, uint16_t len);
const smux_extension_t *smux_frame_find_extension(const smux_frame_t *frame,
                                                   uint16_t type);

/* Frame type classification */
int smux_frame_is_control(uint8_t type);
int smux_frame_is_data(uint8_t type);
int smux_frame_requires_channel(uint8_t type);
int smux_frame_requires_session(uint8_t type);
const char *smux_frame_type_name(uint8_t type);

/* Header extension types */
#define SMUX_EXT_TIMESTAMP  0x0001
#define SMUX_EXT_TRACE_ID   0x0002
#define SMUX_EXT_PRIORITY   0x0003
#define SMUX_EXT_ROUTE_TAG  0x0004
#define SMUX_EXT_FRAG_META  0x0005
#define SMUX_EXT_COMPRESS   0x0006
#define SMUX_EXT_KEY_ID     0x0007
#define SMUX_EXT_CUSTOM     0x00FF

#endif
