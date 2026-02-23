#define NOMINMAX
#include "model.h"
#include "backends/iface_mock.h"
#include "util/id_gen.h"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdlib>

using namespace ovb;

// ─── helpers ──────────────────────────────────────────────────────────────────

static const char *protoName(uint8_t p) {
    switch (p) {
    case 1: return "ETH";
    case 2: return "CAN";
    case 3: return "CANFD";
    case 4: return "UDP";
    case 5: return "TCP";
    default: return "?";
    }
}

// Simple glob-style match: supports '*' and '?' wildcards.
static bool glob_match(const std::string &pattern, const std::string &text) {
    size_t pi = 0, ti = 0;
    size_t star_pi = std::string::npos, star_ti = 0;
    while (ti < text.size()) {
        if (pi < pattern.size() && (pattern[pi] == text[ti] || pattern[pi] == '?')) {
            ++pi; ++ti;
        } else if (pi < pattern.size() && pattern[pi] == '*') {
            star_pi = pi++; star_ti = ti;
        } else if (star_pi != std::string::npos) {
            pi = star_pi + 1; ti = ++star_ti;
        } else {
            return false;
        }
    }
    while (pi < pattern.size() && pattern[pi] == '*') ++pi;
    return pi == pattern.size();
}

static bool passes_filters(const std::vector<FilterRule> &filters, const Packet &p) {
    if (filters.empty()) return true;
    char buf[48];
    std::snprintf(buf, sizeof(buf), "vlan:%u size:%u", p.vlan, p.size);
    std::string repr(buf);
    for (const auto &rule : filters) {
        bool matched = glob_match(rule.expr, repr);
        if (rule.type == FilterRule::Type::Exclude && matched) return false;
        if (rule.type == FilterRule::Type::Include && !matched) return false;
    }
    return true;
}

// ─── Model lifecycle ──────────────────────────────────────────────────────────

Model::Model(AppState &s) : state(s) {
    state.daemon_connected = m_daemon.isConnected();
    if (state.daemon_connected)
        addLog("[startup] Connected to vbusd");
    else
        addLog("[startup] vbusd not found – running in mock mode");
}

Model::~Model() {
    for (auto &[id, sub] : m_subs)
        sub->unsubscribe();
}

// ─── Bus management ───────────────────────────────────────────────────────────

Bus &Model::newBus(const std::string &name) {
    Bus b;
    b.id   = ovb::next_id();
    b.name = name;
    state.buses.push_back(std::move(b));
    Bus &ref = state.buses.back();
    state.selected_bus = ref.id;

    if (state.daemon_connected) {
        // Create a 1 Gbps Ethernet bus on the daemon.
        std::string resp;
        m_daemon.sendCmd("create " + name + " eth 1000000000", resp);
        addLog("[create] bus '" + name + "' → daemon: " + resp);
        subscribeFrames(ref);
    } else {
        addLog("[create] bus '" + name + "' (local mock)");
    }
    return ref;
}

void Model::deleteBus(uint32_t id) {
    auto it = std::find_if(state.buses.begin(), state.buses.end(),
                           [&](const Bus &b) { return b.id == id; });
    if (it == state.buses.end()) return;

    if (it->recording) stopRecord(*it);
    unsubscribeFrames(id);
    detachIface(*it);

    if (state.daemon_connected) {
        std::string resp;
        m_daemon.sendCmd("delete " + it->name, resp);
        addLog("[delete] bus '" + it->name + "' → daemon: " + resp);
    } else {
        addLog("[delete] bus '" + it->name + "'");
    }

    state.buses.erase(it);
    state.selected_bus = state.buses.empty() ? 0 : state.buses.front().id;
}

Bus *Model::getBus(uint32_t id) {
    for (auto &b : state.buses)
        if (b.id == id) return &b;
    return nullptr;
}

// ─── Frame subscription ───────────────────────────────────────────────────────

void Model::subscribeFrames(Bus &b) {
    uint32_t bid = b.id;
    const std::string bname = b.name;
    auto client = std::make_unique<DaemonClient>();
    bool ok = client->subscribe(bname, [this, bid](const RawFrame &f) {
        std::scoped_lock lk(m_queue_mtx);
        m_frame_queue.push_back({bid, f});
    });
    if (ok) {
        m_subs[bid] = std::move(client);
        addLog("[sub] subscribed to frames on '" + bname + "'");
    } else {
        addLog("[sub] WARNING: could not subscribe to '" + bname + "'");
    }
}

void Model::unsubscribeFrames(uint32_t busId) {
    auto it = m_subs.find(busId);
    if (it != m_subs.end()) {
        it->second->unsubscribe();
        m_subs.erase(it);
    }
}

// ─── tick: drain frame queue → ring buffers ───────────────────────────────────

