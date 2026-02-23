
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <unordered_map>
#include <mutex>
#include <sstream>
#include <iostream>
#include <vector>
#include "../core/clock.h"
#include "../core/scheduler.h"
#include "../bus/eth_bus.h"
#include "../bus/can_bus.h"
#include "../taps/recorder.h"
#include "../net/udp_endpoint.h"
#include "../net/tcp_proxy.h"

using namespace vbus;

// Shared subscription state – lifetime outlives BusWrap moves because it is
// heap-allocated via shared_ptr.  The scheduler lambda captures a copy of the
// shared_ptr so the SubInfo stays alive even if the bus is deleted while a
// frame is in-flight.
struct SubInfo {
    std::mutex mtx;
    HANDLE     pipe{INVALID_HANDLE_VALUE};
};

struct BusWrap {
    std::unique_ptr<IBus>             bus;
    std::unique_ptr<Recorder>         rec;
    std::unique_ptr<ICaptureEndpoint> cap;
    std::shared_ptr<SubInfo>          sub{std::make_shared<SubInfo>()};
};

static std::mutex                               g_mtx;
static std::unordered_map<std::string, BusWrap> g_buses;
static RealtimeClock                            g_clk;
static Scheduler                                g_sched(g_clk);

// Shutdown coordination
static std::atomic<bool>       g_shutdown{false};
static std::mutex              g_shutdown_mtx;
static std::condition_variable g_shutdown_cv;

static void detachRecorder(BusWrap &w) {
    w.rec.reset();
    if (auto *eth = dynamic_cast<EthHub *>(w.bus.get()))
        eth->SetRecordCb(nullptr);
    if (auto *can = dynamic_cast<CanBus *>(w.bus.get()))
        can->SetRecordCb(nullptr);
}

// Attach a live-stream subscriber: every frame delivered on the bus is written
// to hPipe as a single message (VCapRec header + payload).
static void attachSub(BusWrap &w, HANDLE hPipe) {
    auto si = w.sub; // shared_ptr copy – captured by the lambda below
    {
        std::scoped_lock lk(si->mtx);
        if (si->pipe != INVALID_HANDLE_VALUE)
            CloseHandle(si->pipe);
        si->pipe = hPipe;
    }
    auto cb = [si](const Frame &f) {
        // Build one message: VCapRec(24 bytes) + payload
        VCapRec rec{};
        rec.Proto = static_cast<uint8_t>(f.Proto);
        rec.Tag   = f.Tag;
        rec.Ts_ns = f.Ts_ns;
        rec.Len   = static_cast<uint32_t>(f.Payload.size());
        std::vector<char> msg(sizeof(VCapRec) + f.Payload.size());
        std::memcpy(msg.data(), &rec, sizeof(VCapRec));
        if (!f.Payload.empty())
            std::memcpy(msg.data() + sizeof(VCapRec), f.Payload.data(), f.Payload.size());
        std::scoped_lock lk(si->mtx);
        if (si->pipe == INVALID_HANDLE_VALUE) return;
        DWORD written = 0;
        if (!WriteFile(si->pipe, msg.data(), static_cast<DWORD>(msg.size()), &written, NULL)) {
            CloseHandle(si->pipe);
            si->pipe = INVALID_HANDLE_VALUE;
        }
    };
    if (auto *eth = dynamic_cast<EthHub *>(w.bus.get())) eth->SetSubCb(cb);
    if (auto *can = dynamic_cast<CanBus *>(w.bus.get())) can->SetSubCb(cb);
}

static void detachSub(BusWrap &w) {
    if (auto *eth = dynamic_cast<EthHub *>(w.bus.get())) eth->SetSubCb(nullptr);
    if (auto *can = dynamic_cast<CanBus *>(w.bus.get())) can->SetSubCb(nullptr);
    std::scoped_lock lk(w.sub->mtx);
    if (w.sub->pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(w.sub->pipe);
        w.sub->pipe = INVALID_HANDLE_VALUE;
    }
}

static void attachRecorder(BusWrap &w, const std::string &path) {
    detachRecorder(w);
    w.rec = std::make_unique<Recorder>();
    if (!w.rec->Open(path)) {
        w.rec.reset();
        return;
    }
    if (auto *eth = dynamic_cast<EthHub *>(w.bus.get())) {
        eth->SetRecordCb([rec = w.rec.get()](const Frame &f) { rec->Write(f); });
    } else if (auto *can = dynamic_cast<CanBus *>(w.bus.get())) {
        can->SetRecordCb([rec = w.rec.get()](const Frame &f) { rec->Write(f); });
    }
}

