# Vision — Why OpenVBus Exists

## The Problem

Modern vehicles contain dozens of Electronic Control Units (ECUs) that communicate over shared buses — primarily Ethernet and CAN (Controller Area Network). Developing, testing, and debugging software for these systems traditionally requires:

- **Expensive proprietary tooling** (Vector CANoe, CANalyzer, ETAS, etc.) that can cost tens of thousands of dollars per seat.
- **Real hardware** — benches with physical CAN interfaces, automotive Ethernet switches, and sensor rigs that tie up lab space and are not reproducible across teams.
- **Manual replay infrastructure** — capturing traffic on a real vehicle and re-injecting it requires per-tool, per-protocol scripting.

This creates a high barrier to entry for individuals, small companies, open-source projects, and anyone doing early-stage ECU or AUTOSAR/middleware prototyping.

## The Idea

**OpenVBus** is an open-source, developer-first platform that brings the full virtual-bus development loop to any Windows (and eventually Linux) workstation — no hardware, no expensive licenses.

The core insight is that the *bus itself* is just a broadcast medium with timing semantics. A virtual bus can be modeled in software and exposed to user processes via the same abstractions a real bus driver would provide. Once that abstraction exists, everything else — recording, replay, filtering, analysis, bridging to real hardware — can be layered on top.

## Goals

| Goal | Description |
|------|-------------|
| **Zero-hardware prototyping** | Spin up a virtual Ethernet hub or CAN bus in one command, attach simulated ECU processes to it, and let them talk. |
| **Capture & replay** | Record traffic to `.vbuscap` files and replay them — exact timing, burst, or scaled — to reproduce scenarios deterministically. |
| **Introspection at every layer** | A live GUI shows bus lists, per-bus packet streams, VLAN topology, filters, and inspector details. |
| **Bridge to real hardware** | When a real NIC or CAN adapter is available, switch the virtual bus to forward to/from it via Npcap or a kernel driver — same API, no application changes. |
| **Open and extensible** | MIT/MPL-licensed C++20 core. Pluggable tap architecture (recorder, stats, future VLAN splitter). Stable C ABI for connecting ECU simulators in any language. |

## What OpenVBus Is Not

- It is **not** a full-fidelity bit-accurate simulation. Propagation delays and arbitration are modelled but hardware-level timing jitter is not the focus.
- It is **not** a replacement for hardware-in-the-loop (HiL) systems where end-to-end physical timing is safety-critical.
- It is **not** an AUTOSAR stack — it is the network substrate those stacks can run on inside a simulation.

## Target Users

- **ECU software developers** who want to iterate quickly on communication middleware without a bench.
- **Test engineers** building regression suites that replay captured vehicle scenarios.
- **Students and researchers** studying automotive networking (CAN, CAN FD, 100BASE-T1, TSN).
- **Tool builders** who need an open bus abstraction to build analyzers, loggers, or fault-injection frameworks on.

## Redefined Vision Statement

> OpenVBus is a software-defined virtual vehicle bus platform. It gives developers a local, hardware-free, fully introspectable Ethernet and CAN network fabric — complete with recording, replay, filtering, and a live GUI — so that the full automotive communication development and test loop can happen on a workstation before any ECU or wire is touched.
