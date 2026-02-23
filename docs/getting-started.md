# Getting Started

## Prerequisites

| Requirement | Version | Notes |
|-------------|---------|-------|
| Windows 10 / 11 | 64-bit | Daemon uses Win32 named pipes |
| Visual Studio 2022 | 17.x | C++ Desktop workload |
| CMake | ≥ 3.20 | Bundled with VS or standalone |
| Git | any | |
| Npcap SDK *(optional)* | 1.13+ | Needed only for `iface_pcap` backend in the GUI |

> **Linux support** is on the roadmap (Phase 5). The bus engine and recorder are portable; the daemon's named-pipe transport will be replaced with a Unix domain socket.

---

## Clone

```powershell
git clone https://github.com/your-org/OpenVBus.git
cd OpenVBus
```

---

## Build — Core (`vbus`)

```powershell
# Configure
cmake -S vbus -B vbus/build -G "Visual Studio 17 2022" -A x64

# Build Release
cmake --build vbus/build --config Release
```

Outputs in `vbus/build/Release/`:

| Binary | Description |
|--------|-------------|
| `vbusd.exe` | Daemon process — keeps buses alive |
| `vbusctl.exe` | CLI for sending commands to the daemon |

---

## Build — GUI (`vbus-gui`)

```powershell
# Without PCAP (mock backend only)
cmake -S vbus-gui -B vbus-gui/build -G "Visual Studio 17 2022" -A x64

# With Npcap (real NIC sniffing)
cmake -S vbus-gui -B vbus-gui/build -G "Visual Studio 17 2022" -A x64 ^
      -DOPENVBUS_WITH_PCAP=ON -DNPCAP_SDK_DIR="C:/npcap-sdk"

cmake --build vbus-gui/build --config Release
```

Output: `vbus-gui/build/Release/openvbus_gui.exe`

---

## Build — Everything from Root

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

---

## Running the Daemon

Open a terminal and start `vbusd`:

```powershell
.\vbus\build\Release\vbusd.exe
```

The daemon blocks and listens on `\\.\pipe\vbusd`. Keep this terminal open.

---

## Quick Demo with the CLI

Open a **second** terminal:

```powershell
$ctl = ".\vbus\build\Release\vbusctl.exe"

# Create a virtual Ethernet hub at 1 Gbps
& $ctl create eth0 eth 1000000000

# Create a virtual CAN bus at 500 kbps
& $ctl create can0 can 500000

# List all buses
& $ctl list

# Start recording eth0 traffic to a file
& $ctl record eth0 on logs\eth0.vbuscap

# Inject a raw Ethernet frame (hex payload)
& $ctl send-eth eth0 deadbeefcafebabe0011223344556677

# Inject a CAN frame (id=0x123, payload)
& $ctl send-can can0 0x123 AABBCCDD

# Stop recording
& $ctl stoprec eth0

# Replay the capture with exact inter-frame timing
& $ctl replay eth0 logs\eth0.vbuscap exact
```

---

## Running the GUI

```powershell
.\vbus-gui\build\Release\openvbus_gui.exe
```

The GUI opens an ImGui window with the following panels:

| Panel | Shortcut / Location |
|-------|---------------------|
| Bus List | Left sidebar |
| Inspector | Right panel, top |
| Packets | Right panel, center |
| Filters | Right panel, bottom-left |
| VLAN | Right panel, bottom-right |
| Log | Bottom strip |

Use **Bus List → + New Bus** to create a virtual bus, then attach a mock or pcap interface via the Inspector panel.

---

## Replay Modes

When replaying a `.vbuscap` file there are three modes:

| Mode | Behaviour |
|------|-----------|
| `exact` | Preserves original inter-frame gaps (nanosecond precision via the Scheduler). |
| `burst` | Ignores timestamps — injects all frames as fast as possible. |
| `scale:K` | Multiplies all inter-frame gaps by factor `K` (e.g. `scale:0.5` = 2× speed). |

---

## Project Layout

```
OpenVBus/
├── CMakeLists.txt        Root CMake (adds both sub-projects)
├── vbus/                 Core daemon, CLI, bus engine, recorder
│   └── src/
│       ├── bus/          EthHub, CanBus, IBus, Frame
│       ├── core/         Clock, Scheduler
│       ├── taps/         Recorder, Replayer, Stats
│       ├── client/       vbus_client C API placeholder
│       ├── daemon/       vbusd.cpp
│       └── cli/          vbusctl.cpp
└── vbus-gui/             ImGui desktop application
    └── src/
        ├── panels/       UI panels
        ├── backends/     iface_mock, iface_pcap
        └── util/         RingBuffer, IdGen
```

---

## Next Steps

- Read [architecture.md](architecture.md) for a detailed component breakdown.
- Read [protocol.md](protocol.md) for the named-pipe command reference and `.vbuscap` binary format.
- See [vision.md](vision.md) for the project goals and roadmap.
