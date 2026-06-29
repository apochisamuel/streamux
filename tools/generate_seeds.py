#!/usr/bin/env python3
"""Generate seed corpus for libstreamux robustness validation harnesses.

Seeds contain valid SMUX protocol frames with correct magic, CRC32, and
structure to provide coverage starting points for the parser and
state machine.
"""
import struct
import os
import sys
import zlib

MAGIC = 0x534D5558  # "SMUX"
HEADER_SIZE = 20

# Frame types
HELLO      = 0x01
HELLO_ACK  = 0x02
DATA       = 0x10
FRAGMENT   = 0x11
FRAG_END   = 0x12
CLOSE      = 0x20
ERROR      = 0x21
WINDOW     = 0x30
PING       = 0x40
PONG       = 0x41
RECEIPT    = 0x50
RECEIPT_ACK= 0x51
MIGRATE    = 0x60
MIGRATE_ACK= 0x61
KEY_ROTATE = 0x70
COMPRESS   = 0x71
PRIORITY   = 0x80
BATCH      = 0x90

VERSION_MAJOR = 1
VERSION_MINOR = 0
DEFAULT_WINDOW = 65536

def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF

def build_frame(ftype: int, channel: int, seq: int, payload: bytes,
                flags: int = 0) -> bytes:
    """Build a complete SMUX frame with valid CRC."""
    header = struct.pack('>IBBHII',
        MAGIC,       # magic
        flags,       # flags
        ftype,       # type
        channel,     # channel_id
        seq,         # sequence
        len(payload) # payload_len
    )
    frame_data = header + payload
    checksum = crc32(frame_data)
    return frame_data + struct.pack('>I', checksum)

def hello_payload(window: int = DEFAULT_WINDOW) -> bytes:
    return struct.pack('>HHI', VERSION_MAJOR, VERSION_MINOR, window) + b'\x00' * 16

def hello_ack_payload(window: int = DEFAULT_WINDOW) -> bytes:
    return struct.pack('>HHI', VERSION_MAJOR, VERSION_MINOR, window) + b'\x00' * 16

def handshake(window: int = DEFAULT_WINDOW) -> bytes:
    h = build_frame(HELLO, 0, 1, hello_payload(window))
    a = build_frame(HELLO_ACK, 0, 2, hello_ack_payload(window))
    return h + a

def generate_seeds(out_dir: str):
    os.makedirs(out_dir, exist_ok=True)
    seeds = {}

    # 1. Handshake only
    seeds['01_handshake'] = handshake()

    # 2. Basic open/data session
    seeds['02_basic_data'] = (
        handshake() +
        build_frame(DATA, 1, 3, b'Hello SMUX channel data')
    )

    # 3. Fragmented complete message
    msg = b'A' * 200
    frag_meta = struct.pack('>HHII', 1, 3, len(msg), 0)
    seeds['03_frag_complete'] = (
        handshake() +
        build_frame(FRAGMENT, 1, 3, msg[:70], flags=0x04) +  # HAS_EXT
        build_frame(FRAGMENT, 1, 4, msg[70:140]) +
        build_frame(FRAG_END, 1, 5, msg[140:])
    )

    # 4. Partial fragmented message
    seeds['04_frag_partial'] = (
        handshake() +
        build_frame(FRAGMENT, 1, 3, b'partial_frag_data_chunk_one') +
        build_frame(FRAGMENT, 1, 4, b'partial_frag_data_chunk_two')
    )

    # 5. Open/data/close
    seeds['05_open_data_close'] = (
        handshake() +
        build_frame(DATA, 1, 3, b'data on channel 1') +
        build_frame(DATA, 1, 4, b'more data on channel 1') +
        build_frame(CLOSE, 1, 5, b'')
    )

    # 6. Multi-channel data
    seeds['06_multi_channel'] = (
        handshake() +
        build_frame(DATA, 1, 3, b'channel 1 payload') +
        build_frame(DATA, 2, 4, b'channel 2 payload') +
        build_frame(DATA, 3, 5, b'channel 3 payload') +
        build_frame(DATA, 4, 6, b'channel 4 payload')
    )

    # 7. Window update
    seeds['07_window_update'] = (
        handshake() +
        build_frame(DATA, 1, 3, b'data before window change') +
        build_frame(WINDOW, 1, 4, struct.pack('>I', 32768))
    )

    # 8. Migration after partial fragment
    seeds['08_migrate_frag'] = (
        handshake() +
        build_frame(DATA, 1, 3, b'pre-migration data') +
        build_frame(FRAGMENT, 1, 4, b'fragment before migrate') +
        build_frame(MIGRATE, 0, 5, struct.pack('>HHH', 0, 1, 1)) +
        build_frame(MIGRATE_ACK, 0, 6, b'')
    )

    # 9. Ping/keepalive path
    seeds['09_ping_pong'] = (
        handshake() +
        build_frame(PING, 0, 3, b'') +
        build_frame(PONG, 0, 4, b'') +
        build_frame(PING, 0, 5, b'')
    )

    # 10. Receipt/ack path
    seeds['10_receipt_ack'] = (
        handshake() +
        build_frame(DATA, 1, 3, b'data needing ack') +
        build_frame(RECEIPT, 1, 4, struct.pack('>I', 3)) +
        build_frame(RECEIPT_ACK, 1, 5, struct.pack('>I', 3))
    )

    # 11. Compressed frame path
    seeds['11_compressed'] = (
        handshake() +
        build_frame(DATA, 1, 3, b'compressed payload data', flags=0x01)
    )

    # 12. Replay/retransmission path
    seeds['12_retransmit'] = (
        handshake() +
        build_frame(DATA, 1, 3, b'original data frame') +
        build_frame(DATA, 1, 3, b'original data frame', flags=0x10)  # RETRANSMIT
    )

    # 13. Error frame
    seeds['13_error'] = (
        handshake() +
        build_frame(DATA, 1, 3, b'data before error') +
        build_frame(ERROR, 1, 4, struct.pack('>I', 0x0001))
    )

    # 14. Priority change
    seeds['14_priority'] = (
        handshake() +
        build_frame(DATA, 1, 3, b'low priority data') +
        build_frame(PRIORITY, 1, 4, struct.pack('BB', 1, 128))
    )

    # Write seeds to each harness directory
    for harness in ['channel_fuzzer', 'session_fuzzer', 'stream_fuzzer', 'migrate_fuzzer']:
        harness_dir = os.path.join(out_dir, harness)
        os.makedirs(harness_dir, exist_ok=True)
        for name, data in seeds.items():
            path = os.path.join(harness_dir, f'{name}.bin')
            with open(path, 'wb') as f:
                f.write(data)

    print(f'Generated {len(seeds)} seeds for 4 harnesses in {out_dir}/')
    return len(seeds)

if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else 'fuzz/corpus'
    n = generate_seeds(out)
    print(f'Total: {n * 4} seed files')