static std::string list_buses() {
    std::ostringstream oss;
    std::scoped_lock   lk(g_mtx);
    for (auto &[n, wrap] : g_buses)
        oss << n << "\n";
    return oss.str();
}

static std::vector<std::string> split(const std::string &s) {
    std::istringstream       iss(s);
    std::vector<std::string> out;
    std::string              tok;
    while (iss >> tok) out.push_back(tok);
    return out;
}

static void handle_cmd(HANDLE hPipe, const std::string &line, std::string &resp, bool &stay_open) {
    auto args = split(line);
    if (args.empty()) { resp = "ERR empty"; return; }

    // ── create <name> eth <link_bps>
    // ── create <name> can <bitrate>
    if (args[0] == "create" && args.size() >= 4) {
        std::string name = args[1], type = args[2];
        std::scoped_lock lk(g_mtx);
        if (g_buses.count(name)) { resp = "ERR already exists"; return; }
        if (type == "eth") {
            uint64_t bps  = std::stoull(args[3]);
            g_buses[name] = BusWrap{std::make_unique<EthHub>(g_sched, g_clk, bps), nullptr};
            resp = "OK created eth";
        } else if (type == "can") {
            uint32_t br   = std::stoul(args[3]);
            g_buses[name] = BusWrap{std::make_unique<CanBus>(g_sched, g_clk, br), nullptr};
            resp = "OK created can";
        } else {
            resp = "ERR type";
        }
        return;
    }

    // ── delete <name>
    if (args[0] == "delete" && args.size() >= 2) {
        std::scoped_lock lk(g_mtx);
        auto it = g_buses.find(args[1]);
        if (it == g_buses.end()) { resp = "ERR no bus"; return; }
        detachSub(it->second);
        detachRecorder(it->second);
        if (it->second.cap) { it->second.cap->stop(); it->second.cap.reset(); }
        g_buses.erase(it);
        resp = "OK deleted";
        return;
    }

    // ── list
    if (args[0] == "list") {
        resp = list_buses();
        return;
    }

    // ── record <name> on <file>
    // ── record <name> off
    if (args[0] == "record" && args.size() >= 3) {
        std::scoped_lock lk(g_mtx);
        auto it = g_buses.find(args[1]);
        if (it == g_buses.end()) { resp = "ERR no bus"; return; }
        if (args[2] == "on" && args.size() >= 4) {
            attachRecorder(it->second, args[3]);
            resp = it->second.rec ? "OK rec on" : "ERR open failed";
        } else {
            detachRecorder(it->second);
            resp = "OK rec off";
        }
        return;
    }

    // ── stoprec <name>  (alias for record <name> off)
    if (args[0] == "stoprec" && args.size() >= 2) {
        std::scoped_lock lk(g_mtx);
        auto it = g_buses.find(args[1]);
        if (it == g_buses.end()) { resp = "ERR no bus"; return; }
        detachRecorder(it->second);
        resp = "OK rec off";
        return;
    }

    // ── stats <name>
    if (args[0] == "stats" && args.size() >= 2) {
        std::scoped_lock lk(g_mtx);
        auto it = g_buses.find(args[1]);
        if (it == g_buses.end()) { resp = "ERR no bus"; return; }
        uint64_t tx = 0, rx = 0, drops = 0;
        if (auto *eth = dynamic_cast<EthHub *>(it->second.bus.get())) {
            tx    = eth->Stats().Tx_frames.load(std::memory_order_relaxed);
            rx    = eth->Stats().Rx_frames.load(std::memory_order_relaxed);
            drops = eth->Stats().Drops.load(std::memory_order_relaxed);
        } else if (auto *can = dynamic_cast<CanBus *>(it->second.bus.get())) {
            tx    = can->Stats().Tx_frames.load(std::memory_order_relaxed);
            rx    = can->Stats().Rx_frames.load(std::memory_order_relaxed);
            drops = can->Stats().Drops.load(std::memory_order_relaxed);
        }
        std::ostringstream oss;
        oss << "tx=" << tx << " rx=" << rx << " drops=" << drops;
        resp = oss.str();
        return;
    }

    // ── send-eth <name> <hex>
    if (args[0] == "send-eth" && args.size() >= 3) {
        std::scoped_lock lk(g_mtx);
        auto it = g_buses.find(args[1]);
        if (it == g_buses.end()) { resp = "ERR no bus"; return; }
        Frame f;
        f.Proto   = Proto::ETH2;
        f.Payload = hex_to_bytes(args[2]);
        it->second.bus->Send(nullptr, std::move(f));
        resp = "OK sent";
        return;
    }

    // ── send-can <name> <id> <hex>  (CAN 2.0, payload <= 8 bytes)
    if (args[0] == "send-can" && args.size() >= 4) {
        auto payload = hex_to_bytes(args[3]);
        if (payload.size() > 8) { resp = "ERR payload too long (max 8 bytes for CAN 2.0)"; return; }
        std::scoped_lock lk(g_mtx);
        auto it = g_buses.find(args[1]);
        if (it == g_buses.end()) { resp = "ERR no bus"; return; }
        Frame f;
        f.Proto   = Proto::CAN20;
        f.Tag     = std::stoul(args[2], nullptr, 0); // accepts 0x... or decimal
        f.Payload = std::move(payload);
        it->second.bus->Send(nullptr, std::move(f));
        resp = "OK sent";
        return;
    }

    // ── send-canfd <name> <id> <hex>  (CAN FD, payload <= 64 bytes)
    if (args[0] == "send-canfd" && args.size() >= 4) {
        auto payload = hex_to_bytes(args[3]);
        if (payload.size() > 64) { resp = "ERR payload too long (max 64 bytes for CAN FD)"; return; }
        std::scoped_lock lk(g_mtx);
        auto it = g_buses.find(args[1]);
        if (it == g_buses.end()) { resp = "ERR no bus"; return; }
        Frame f;
        f.Proto   = Proto::CANFD;
        f.Tag     = std::stoul(args[2], nullptr, 0);
        f.Payload = std::move(payload);
        it->second.bus->Send(nullptr, std::move(f));
        resp = "OK sent";
        return;
    }

    // ── replay <name> <file> <exact|burst|scale:K>
    if (args[0] == "replay" && args.size() >= 4) {
        const std::string &name = args[1], &file = args[2], &mode = args[3];

        // Grab bus pointer without holding g_mtx for the full replay duration.
        IBus *busPtr = nullptr;
        {
            std::scoped_lock lk(g_mtx);
            auto it = g_buses.find(name);
            if (it == g_buses.end()) { resp = "ERR no bus"; return; }
            busPtr = it->second.bus.get();
        }

        Replayer rep;
        if (!rep.Open(file)) { resp = "ERR open"; return; }

        bool   do_timing = (mode != "burst");
        double scale     = 1.0;
        if (mode.rfind("scale:", 0) == 0) {
            try { scale = std::stod(mode.substr(6)); }
            catch (...) { resp = "ERR bad scale"; return; }
        }

        Frame    f;
        uint64_t first_cap_ts = 0;
        uint64_t replay_start = static_cast<uint64_t>(g_clk.Now().count());

        while (rep.Next(f)) {
            if (do_timing) {
                if (first_cap_ts == 0) first_cap_ts = f.Ts_ns;
                uint64_t offset_ns = static_cast<uint64_t>((f.Ts_ns - first_cap_ts) * scale);
                uint64_t fire_at   = replay_start + offset_ns;
                while (static_cast<uint64_t>(g_clk.Now().count()) < fire_at)
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
            busPtr->Send(nullptr, f);
        }
        resp = "OK replayed";
        return;
    }

    // ── capture-udp <name> <bindport>
    if (args[0] == "capture-udp" && args.size() >= 3) {
        std::scoped_lock lk(g_mtx);
        auto it = g_buses.find(args[1]);
        if (it == g_buses.end()) { resp = "ERR no bus"; return; }
        if (it->second.cap) { it->second.cap->stop(); }
        uint16_t port = static_cast<uint16_t>(std::stoul(args[2]));
        auto ep = std::make_unique<UdpEndpoint>(*it->second.bus, port);
        if (!ep->start()) { resp = "ERR bind failed"; return; }
        it->second.cap = std::move(ep);
        resp = "OK capturing udp";
        return;
    }

    // ── capture-tcp <name> <bindport> <targethost> <targetport>
    if (args[0] == "capture-tcp" && args.size() >= 5) {
        std::scoped_lock lk(g_mtx);
        auto it = g_buses.find(args[1]);
        if (it == g_buses.end()) { resp = "ERR no bus"; return; }
        if (it->second.cap) { it->second.cap->stop(); }
        uint16_t bindPort   = static_cast<uint16_t>(std::stoul(args[2]));
        uint16_t targetPort = static_cast<uint16_t>(std::stoul(args[4]));
        auto ep = std::make_unique<TcpProxy>(*it->second.bus, bindPort, args[3], targetPort);
        if (!ep->start()) { resp = "ERR listen failed"; return; }
        it->second.cap = std::move(ep);
        resp = "OK capturing tcp";
        return;
    }

    // ── stop-capture <name>
    if (args[0] == "stop-capture" && args.size() >= 2) {
        std::scoped_lock lk(g_mtx);
        auto it = g_buses.find(args[1]);
        if (it == g_buses.end()) { resp = "ERR no bus"; return; }
        if (it->second.cap) { it->second.cap->stop(); it->second.cap.reset(); }
        resp = "OK capture stopped";
        return;
    }

    // ── replay-udp <name> <file> <dsthost> <dstport> <exact|burst|scale:K>
    if (args[0] == "replay-udp" && args.size() >= 6) {
        const std::string &file    = args[2];
        const std::string &dstHost = args[3];
        uint16_t           dstPort = static_cast<uint16_t>(std::stoul(args[4]));
        const std::string &mode    = args[5];

        Replayer rep;
        if (!rep.Open(file)) { resp = "ERR open"; return; }

        WSADATA wd{};
        WSAStartup(MAKEWORD(2, 2), &wd);
        SOCKET sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock == INVALID_SOCKET) { resp = "ERR socket"; return; }

        sockaddr_in dst{};
        dst.sin_family      = AF_INET;
        dst.sin_port        = htons(dstPort);
        inet_pton(AF_INET, dstHost.c_str(), &dst.sin_addr);

        bool   do_timing = (mode != "burst");
        double scale     = 1.0;
        if (mode.rfind("scale:", 0) == 0) {
            try { scale = std::stod(mode.substr(6)); }
            catch (...) { closesocket(sock); resp = "ERR bad scale"; return; }
        }

        Frame    f;
        uint64_t first_cap_ts = 0;
        uint64_t replay_start = static_cast<uint64_t>(g_clk.Now().count());

        while (rep.Next(f)) {
            if (f.Proto != Proto::UDP) continue;
            if (do_timing) {
                if (first_cap_ts == 0) first_cap_ts = f.Ts_ns;
                uint64_t offset_ns = static_cast<uint64_t>((f.Ts_ns - first_cap_ts) * scale);
                uint64_t fire_at   = replay_start + offset_ns;
                while (static_cast<uint64_t>(g_clk.Now().count()) < fire_at)
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
            ::sendto(sock, reinterpret_cast<const char *>(f.Payload.data()),
                     static_cast<int>(f.Payload.size()), 0,
                     reinterpret_cast<const sockaddr *>(&dst), sizeof(dst));
        }
        closesocket(sock);
        resp = "OK replayed udp";
        return;
    }

    // ── replay-sync <mode> <file1> <host1> <port1> [<file2> <host2> <port2> ...]
    //
    // Replays multiple .vbuscap files simultaneously as UDP datagrams.
    // All streams share a single time origin (global minimum Ts_ns across all
    // files) so inter-stream timing is preserved exactly as captured.
    //
    // mode = exact | burst | scale:K
    // Each stream is described by 3 arguments: <capturefile> <desthost> <destport>
    // Minimum: one stream → 5 args total.
    if (args[0] == "replay-sync" && args.size() >= 5 && (args.size() - 2) % 3 == 0) {
        const std::string &mode = args[1];

        bool   do_timing = (mode != "burst");
        double scale     = 1.0;
        if (mode.rfind("scale:", 0) == 0) {
            try { scale = std::stod(mode.substr(6)); }
            catch (...) { resp = "ERR bad scale"; return; }
        }

        struct StreamDesc { std::string file, host; uint16_t port; };
        std::vector<StreamDesc> streams;
        for (size_t i = 2; i + 3 <= args.size(); i += 3) {
            StreamDesc sd;
            sd.file = args[i];
            sd.host = args[i + 1];
            sd.port = static_cast<uint16_t>(std::stoul(args[i + 2]));
            streams.push_back(std::move(sd));
        }

        // ── Phase 1: validate all files and find global first timestamp ──────
        uint64_t global_first_ts = UINT64_MAX;
        for (auto &sd : streams) {
            Replayer peek;
            if (!peek.Open(sd.file)) {
                resp = "ERR open failed: " + sd.file;
                return;
            }
            Frame f;
            if (peek.Next(f) && f.Ts_ns > 0 && f.Ts_ns < global_first_ts)
                global_first_ts = f.Ts_ns;
        }
        if (global_first_ts == UINT64_MAX) global_first_ts = 0;

        // ── Phase 2: launch one thread per stream, all aligned to the same ───
        //            wall-clock start point (50 ms from now for startup slack).
        const uint64_t replay_start = static_cast<uint64_t>(g_clk.Now().count())
                                      + 50'000'000ULL; // +50 ms

        for (auto sd : streams) {
            std::thread([sd, global_first_ts, replay_start, do_timing, scale]() {
                Replayer rep;
                if (!rep.Open(sd.file)) return;

                SOCKET sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
                if (sock == INVALID_SOCKET) return;

                int sndbuf = 4 * 1024 * 1024;
                ::setsockopt(sock, SOL_SOCKET, SO_SNDBUF,
                             reinterpret_cast<const char *>(&sndbuf), sizeof(sndbuf));

                sockaddr_in dst{};
                dst.sin_family = AF_INET;
                dst.sin_port   = htons(sd.port);
                inet_pton(AF_INET, sd.host.c_str(), &dst.sin_addr);

                // Wait for the shared start time
                while (static_cast<uint64_t>(g_clk.Now().count()) < replay_start)
                    std::this_thread::sleep_for(std::chrono::microseconds(100));

                Frame f;
                while (rep.Next(f)) {
                    if (f.Proto != Proto::UDP) continue;
                    if (do_timing) {
                        uint64_t offset_ns = static_cast<uint64_t>(
                            static_cast<double>(f.Ts_ns - global_first_ts) * scale);
                        uint64_t fire_at = replay_start + offset_ns;
                        while (static_cast<uint64_t>(g_clk.Now().count()) < fire_at)
                            std::this_thread::sleep_for(std::chrono::microseconds(100));
                    }
                    ::sendto(sock,
                             reinterpret_cast<const char *>(f.Payload.data()),
                             static_cast<int>(f.Payload.size()), 0,
                             reinterpret_cast<const sockaddr *>(&dst), sizeof(dst));
                }
                closesocket(sock);
            }).detach();
        }

        resp = "OK sync started " + std::to_string(streams.size()) + " streams";
        return;
    }

    // ── subscribe <name>  – keeps this pipe connection open; streams frames
    if (args[0] == "subscribe" && args.size() >= 2) {
        std::scoped_lock lk(g_mtx);
        auto it = g_buses.find(args[1]);
        if (it == g_buses.end()) { resp = "ERR no bus"; return; }
        resp = "OK stream";
        stay_open = true;
        attachSub(it->second, hPipe);
        return;
    }

    // ── quit
    if (args[0] == "quit") {
        resp = "OK bye";
        g_shutdown.store(true);
        g_shutdown_cv.notify_all();
        return;
    }

    resp = "ERR cmd";
}

static void pipe_thread() {
    const wchar_t *pipename = L"\\\\.\\pipe\\vbusd";
    while (!g_shutdown.load()) {
        HANDLE hPipe = CreateNamedPipeW(
            pipename,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            4096, 4096, 0, NULL);
        if (hPipe == INVALID_HANDLE_VALUE) {
            std::cerr << "CreateNamedPipe failed: " << GetLastError() << "\n";
            return;
        }
        BOOL connected = ConnectNamedPipe(hPipe, NULL) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (connected) {
            std::thread([hPipe]() {
                char  buf[65536];
                DWORD nread     = 0;
                bool  stay_open = false;
                while (ReadFile(hPipe, buf, sizeof(buf) - 1, &nread, NULL)) {
                    buf[nread] = 0;
                    std::string line(buf, nread);
                    while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
                        line.pop_back();
                    std::string resp;
                    handle_cmd(hPipe, line, resp, stay_open);
                    resp.push_back('\n');
                    DWORD written = 0;
                    WriteFile(hPipe, resp.data(), (DWORD)resp.size(), &written, NULL);
                    if (stay_open) return; // hPipe now owned by SubInfo
                }
                FlushFileBuffers(hPipe);
                DisconnectNamedPipe(hPipe);
                CloseHandle(hPipe);
            }).detach();
        } else {
            CloseHandle(hPipe);
        }
    }
}

int main() {
    std::thread t(pipe_thread);
    t.detach();
    std::cout << "vbusd running. Pipe: \\\\.\\pipe\\vbusd\n";
    std::cout << "Send 'quit' to stop.\n";

    // Main loop: drain scheduled events every 1 ms; exit on shutdown signal.
    std::unique_lock<std::mutex> lk(g_shutdown_mtx);
    while (!g_shutdown.load()) {
        g_shutdown_cv.wait_for(lk, std::chrono::milliseconds(1));
        g_sched.run_until(g_clk.Now());
    }

    // Flush all open recorders before exit.
    {
        std::scoped_lock blk(g_mtx);
        for (auto &[n, wrap] : g_buses)
            if (wrap.rec) wrap.rec->Close();
    }
    std::cout << "vbusd stopped.\n";
    return 0;
}
