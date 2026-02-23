# OpenVBus Documentation

> A software-defined virtual vehicle bus platform — hardware-free Ethernet and CAN development on your workstation.

---

## Contents

| Document | Description |
|----------|-------------|
| [vision.md](vision.md) | Why OpenVBus exists, what it is and is not, target users, redefined vision statement |
| [architecture.md](architecture.md) | System components, data-flow diagrams, module breakdown for `vbus` and `vbus-gui` |
| [getting-started.md](getting-started.md) | Build instructions, running the daemon, CLI quick-demo, GUI walkthrough |
| [protocol.md](protocol.md) | Named-pipe command reference and VBUSCAP binary file format |

---

## One-Minute Summary

OpenVBus lets you create named virtual buses (Ethernet hub or CAN) in a background daemon, attach simulated ECU processes to them, capture traffic to `.vbuscap` files, and replay it — all without any physical hardware. A companion ImGui desktop GUI provides a live packet view, VLAN topology, filtering, and bus inspection.

```
vbusd.exe                        ← keeps buses alive
  │  \\.\pipe\vbusd (text)
  ▼
vbusctl.exe create eth0 eth 1G   ← create a 1-Gbps virtual Ethernet hub
vbusctl.exe record eth0 on x.cap ← start capturing
vbusctl.exe send-eth eth0 deadbeef ← inject a frame
vbusctl.exe replay eth0 x.cap exact ← replay with original timing
```

---

## Quick Links

- [Build instructions →](getting-started.md#build--core-vbus)
- [All CLI commands →](protocol.md#commands)
- [VBUSCAP file format →](protocol.md#vbuscap-binary-format)
- [Layered roadmap →](architecture.md#layered-roadmap)
- [Vision & goals →](vision.md#goals)

---

## License

OpenVBus is released under the [Mozilla Public License 2.0](../LICENSE).
