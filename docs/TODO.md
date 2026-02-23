# OpenVBus — Implementation TODO

Tasks are grouped by component and ordered by priority. Each item links to the affected file(s).

---

## 🔴 Bugs (broken today)

- [x] **stoprec command missing from daemon**
  Added as direct alias that calls `detachRecorder()`.
  → `vbus/src/daemon/vbusd.cpp`

- [x] **Replay timing origin mismatch**
  Fixed: now uses inter-frame deltas (`f.Ts_ns - first_cap_ts`) relative to `replay_start`.
  → `vbus/src/daemon/vbusd.cpp`

- [x] **Replay `scale:K` not parsed**
  `burst` skips all timing; `scale:K` multiplies each offset by `K`.
  → `vbus/src/daemon/vbusd.cpp`

- [x] **`quit` calls `ExitProcess(0)` — no cleanup**
  Replaced with `g_shutdown` atomic flag + `condition_variable`; recorders are flushed before exit.
  → `vbus/src/daemon/vbusd.cpp`

- [x] **Packet timestamps not monotonic in GUI mock**
  Added `m_tick_ns` accumulator to `Model`; all packets get `timestamp_ns = m_tick_ns`.
  → `vbus-gui/src/model.h`, `vbus-gui/src/model.cpp`

- [x] **Filter rules never applied**
  Added `glob_match()` and `passes_filters()` in `Model::tick()`; packets that fail exclude/include rules are dropped from the ring.
  → `vbus-gui/src/model.cpp`

---

## 🟠 Core — must work for the platform to be meaningful

- [x] **Wire Scheduler into EthHub delivery**
  Serialization delay = `(payload + 18) * 8 / link_bps` ns; frame delivered via `m_Scheduler.post()`.
  → `vbus/src/bus/eth_bus.cpp`

- [x] **Wire Scheduler into CanBus delivery**
  Frame bit count = `43 + N*8` (CAN 2.0) or `67 + N*8` (CAN FD); delivered via `m_Scheduler.post()`.
  → `vbus/src/bus/can_bus.cpp`

- [x] **Add mutex to Scheduler**
  `post()` and `run_until()`/`run()` now hold `m_mtx`; callbacks drained to local vector before firing.
  → `vbus/src/core/scheduler.h`, `vbus/src/core/scheduler.cpp`

- [x] **Add `delete <name>` command to daemon**
  Detaches recorder, erases from `g_buses`.
  → `vbus/src/daemon/vbusd.cpp`

- [ ] **Connect GUI to vbusd control pipe**
  `Model` currently never opens `\\.\pipe\vbusd`. Add a `DaemonClient` class (or inline in `Model`) that issues `create`, `list`, `record`, `stoprec` commands over the pipe.
  → `vbus-gui/src/model.h`, `vbus-gui/src/model.cpp` (new file `vbus-gui/src/daemon_client.h/.cpp`)

---

## 🟡 Features — needed for a complete first release

- [x] **Populate BusStats in EthHub and CanBus**
  `Tx_frames` incremented on `Send`; `Rx_frames` incremented per endpoint delivery inside the scheduled callback.
  → `vbus/src/bus/eth_bus.h/.cpp`, `vbus/src/bus/can_bus.h/.cpp`

- [x] **Add `stats <name>` command to daemon**
  Returns `tx=N rx=N drops=N` from `BusStats`.
  → `vbus/src/daemon/vbusd.cpp`

- [x] **CAN FD send path**
  Added `send-canfd <name> <id> <hex>` command; sets `Proto::CANFD`, validates payload ≤ 64 bytes. `CanBus::Send` uses extended bit-count for FD frames.
  → `vbus/src/daemon/vbusd.cpp`, `vbus/src/bus/can_bus.cpp`

- [x] **CAN 2.0 payload size validation**
  `send-can` now rejects payloads > 8 bytes with an error response.
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

- [x] **GUI: `InputText` UB on `std::string::data()`**
  Inspector and Filters now use `char[128]` buffers sync'd back to `std::string`.
  → `panel_inspector.cpp`, `panel_filters.cpp`

- [x] **GUI: duplicate stale filter evaluator in Packets panel**
  Removed `pass()` and per-packet filter loop; ring already contains only filtered packets.
  → `panel_packets.cpp`

- [x] **GUI: Log panel always printed "Startup OK"**
  Added `AppState::log_lines`; `Model` emits timestamped entries on create/delete bus and iface attach/detach. Log panel shows scrolling entries with Clear button.
  → `app_state.h`, `model.h/.cpp`, `panel_log.cpp`

- [x] **GUI: VLAN panel was a static stub**
  Now counts packets per VLAN from the live ring and displays them in a sorted scrollable list.
  → `panel_vlan.cpp`

- [x] **GUI: Bus List had no delete button**
  Added an X `SmallButton` next to each bus entry.
  → `panel_bus_list.cpp`

- [x] **GUI: "Exit" menu item did nothing**
  Sets `AppState::request_exit`; `main.cpp` checks it in the render loop condition.
  → `imgui_layer.cpp`, `main.cpp`

- [x] **GUI: iface `start()`/`stop()` never called**
  `Model::attachIface()` calls `iface::make()` + `start()`; `Model::detachIface()` calls `stop()`. Inspector calls these instead of directly mutating `bus->iface`.
  → `model.h/.cpp`, `panel_inspector.cpp`

- [ ] **Replace all `TODO` comments in source with links back here**
- [ ] **Add unit tests for Scheduler (fire order, concurrent post)**
- [ ] **Add unit tests for Recorder/Replayer round-trip**
- [ ] **Add unit tests for `hex_to_bytes` edge cases (odd length, uppercase)**
- [ ] **`.clang-format` — enforce project-wide, add CI step**
- [ ] **Linux port: replace named pipe with Unix domain socket in vbusd**
- [ ] **Linux port: replace `CreateFileMapping` with `shm_open` + `mmap` in vbus_client**
