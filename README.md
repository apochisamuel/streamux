# libstreamux

An embeddable C library for multiplexed binary message transport over byte streams.

libstreamux provides reliable, ordered delivery of messages across multiple logical channels over a single byte stream. It handles session negotiation, frame parsing, fragmentation and reassembly, flow control, receipt tracking, route migration, optional compression, and optional encryption.

## Features

- **Session handshake** with version negotiation and capability exchange
- **Logical channels** — multiplex many independent streams over one connection
- **Binary frame parser/writer** with CRC32 integrity validation
- **Fragmentation and reassembly** for messages exceeding frame size limits
- **Flow control** with sliding window, backpressure, and credit-based scheduling
- **Receipt tracking** with bitmap acknowledgment and replay detection
- **Retransmission** with exponential backoff retry queue
- **Route migration** — move channels between transport paths without data loss
- **Optional compression** using LZ77 with sliding window dictionary
- **Optional encryption** using stream cipher with key rotation protocol
- **Transcript logging** with slab-based storage, compaction, and binary export
- **Diagnostics** with per-counter stats, health scoring, and invariant checks
- **CLI tools** for frame inspection, encoding, decoding, and connection replay
- **Zero external dependencies** — pure C11, no third-party libraries

## Building

```bash
make all        # Build libstreamux.a
make test       # Run unit tests
make asan       # Build with AddressSanitizer
make harnesses  # Build robustness validation harnesses
```

Requires a C11-capable compiler (GCC or Clang).

## API Example

```c
#include "stream/stream.h"

int main(void) {
    /* Create connection */
    smux_conn_t *conn = smux_conn_create();
    smux_conn_set_window(conn, 65536);

    /* Start handshake */
    smux_conn_start(conn);

    /* Open a channel */
    smux_conn_channel_open(conn, 1);

    /* Feed received data from the transport */
    uint8_t recv_buf[4096];
    size_t recv_len = read_from_transport(recv_buf, sizeof(recv_buf));
    smux_conn_feed(conn, recv_buf, recv_len);

    /* Read assembled data from channel */
    uint8_t data[4096];
    size_t nread = 0;
    smux_conn_channel_read(conn, 1, data, sizeof(data), &nread);

    /* Flush pending output to transport */
    uint8_t send_buf[8192];
    size_t written = 0;
    smux_conn_flush(conn, send_buf, sizeof(send_buf), &written);
    write_to_transport(send_buf, written);

    smux_conn_destroy(conn);
    return 0;
}
```

## CLI Example

```bash
# Inspect a binary capture
./smux_cli inspect capture.bin

# Decode and extract payloads
./smux_cli decode capture.bin -o payloads.bin -v

# Validate frame integrity
./smux_cli validate capture.bin

# Replay through a connection
./smux_cli replay capture.bin -v
```

## Frame Format

```
+--------+-------+------+-----------+----------+-------------+----------+
| Magic  | Flags | Type | ChannelID | Sequence | PayloadLen  | Payload  | CRC32  |
| 4B     | 1B    | 1B   | 2B        | 4B       | 4B          | variable | 4B     |
+--------+-------+------+-----------+----------+-------------+----------+--------+
```

- **Magic**: `0x534D5558` ("SMUX")
- **Flags**: COMPRESSED(0x01), ENCRYPTED(0x02), HAS_EXT(0x04), URGENT(0x08), RETRANSMIT(0x10), FINAL(0x20)
- **Type**: HELLO, HELLO_ACK, DATA, FRAGMENT, FRAG_END, CLOSE, ERROR, WINDOW, PING, PONG, RECEIPT, RECEIPT_ACK, MIGRATE, MIGRATE_ACK, KEY_ROTATE, COMPRESS, PRIORITY, BATCH
- **CRC32**: computed over magic through payload (exclusive of CRC field)

## Channel Lifecycle

| State     | Transitions To        | Triggered By              |
|-----------|-----------------------|---------------------------|
| CLOSED    | OPEN                  | Channel open request      |
| OPEN      | DRAINING, ERROR       | Close request, error      |
| DRAINING  | CLOSED                | All pending data flushed  |
| ERROR     | CLOSED                | Protocol/application error|
| MIGRATING | OPEN                  | Migration complete        |

## Protocol Subsystems

| Module      | Responsibility                              |
|-------------|---------------------------------------------|
| core        | Types, status codes, byte utilities, logging|
| checksum    | CRC32 (slicing-by-4), Adler-32             |
| buffer      | Slab pool, slices, refcounting, recycling   |
| frame       | Parser, serializer, extensions, validation  |
| session     | Handshake FSM, negotiation, keepalive       |
| channel     | Table, lifecycle, delivery ordering         |
| reassembly  | Fragment tracking, bitmask-based assembly   |
| flow        | Window, backpressure, credit scheduling     |
| receipt     | Ack tracking, replay detection, retransmit  |
| route       | Routing table, migration FSM, handoff       |
| compression | LZ77 with hash chains, RLE, adapter        |
| crypto      | Stream cipher, key rotation, HMAC auth      |
| scheduler   | Timer wheel, priority queue, delayed queue  |
| transcript  | Event storage, compaction, binary export    |
| diagnostics | Statistics, invariants, health scoring      |
| stream      | Connection management, dispatch, policy     |
| cli         | Inspect, encode, decode, replay tools       |
| api         | Managed connections, multiplexer, pool      |

## Testing

```bash
make test                          # Run unit tests
python3 tools/generate_seeds.py    # Generate seed corpus
make harnesses                     # Build validation harnesses
```

## Design Notes

libstreamux is designed for embedding in applications that need reliable multiplexed communication over unreliable or capacity-limited byte streams. The protocol is self-framing with CRC integrity, so it can operate over raw TCP, serial links, pipes, or any ordered byte transport.

The library is fully synchronous and non-blocking. The caller drives time via `smux_conn_set_clock()` and I/O via `smux_conn_feed()` / `smux_conn_flush()`. No threads, no event loops, no system calls.

Memory management uses a slab pool for frame buffers and an arena allocator for burst processing. The library makes no heap allocations during steady-state frame processing after initial setup.

## License

MIT License. See [LICENSE](LICENSE) for details.
