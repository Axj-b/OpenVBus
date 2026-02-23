# vbus_bench – Traffic Generator & Sink

`vbus_bench` is a standalone UDP/TCP traffic generator and passive sink for testing OpenVBus capture and recording at realistic line rates.

---

## Building

`vbus_bench` is part of the `vbus` CMake project and built automatically alongside `vbusd`:

```powershell
cmake --build build --target vbus_bench --config Debug
# Output: build\vbus\Debug\vbus_bench.exe
```

---

## Usage

```
vbus_bench udp  <host> <port> <rate> [duration_sec]
vbus_bench tcp  <host> <port> <rate> [duration_sec]
vbus_bench recv <port>
```

| Argument | Description |
|---|---|
| `host` | Destination IP (e.g. `127.0.0.1`) |
| `port` | Destination UDP/TCP port |
| `rate` | Target bitrate — suffix `g` (Gbit/s), `m` (Mbit/s), `k` (kbit/s), or plain bits/s |
| `duration_sec` | How long to run in seconds. Omit or set `0` for indefinite (Ctrl-C to stop) |

### Rate format examples

| Input | Interpreted as |
|---|---|
| `1g` | 1 000 000 000 bit/s |
| `10g` | 10 000 000 000 bit/s |
| `500m` | 500 000 000 bit/s |
| `100k` | 100 000 bit/s |
| `1500000` | 1 500 000 bit/s |

---

## Modes

### `udp` — UDP Sender

Sends a continuous stream of UDP datagrams to `host:port` at the specified rate using a **token-bucket** pacer.

```powershell
# 1 Gbit/s for 30 seconds
.\build\vbus\Debug\vbus_bench.exe udp 127.0.0.1 9000 1g 30

# 10 Gbit/s until Ctrl-C
.\build\vbus\Debug\vbus_bench.exe udp 127.0.0.1 9000 10g
```

### `tcp` — TCP Sender

Connects to `host:port` and streams data at the specified rate. The receiving end must already be listening (use `recv` mode, or configure a `capture-tcp` proxy in vbusd).

```powershell
# 1 Gbit/s through a vbus TCP proxy (bind port 9002 → sink on 9001)
.\build\vbus\Debug\vbus_bench.exe tcp 127.0.0.1 9002 1g 30
```

### `recv` — Passive Sink

Binds a UDP socket and a TCP listen socket on the same port and counts all incoming bytes/packets without forwarding. Useful as:
- A traffic sink when testing the TCP proxy
- A loopback endpoint to verify the sender is working

```powershell
.\build\vbus\Debug\vbus_bench.exe recv 9001
```

---

## Live Statistics

Every second a stats line is printed:

```
Sec    Pkts/s          Bits/s     Total MB   Errors
------  ----------  ------------  ----------  --------
[UDP] 1      85431       948.7M       11.86         0
[UDP] 2      85612       952.3M       23.76         0
```

A final summary line is printed when the sender exits:

```
Done. Total: 356.40 MB  (0.95 Gbit/s avg)
```

---

## Payload Sizes

Payload size is chosen automatically based on the requested rate to minimise per-packet overhead:

| Rate | Payload size |
|---|---|
| > 5 Gbit/s | 65 000 B |
| > 500 Mbit/s | 16 384 B |
| > 50 Mbit/s | 4 096 B |
| ≤ 50 Mbit/s | 1 400 B (near Ethernet MTU) |

---

## End-to-End Test Scenarios

### Scenario 1 — Record localhost UDP traffic

```powershell
# 1. Start daemon
.\build\vbus\Debug\vbusd.exe

# 2. Open GUI, create bus "test", Capture → UDP, bind port 9000,
#    Recording → set output path "C:\test.vbuscap" → Start recording

# 3. Generate 1 Gbit/s for 10 seconds
.\build\vbus\Debug\vbus_bench.exe udp 127.0.0.1 9000 1g 10

# 4. Stop recording in GUI → test.vbuscap is written
# 5. Replay → "exact" injects frames back into the bus
```

### Scenario 2 — Record TCP traffic through a proxy

```powershell
# 1. Start a passive sink on port 9001
.\build\vbus\Debug\vbus_bench.exe recv 9001

# 2. In GUI: create bus "tcptest", Capture → TCP proxy
#    bind port = 9002, target = 127.0.0.1:9001 → Attach
#    Recording → set output path → Start recording

# 3. Send traffic through the proxy
.\build\vbus\Debug\vbus_bench.exe tcp 127.0.0.1 9002 500m 20

# 4. Stop recording → replay with "burst" to push frames as fast as possible
```

### Scenario 3 — Stress test at 10 Gbit/s

```powershell
# Note: actual throughput is limited by loopback socket buffer and
# Windows TCP/UDP stack. Expect ~5-9 Gbit/s on a modern machine.
.\build\vbus\Debug\vbus_bench.exe udp 127.0.0.1 9000 10g 5
```
