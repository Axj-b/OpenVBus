# OpenVBus — Implementation TODO

Tasks are grouped by component and ordered by priority. Each item links to the affected file(s).

---

## 🔴 Bugs (broken today)

- [ ] **stoprec command missing from daemon**
  `handle_cmd` only handles `record <name> off`; `stoprec <name>` hits `ERR cmd`.
  → `vbus/src/daemon/vbusd.cpp`

- [ ] **Replay timing origin mismatch**
  `exact` mode compares capture-epoch absolute timestamps against the daemon's `RealtimeClock` which starts at 0 on launch. Should compute inter-frame deltas, not compare absolute values.
  → `vbus/src/daemon/vbusd.cpp` (replay block)

- [ ] **Replay `scale:K` not parsed**
  Only `mode == "exact"` is checked; `burst` and `scale:K` are silent no-ops (fallthrough).
  → `vbus/src/daemon/vbusd.cpp` (replay block)

- [ ] **`quit` calls `ExitProcess(0)` — no cleanup**
  Open `Recorder` files may not be flushed; pipe threads are not joined.
  Replace with a shutdown flag + condition variable.
  → `vbus/src/daemon/vbusd.cpp`

- [ ] **Packet timestamps not monotonic in GUI mock**
  `p.timestamp_ns += dt * 1e9` increments from 0 on every new `Packet` struct. Need a global accumulator in `Model::tick()`.
  → `vbus-gui/src/model.cpp`

- [ ] **Filter rules never applied**
  `Bus::filters` is populated by the filter panel but `Model::tick()` ignores every rule entirely.
  → `vbus-gui/src/model.cpp`

---

## 🟠 Core — must work for the platform to be meaningful

- [ ] **Wire Scheduler into EthHub delivery**
  Replace immediate synchronous delivery in `EthHub::Send` with `m_Scheduler.post(now + delay, fn)` where `delay = payload_bytes * 8 / link_bps`.
  → `vbus/src/bus/eth_bus.cpp`

- [ ] **Wire Scheduler into CanBus delivery**
  Same as above: `delay = frame_bits / bitrate` (11 + 8·N + 3 overhead bits for CAN 2.0).
  → `vbus/src/bus/can_bus.cpp`

- [ ] **Add mutex to Scheduler**
  Pipe handler threads will call `bus->Send()` → `scheduler.post()` concurrently while the main loop calls `run_until()`. Add a `std::mutex` to `Scheduler`.
  → `vbus/src/core/scheduler.h`, `vbus/src/core/scheduler.cpp`

- [ ] **Add `delete <name>` command to daemon**
  Bus map grows forever; no way to tear down a bus. Detach recorder, erase from `g_buses`.
  → `vbus/src/daemon/vbusd.cpp`

- [ ] **Connect GUI to vbusd control pipe**
  `Model` currently never opens `\\.\pipe\vbusd`. Add a `DaemonClient` class (or inline in `Model`) that issues `create`, `list`, `record`, `stoprec` commands over the pipe.
  → `vbus-gui/src/model.h`, `vbus-gui/src/model.cpp` (new file `vbus-gui/src/daemon_client.h/.cpp`)

---

## 🟡 Features — needed for a complete first release

- [ ] **Populate BusStats in EthHub and CanBus**
  Add a `BusStats` member to each bus; increment `Tx_frames` on `Send`, `Rx_frames` on each `On_rx` delivery, `Drops` when an endpoint queue is full (future).
  → `vbus/src/bus/eth_bus.h/.cpp`, `vbus/src/bus/can_bus.h/.cpp`

- [ ] **Add `stats <name>` command to daemon**
  Expose `BusStats` over the control pipe so CLI and GUI can display live counters.
  → `vbus/src/daemon/vbusd.cpp`

- [ ] **CAN FD send path**
  Add `send-canfd <name> <id> <hex>` command (sets `Proto::CANFD`, validates payload ≤ 64 bytes). Add CAN FD bitrate field to `CanBus` constructor.
  → `vbus/src/daemon/vbusd.cpp`, `vbus/src/bus/can_bus.h/.cpp`

- [ ] **CAN 2.0 payload size validation**
  `send-can` should reject payloads > 8 bytes with `ERR payload too long`.
  → `vbus/src/daemon/vbusd.cpp`

- [ ] **Real VLAN splitter tap**
  Parse `Packet::vlan` from the Ethernet 802.1Q tag bytes in the payload instead of random assignment. Surface per-VLAN counters from `EthHub`.
  → `vbus/src/taps/` (new file `vlan_tap.h/.cpp`), `vbus-gui/src/model.cpp`

- [ ] **Implement `iface_pcap` backend properly**
  `PCap::start()` always returns `false`. Implement `pcap_open_live`, adapter enumeration via `pcap_findalldevs`, and a receive thread that pushes `Packet` objects into the bus ring.
  → `vbus-gui/src/backends/iface_pcap.h/.cpp`

---

## 🔵 Future — shared-memory data plane (Phase 2)

- [ ] **Design shared-memory ring layout**
  Define header struct: capacity, head, tail, per-slot `VCapRec` + payload inline. Decide slot size (fixed 2 KB covers CAN FD + Ethernet jumbo minimal).

- [ ] **Implement `vbus_open` — create/map the ring**
  `CreateFileMapping(INVALID_HANDLE_VALUE, ...)` named by bus name; `MapViewOfFile` in both daemon and client.
  → `vbus/src/client/vbus_client.cpp`

- [ ] **Implement lock-free SPSC enqueue/dequeue**
  Use `std::atomic` head/tail with `memory_order_acquire/release`. Provide `vbus_send` and `vbus_recv` in the C API.
  → `vbus/src/client/vbus_client.h/.cpp`

- [ ] **Daemon: expose ring handle to `IBus` endpoints**
  Each `BusWrap` should optionally hold a ring. Connected shared-memory clients register as `IEndpoint` instances backed by the ring writer.
  → `vbus/src/daemon/vbusd.cpp`

---

## ⚪ Polish / Quality

- [ ] **Replace all `TODO` comments in source with links back here**
- [ ] **Add unit tests for Scheduler (fire order, concurrent post)**
- [ ] **Add unit tests for Recorder/Replayer round-trip**
- [ ] **Add unit tests for `hex_to_bytes` edge cases (odd length, uppercase)**
- [ ] **`.clang-format` — enforce project-wide, add CI step**
- [ ] **Linux port: replace named pipe with Unix domain socket in vbusd**
- [ ] **Linux port: replace `CreateFileMapping` with `shm_open` + `mmap` in vbus_client**
