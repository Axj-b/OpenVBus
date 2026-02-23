# Architecture

## High-Level Overview

OpenVBus is split into two top-level CMake sub-projects that can be built and deployed independently.

```
┌────────────────────────────────────────────────────────────────┐
│  vbus-gui  (ImGui / OpenGL desktop application)                │
│  ovb::Model ← ovb::AppState ← panels (bus list, inspector,    │
│                                        packets, filters, VLAN, │
│                                        log)                    │
│  Backends: iface_mock | iface_pcap                             │
└───────────────────────────┬────────────────────────────────────┘
                            │ (future: control pipe / shared-mem)
┌───────────────────────────▼────────────────────────────────────┐
│  vbus  (core library + daemon + CLI)                           │
│                                                                │
│  ┌──────────┐   ┌──────────┐   ┌──────────────┐              │
│  │ vbusd.exe│   │vbusctl   │   │ vbus_client  │              │
│  │ (daemon) │   │ (CLI)    │   │ (future C API│              │
│  └────┬─────┘   └────┬─────┘   └──────┬───────┘              │
│       │  Named pipe  │                │ Shared-mem (TODO)     │
│       └──────────────┘                │                       │
│                                       ▼                       │
│  ┌─────────────────────────────────────────────────────────┐  │
│  │                    Bus Engine                           │  │
│  │  EthHub (IBus)          CanBus (IBus)                   │  │
│  │  link_bps scheduling    bitrate scheduling              │  │
│  │  IEndpoint multicast    IEndpoint multicast             │  │
│  └────────────────────┬────────────────────────────────────┘  │
│                       │ Tap callbacks                          │
│  ┌────────────────────▼────────────────────────────────────┐  │
│  │                    Taps                                 │  │
│  │  Recorder / Replayer (.vbuscap)    BusStats             │  │
│  └─────────────────────────────────────────────────────────┘  │
│                                                                │
│  ┌────────────────────────────────────────────────────────┐   │
│  │                   Core Utilities                       │   │
│  │  Clock (RealtimeClock)   Scheduler (event queue)       │   │
│  └────────────────────────────────────────────────────────┘   │
└────────────────────────────────────────────────────────────────┘
```

---

## Sub-project: `vbus`

### `core/`

| File | Purpose |
|------|---------|
| `clock.h` | `Clock` abstract interface + `RealtimeClock` implementation. Time unit is `SimTime` = `std::chrono::nanoseconds`. |
| `scheduler.h/.cpp` | Min-heap priority queue of `(SimTime, callback)` pairs. Used by bus implementations to model propagation and serialisation delay. |
| `core.h` | Pulls in `<cstdint>`, `<string>`, `<filesystem>` — common includes shared by all modules. |

### `bus/`

| File | Purpose |
|------|---------|
| `frame.h` | `Frame` struct: `Proto` enum (ETH2, CAN20, CANFD), `Tag` (CAN identifier or reserved), `Ts_ns` timestamp, `Payload` byte vector. Also `hex_to_bytes()` utility. |
| `ibus.h` | `IBus` and `IEndpoint` abstract interfaces. Any bus type implements `IBus`; any ECU endpoint implements `IEndpoint::On_rx()`. |
| `eth_bus.h/.cpp` | `EthHub` — multicast Ethernet hub. Schedules delivery based on `link_bps` serialization time. Fires a `RecordCb` tap on every forwarded frame. |
| `can_bus.h/.cpp` | `CanBus` — CAN broadcast bus. Schedules delivery based on `bitrate`. Fires a `RecordCb` tap on every forwarded frame. |

### `taps/`

Taps are callbacks attached to buses. They observe traffic without being an `IEndpoint`.

| File | Purpose |
|------|---------|
| `recorder.h/.cpp` | `Recorder` writes frames to a `.vbuscap` binary file. `Replayer` reads them back and yields `Frame` objects. |
| `stats.h` | `BusStats` — atomic counters for `Rx_frames`, `Tx_frames`, `Drops`. |

### `client/`

| File | Purpose |
|------|---------|
| `vbus_client.h/.cpp` | Placeholder C ABI (`vbus_open`, `vbus_close`). Will be implemented as a shared-memory ring-buffer data plane. Today clients talk via the control pipe. |

