# Protocol Reference

## Named-Pipe Control Protocol

### Transport

| Property | Value |
|----------|-------|
| Pipe name | `\\.\pipe\vbusd` |
| Direction | Full-duplex (GENERIC_READ \| GENERIC_WRITE) |
| Encoding | UTF-8 text, newline-delimited |
| Framing | Client sends one command line (`\n` terminated). Daemon responds with one text block, then the client closes the handle. |

Commands and responses are **space-separated tokens**. Extra whitespace is ignored. Commands are case-sensitive (lowercase).

---

### Commands

#### `create <name> eth <link_bps>`

Creates a virtual Ethernet hub.

| Argument | Type | Description |
|----------|------|-------------|
| `name` | string | Unique bus identifier (e.g. `eth0`) |
| `link_bps` | uint64 | Link speed in bits/second (e.g. `1000000000` for 1 Gbps) |

Response: `OK created eth` or `ERR type`

#### `create <name> can <bitrate>`

Creates a virtual CAN bus.

| Argument | Type | Description |
|----------|------|-------------|
| `name` | string | Unique bus identifier (e.g. `can0`) |
| `bitrate` | uint32 | Bit rate in bits/second (e.g. `500000` for 500 kbps) |

Response: `OK created can` or `ERR type`

#### `delete <name>`

Destroys a named bus. Detaches any running recorder and removes the bus from the daemon.

Response: `OK deleted` or `ERR no bus`

#### `list`

Returns a newline-separated list of all bus names currently managed by the daemon.

Response:
```
eth0
can0
```

#### `record <name> on <file>`

Attaches a `Recorder` tap to the named bus. All subsequent frames forwarded on that bus are appended to `<file>` in VBUSCAP format.

| Argument | Type | Description |
|----------|------|-------------|
| `name` | string | Bus name |
| `file` | path | Absolute or relative path for the capture file |

Response: `OK rec on` or `ERR no bus`

#### `record <name> off`  /  `stoprec <name>`

Detaches the recorder tap and closes the file.

Response: `OK rec off` or `ERR no bus`

#### `send-eth <name> <hex>`

Injects a raw Ethernet frame payload into the named bus. All connected endpoints (except injector, which is `nullptr`) receive the frame.

| Argument | Type | Description |
|----------|------|-------------|
| `name` | string | Bus name |
| `hex` | hex string | Even-length lowercase/uppercase hex payload (e.g. `deadbeefcafe`) |

Response: `OK sent` or `ERR no bus`

#### `send-can <name> <id> <hex>`

Injects a CAN frame. Payload must be **≤ 8 bytes** (CAN 2.0 limit).

| Argument | Type | Description |
|----------|------|-------------|
| `name` | string | Bus name |
| `id` | uint32 (decimal or `0x` hex) | CAN frame identifier (11-bit or 29-bit) |
| `hex` | hex string | Frame payload (max 8 bytes) |

Response: `OK sent`, `ERR payload too long (max 8 bytes for CAN 2.0)`, or `ERR no bus`

#### `send-canfd <name> <id> <hex>`

Injects a CAN FD frame. Payload must be **≤ 64 bytes**.

| Argument | Type | Description |
|----------|------|-------------|
| `name` | string | Bus name |
| `id` | uint32 (decimal or `0x` hex) | CAN FD frame identifier |
| `hex` | hex string | Frame payload (max 64 bytes) |

Response: `OK sent`, `ERR payload too long (max 64 bytes for CAN FD)`, or `ERR no bus`

#### `stats <name>`

Returns live frame counters for the named bus.

Response: `tx=N rx=N drops=N`

#### `replay <name> <file> <mode>`

Opens a `.vbuscap` file and injects all recorded frames into the named bus.

| Argument | Type | Description |
|----------|------|-------------|
| `name` | string | Bus name |
| `file` | path | Capture file |
| `mode` | string | `exact`, `burst`, or `scale:K` where K is a float multiplier |

Response: `OK replay done` or `ERR ...`

#### `capture-udp <name> <bindport>`

Binds a UDP socket on `0.0.0.0:<bindport>` and forwards every received datagram into the named bus as a `Proto::UDP` frame. The frame `Tag` encodes the source address: `(src_ip_u32 << 32) | (src_port << 16) | dst_port`.

Response: `OK capturing udp` or `ERR ...`

#### `capture-tcp <name> <bindport> <targethost> <targetport>`

Starts a transparent TCP proxy: listens on `<bindport>`, forwards connections to `<targethost>:<targetport>`, and records every relayed chunk as a `Proto::TCP` frame. `Tag=0` marks client→server data; `Tag=1` marks server→client data.

Response: `OK capturing tcp` or `ERR ...`

#### `stop-capture <name>`

Stops any active UDP/TCP capture endpoint attached to the named bus without deleting the bus.

Response: `OK capture stopped` or `ERR ...`

#### `replay-udp <name> <file> <dsthost> <dstport> <mode>`