void Model::tick(double dt) {
    m_tick_ns += static_cast<uint64_t>(dt * 1e9);

    // Refresh daemon connection status periodically (every ~2 s)
    static uint64_t last_ping = 0;
    if (m_tick_ns - last_ping > 2'000'000'000ULL) {
        last_ping = m_tick_ns;
        bool was = state.daemon_connected;
        state.daemon_connected = m_daemon.isConnected();
        if (!was && state.daemon_connected)
            addLog("[daemon] connected");
        else if (was && !state.daemon_connected)
            addLog("[daemon] disconnected");
    }

    // Drain incoming frames from subscription threads
    std::deque<std::pair<uint32_t, RawFrame>> local;
    {
        std::scoped_lock lk(m_queue_mtx);
        local.swap(m_frame_queue);
    }
    for (auto &[bid, rf] : local) {
        Bus *bus = getBus(bid);
        if (!bus || !bus->enabled) continue;

        Packet p;
        p.timestamp_ns = rf.ts_ns;
        p.proto        = rf.proto;
        p.size         = static_cast<uint16_t>(rf.payload.size());
        p.vlan         = 0;
        size_t copy    = std::min(rf.payload.size(), p.preview.size());
        std::copy(rf.payload.begin(), rf.payload.begin() + static_cast<ptrdiff_t>(copy),
                  p.preview.begin());

        if (!passes_filters(bus->filters, p)) continue;
        bus->ring.push_front(p);
        if (bus->ring.size() > 4096) bus->ring.pop_back();
    }

    // Mock: generate synthetic traffic for buses that have no daemon subscription
    if (!state.daemon_connected) {
        std::uniform_int_distribution<int> sizeDist(64, 1500);
        std::uniform_int_distribution<int> vlanDist(0, 1);
        for (auto &b : state.buses) {
            if (!b.enabled || m_subs.count(b.id)) continue;
            Packet p;
            p.timestamp_ns = m_tick_ns;
            p.proto        = 1; // ETH2
            p.vlan         = vlanDist(rng) ? 100 : 0;
            p.size         = static_cast<uint16_t>(sizeDist(rng));
            for (int i = 0; i < 64; i++) p.preview[i] = static_cast<uint8_t>(rng());
            if (!passes_filters(b.filters, p)) continue;
            b.ring.push_front(p);
            if (b.ring.size() > 4096) b.ring.pop_back();
        }
    }
}

// ─── Interface management (capture) ───────────────────────────────────────────

std::vector<InterfaceDesc> Model::enumerateIfaces() const {
    return {
        {"UDP capture (receive datagrams)",  "udp"},
        {"TCP proxy (transparent relay)",    "tcp"},
        {"Mock (synthetic traffic)",         "mock"},
    };
}

void Model::attachIface(Bus &b, const InterfaceDesc &d) {
    detachIface(b);
    b.iface = d;

    if (d.driver == "mock") {
        auto backend = iface::make(d);
        if (backend && backend->start()) {
            m_ifaces[b.id] = std::move(backend);
            addLog("[iface] mock attached to '" + b.name + "'");
        } else {
            b.iface.reset();
        }
        return;
    }

    if (!state.daemon_connected) {
        addLog("[iface] daemon not connected – cannot attach " + d.driver);
        b.iface.reset();
        return;
    }

    std::string resp;
    if (d.driver == "udp") {
        m_daemon.sendCmd(
            "capture-udp " + b.name +
            " " + std::string(b.bind_host) +
            " " + std::to_string(b.bind_port), resp);
        addLog("[capture] UDP " + std::string(b.bind_host) + ":" +
               std::to_string(b.bind_port) +
               " on '" + b.name + "' → " + resp);
    } else if (d.driver == "tcp") {
        m_daemon.sendCmd(
            "capture-tcp " + b.name +
            " " + std::string(b.bind_host) +
            " " + std::to_string(b.bind_port) +
            " " + std::string(b.target_host) +
            " " + std::to_string(b.target_port), resp);
        addLog("[capture] TCP proxy " + std::string(b.bind_host) + ":" +
               std::to_string(b.bind_port) +
               " → " + std::string(b.target_host) + ":" +
               std::to_string(b.target_port) +
               " on '" + b.name + "' → " + resp);
    }

    if (resp.rfind("ERR", 0) == 0) {
        b.iface.reset();
        addLog("[iface] FAILED: " + resp);
    }
}

void Model::detachIface(Bus &b) {
    // Remove mock backend
    auto mit = m_ifaces.find(b.id);
    if (mit != m_ifaces.end()) {
        mit->second->stop();
        m_ifaces.erase(mit);
    }

    if (b.iface && b.iface->driver != "mock" && state.daemon_connected) {
        std::string resp;
        m_daemon.sendCmd("stop-capture " + b.name, resp);
        addLog("[iface] detached capture on '" + b.name + "' → " + resp);
    } else if (b.iface) {
        addLog("[iface] detached '" + b.iface->driver + "' from '" + b.name + "'");
    }
    b.iface.reset();
}

// ─── Recording ────────────────────────────────────────────────────────────────