### `daemon/`

`vbusd.exe` is the host process that:

1. Creates a `RealtimeClock` and a `Scheduler`.
2. Listens on `\\.\pipe\vbusd` for line-oriented text commands.
3. Dispatches commands to a locked map of named `BusWrap` entries (`IBus` + optional `Recorder`).
4. Runs indefinitely; clean shutdown on `quit` command.

### `cli/`

`vbusctl.exe` opens `\\.\pipe\vbusd`, writes one line, reads the response, prints it, and exits. It is a thin shell wrapper around the pipe protocol.

---

## Sub-project: `vbus-gui`

The GUI is built with **Dear ImGui** over **OpenGL 3** (via `glad`) and runs as a standalone desktop application. It does **not** link against the `vbus` daemon today — it has its own in-process model and mock/pcap backends. The planned next step is to connect the GUI to the daemon over the control pipe.

### `src/`

| File | Purpose |
|------|---------|
| `main.cpp` | Entry point. Creates `AppState`, `Model`, `ImguiLayer`. Runs the render loop. |
| `app_state.h` | Plain data: `Bus`, `Packet`, `FilterRule`, `InterfaceDesc`, `AppState`. The single source of truth for the UI. |
| `model.h/.cpp` | `Model` wraps `AppState` and provides operations: `newBus`, `deleteBus`, `getBus`, `tick`, `enumerateIfaces`. Generates mock packet traffic via `tick()`. |
| `imgui_layer.h/.cpp` | Window/context setup, event loop, panel orchestration. |

### `panels/`

Each panel is a self-contained ImGui window that reads/writes `AppState` via `Model`.

| Panel | Responsibility |
|-------|---------------|
| `panel_bus_list` | Left sidebar listing all virtual buses; create / delete; select active bus. |
| `panel_inspector` | Detailed view of the selected bus: attached interface, bitrate, live stats. |
| `panel_packets` | Scrolling packet log for the selected bus with hex preview. |
| `panel_filters` | Add/remove include/exclude filter rules (BPF-style, stub). |
| `panel_vlan` | VLAN topology view — per-VLAN packet counts for the selected bus. |
| `panel_log` | Application event log (create/delete bus, errors, etc.). |

### `backends/`

| File | Purpose |
|------|---------|
| `iface_base.h` | `ovb::iface::Base` abstract interface with `start()`, `stop()`, `stats()`. |
| `iface_mock.h/.cpp` | Mock backend that generates synthetic packets for UI development. |
| `iface_pcap.h/.cpp` | Npcap/WinPcap backend that sniffs a real NIC (optional, enabled with `-DOPENVBUS_WITH_PCAP=ON`). |

### `util/`

| File | Purpose |
|------|---------|
| `id_gen.h` | Simple monotonic ID generator for buses and packets. |
| `ringbuffer.h` | Fixed-capacity ring buffer used for the per-bus packet ring in `AppState::Bus::ring`. |

---

## Data Flow — Recording a Frame

```
Application code
    │
    ▼  IBus::Send(src, Frame)
EthHub::Send()
    │ 1. Compute serialisation delay = payload_bytes * 8 / link_bps
    │ 2. Scheduler::post(now + delay, deliver_fn)
    │
    ▼  (at delivery time)
deliver_fn()
    │ 3. For each IEndpoint ≠ src → IEndpoint::On_rx(frame)
    │ 4. If RecordCb set → RecordCb(frame)
    │                           │
    │                           ▼
    │                    Recorder::Write()
    │                    → VCapHeader + VCapRec + payload bytes
    ▼
IEndpoint::On_rx() in each connected simulator
```

---

## Layered Roadmap

```
Phase 1 (current)   Daemon + CLI + Recorder/Replayer + GUI scaffold
Phase 2             Shared-memory ring data plane (vbus_client)
Phase 3             Npcap Ethernet bridge, CAN adapter bridge (SocketCAN / PEAK)
Phase 4             CAN FD, TSN scheduling hooks, SOME/IP parser tap
Phase 5             Linux port (Unix domain socket control, io_uring data plane)
```