Opens a `.vbuscap` file and replays every `Proto::UDP` frame as a real UDP datagram to `<dsthost>:<dstport>`. Non-UDP frames are skipped.

| Argument | Type | Description |
|----------|------|-------------|
| `name` | string | Bus name |
| `file` | path | Capture file |
| `dsthost` | string | Destination IP address |
| `dstport` | uint16 | Destination UDP port |
| `mode` | string | `exact`, `burst`, or `scale:K` where K is a float multiplier |

Response: `OK replayed udp` or `ERR ...`

#### `replay-sync <mode> <file1> <host1> <port1> [<file2> <host2> <port2> ...]`

Replays **multiple `.vbuscap` files simultaneously** as UDP datagrams, preserving the exact inter-stream timing as originally captured.

All streams share a single global time origin: the minimum `Ts_ns` found across all capture files. Playback begins ~50 ms after the command is acknowledged, giving every worker thread time to reach the start barrier together.

Only `Proto::UDP` frames are replayed; all other frame types are silently skipped.

| Argument | Type | Description |
|----------|------|-------------|
| `mode` | string | `exact`, `burst`, or `scale:K` where K is a float multiplier |
| `file1` | path | Capture file for stream 1 |
| `host1` | string | Destination IP for stream 1 |
| `port1` | uint16 | Destination UDP port for stream 1 |
| `file2…` | — | Additional streams (repeat the file/host/port triple) |

**Modes:**

| Mode | Behaviour |
|------|----------|
| `exact` | Replays at original wall-clock spacing (default) |
| `burst` | Sends all frames as fast as possible (no timing) |
| `scale:K` | Multiplies all inter-frame gaps by K (K < 1 = faster, K > 1 = slower) |

Response: `OK sync started N streams` (immediately; background threads handle playback) or `ERR open failed: <path>`

**Example — 4 video streams recorded on ports 5001–5004, replayed to another host:**
```
replay-sync exact v1.vbuscap 192.168.1.42 5001 v2.vbuscap 192.168.1.42 5002 v3.vbuscap 192.168.1.42 5003 v4.vbuscap 192.168.1.42 5004
```

---

#### `quit`

Shuts down the daemon cleanly.

Response: `OK bye`

---

## VBUSCAP Binary Format

VBUSCAP (`.vbuscap`) is a simple sequential binary format with no compression. All multi-byte integers are **little-endian**.

### File Header

Appears once at byte offset 0:

```
Offset  Size  Type      Field
──────────────────────────────────────────────
0       8     char[8]   Magic = "VBUSCAP\0"
8       4     uint32    Version = 1
12      4     uint32    Reserved (write 0)
```

Total header size: **16 bytes**

### Record Header (VCapRec)

Each captured frame is preceded by a record header:

```
Offset  Size  Type      Field
──────────────────────────────────────────────
0       1     uint8     Proto  (1=ETH2, 2=CAN20, 3=CANFD, 4=UDP, 5=TCP)
1       1     uint8     Flags  (reserved, write 0)
2       2     uint16    Reserved (write 0)
4       8     uint64    Tag    (CAN identifier, or 0 for Ethernet)
12      8     uint64    Ts_ns  (delivery timestamp in nanoseconds)
20      4     uint32    Len    (payload byte count)
```

Total record header size: **24 bytes**

Immediately following the record header are `Len` bytes of raw payload.

### Reading a VBUSCAP File (pseudocode)

```
read VCapHeader (16 bytes); assert magic == "VBUSCAP\0"
while not EOF:
    read VCapRec (24 bytes)
    read rec.Len bytes as payload
    emit Frame{proto=rec.Proto, tag=rec.Tag, ts=rec.Ts_ns, payload}
```

### Proto Enum Values

| Name | Value | Description |
|------|-------|-------------|
| `ETH2` | 1 | IEEE 802.3 Ethernet II frame |
| `CAN20` | 2 | CAN 2.0A/B standard/extended frame (≤ 8 byte payload) |
| `CANFD` | 3 | CAN FD frame (≤ 64 byte payload) |
| `UDP` | 4 | UDP datagram; Tag = `(src_ip << 32) \| (src_port << 16) \| dst_port` |
| `TCP` | 5 | TCP relay chunk; Tag = 0 (client→server) or 1 (server→client) |

---

## Future: Shared-Memory Data Plane

The `vbus_client` C API (`vbus_open` / `vbus_close`) is currently a stub. The planned implementation will use a **shared-memory ring buffer** per bus:

```
vbusd process                   Client process
┌──────────────────┐            ┌──────────────────┐
│  TX ring head    │◄──mmap─────│  write frames    │
│  RX ring tail    │────mmap────►│  read frames     │
└──────────────────┘            └──────────────────┘
```

The control pipe will remain for bus lifecycle management. The data plane will be zero-copy via shared memory, bypassing the pipe entirely for high-frequency frame injection.