void Model::startRecord(Bus &b) {
    if (!state.daemon_connected) {
        addLog("[rec] daemon not connected"); return;
    }
    if (b.record_path[0] == '\0') {
        addLog("[rec] no output path set"); return;
    }
    std::string resp;
    m_daemon.sendCmd("record " + b.name + " on " + std::string(b.record_path), resp);
    b.recording = (resp.rfind("OK", 0) == 0);
    addLog("[rec] start '" + b.name + "' → " + resp);
}

void Model::stopRecord(Bus &b) {
    if (!state.daemon_connected) return;
    std::string resp;
    m_daemon.sendCmd("stoprec " + b.name, resp);
    b.recording = false;
    addLog("[rec] stop '" + b.name + "' → " + resp);
}
void Model::startRecordAll() {
    if (!state.daemon_connected) {
        addLog("[rec-all] daemon not connected"); return;
    }
    bool any = false;
    for (auto &b : state.buses) {
        // Build per-bus path: prefix_busname.vbuscap
        std::string path = std::string(state.record_all_prefix)
                           + "_" + b.name + ".vbuscap";
        // Copy into the bus record_path so the inspector shows it too
        std::snprintf(b.record_path, sizeof(b.record_path), "%s", path.c_str());
        std::string resp;
        m_daemon.sendCmd("record " + b.name + " on " + path, resp);
        b.recording = (resp.rfind("OK", 0) == 0);
        addLog("[rec-all] '" + b.name + "' -> " + resp);
        if (b.recording) any = true;
    }
    state.global_recording = any;
}

void Model::stopRecordAll() {
    for (auto &b : state.buses) {
        if (!b.recording) continue;
        std::string resp;
        m_daemon.sendCmd("stoprec " + b.name, resp);
        b.recording = false;
        addLog("[rec-all] stop '" + b.name + "' -> " + resp);
    }
    state.global_recording = false;
}
void Model::startReplayAll() {
    if (!state.daemon_connected) {
        addLog("[replay-all] daemon not connected"); return;
    }
    // Build: replay-sync <mode> file1 host1 port1 file2 host2 port2 ...
    // mode for daemon: exact | burst | scale:K
    std::string daemonMode = state.replay_all_mode;
    if (daemonMode == "scale") {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "scale:%.3f", state.replay_all_scale);
        daemonMode = buf;
    }
    std::string cmd = "replay-sync " + daemonMode;
    int streamCount = 0;
    for (auto &b : state.buses) {
        if (b.record_path[0] == '\0') continue;
        if (b.forward_host[0] == '\0' || b.forward_port == 0) continue;
        cmd += " " + std::string(b.record_path);
        cmd += " " + std::string(b.forward_host);
        cmd += " " + std::to_string(b.forward_port);
        ++streamCount;
    }
    if (streamCount == 0) {
        addLog("[replay-all] no buses with record file + forward destination set"); return;
    }
    std::string resp;
    m_daemon.sendCmd(cmd, resp);
    state.global_replaying = true;
    addLog("[replay-all] " + std::to_string(streamCount) + " stream(s) -> " + resp);
}

void Model::replayFile(Bus &b, const std::string &mode) {
    if (!state.daemon_connected) {
        addLog("[replay] daemon not connected"); return;
    }
    if (b.replay_path[0] == '\0') {
        addLog("[replay] no file path set"); return;
    }
    // Run replay in a detached thread so the GUI stays responsive.
    std::string resp;
    // Use replay-udp if proto is UDP, replay otherwise.
    // For simplicity always use generic replay (injects on the bus).
    m_daemon.sendCmd(
        "replay " + b.name + " " + std::string(b.replay_path) + " " + mode, resp);
    addLog("[replay] '" + b.name + "' " + mode + " → " + resp);
}

// ─── Log ──────────────────────────────────────────────────────────────────────
// ─── UDP Forward ──────────────────────────────────────────────────────────────────

void Model::startForward(Bus &b) {
    if (!state.daemon_connected) {
        addLog("[fwd] daemon not connected"); return;
    }
    std::string resp;
    m_daemon.sendCmd(
        "forward-udp " + b.name +
        " " + std::string(b.forward_host) +
        " " + std::to_string(b.forward_port), resp);
    b.forwarding = (resp.rfind("OK", 0) == 0);
    addLog("[fwd] start '" + b.name + "' → " +
           std::string(b.forward_host) + ":" + std::to_string(b.forward_port) +
           " → " + resp);
}

void Model::stopForward(Bus &b) {
    if (!state.daemon_connected) return;
    std::string resp;
    m_daemon.sendCmd("stop-forward " + b.name, resp);
    b.forwarding = false;
    addLog("[fwd] stop '" + b.name + "' → " + resp);
}
void Model::addLog(const std::string &msg) {
    char prefix[32];
    std::snprintf(prefix, sizeof(prefix), "[%08.3f] ", m_tick_ns / 1e9);
    state.log_lines.push_back(std::string(prefix) + msg);
    if (state.log_lines.size() > 1024)
        state.log_lines.erase(state.log_lines.begin());
}
