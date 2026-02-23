
#include <windows.h>
#include <string>
#include <thread>
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

using namespace vbus;

struct BusWrap {
    std::unique_ptr<IBus>     bus;
    std::unique_ptr<Recorder> rec;
};

static std::mutex                               g_mtx;
static std::unordered_map<std::string, BusWrap> g_buses;
static RealtimeClock                            g_clk;
static Scheduler                                g_sched(g_clk);

static void attachRecorder(const std::string &name, BusWrap &w, const std::string &path) {
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
    for (auto &[n, wrap] : g_buses) {
        oss << n << "\n";
    }
    return oss.str();
}

static std::vector<std::string> split(const std::string &s) {
    std::istringstream       iss(s);
    std::vector<std::string> out;
    std::string              tok;
    while (iss >> tok) out.push_back(tok);
    return out;
}

static void handle_cmd(const std::string &line, std::string &resp) {
    auto args = split(line);
    if (args.empty()) {
        resp = "ERR empty";
        return;
    }
    if (args[0] == "create" && args.size() >= 4) {
        std::string      name = args[1], type = args[2];
        std::scoped_lock lk(g_mtx);
        if (type == "eth") {
            uint64_t bps  = std::stoull(args[3]);
            auto     hub  = std::make_unique<EthHub>(g_sched, g_clk, bps);
            g_buses[name] = BusWrap{std::move(hub), nullptr};
            resp          = "OK created eth";
            return;
        } else if (type == "can") {
            uint32_t br   = std::stoul(args[3]);
            auto     can  = std::make_unique<CanBus>(g_sched, g_clk, br);
            g_buses[name] = BusWrap{std::move(can), nullptr};
            resp          = "OK created can";
            return;
        }
        resp = "ERR type";
    } else if (args[0] == "list") {
        resp = list_buses();
    } else if (args[0] == "record" && args.size() >= 4) {
        std::string      name = args[1], onoff = args[2], path = args[3];
        std::scoped_lock lk(g_mtx);
        auto             it = g_buses.find(name);
        if (it == g_buses.end()) {
            resp = "ERR no bus";
            return;
        }
        if (onoff == "on") {
            attachRecorder(name, it->second, path);
            resp = "OK rec on";
        } else {
            it->second.rec.reset();
            if (auto *eth = dynamic_cast<EthHub *>(it->second.bus.get()))
                eth->SetRecordCb(nullptr);
            if (auto *can = dynamic_cast<CanBus *>(it->second.bus.get()))
                can->SetRecordCb(nullptr);
            resp = "OK rec off";
        }
    } else if (args[0] == "send-eth" && args.size() >= 3) {
        std::string      name = args[1], hex = args[2];
        std::scoped_lock lk(g_mtx);
        auto             it = g_buses.find(name);
        if (it == g_buses.end()) {
            resp = "ERR no bus";
            return;
        }
        Frame f;
        f.Proto   = Proto::ETH2;
        f.Payload = hex_to_bytes(hex);
        it->second.bus->Send(nullptr, std::move(f));
        resp = "OK sent";
    } else if (args[0] == "send-can" && args.size() >= 4) {
        std::string      name = args[1];
        uint32_t         id   = std::stoul(args[2]);
        std::string      hex  = args[3];
        std::scoped_lock lk(g_mtx);
        auto             it = g_buses.find(name);
        if (it == g_buses.end()) {
            resp = "ERR no bus";
            return;
        }
        Frame f;
        f.Proto   = Proto::CAN20;
        f.Tag     = id;
        f.Payload = hex_to_bytes(hex);
        it->second.bus->Send(nullptr, std::move(f));
        resp = "OK sent";
    } else if (args[0] == "replay" && args.size() >= 4) {
        std::string      name = args[1], file = args[2], mode = args[3];
        std::scoped_lock lk(g_mtx);
        auto             it = g_buses.find(name);
        if (it == g_buses.end()) {
            resp = "ERR no bus";
            return;
        }
        // crude synchronous replay
        Replayer rep;
        if (!rep.Open(file)) {
            resp = "ERR open";
            return;
        }
        Frame    f;
        uint64_t last_ts = 0;
        while (rep.Next(f)) {
            if (mode == "exact" && last_ts != 0) {
                auto now    = std::chrono::duration_cast<std::chrono::nanoseconds>(g_clk.Now()).count();
                auto target = f.Ts_ns;
                // Busy-wait sleep for MVP
                while (std::chrono::duration_cast<std::chrono::nanoseconds>(g_clk.Now()).count() < target) {
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                }
            }
            it->second.bus->Send(nullptr, f);
            last_ts = f.Ts_ns;
        }
        resp = "OK replayed";
    } else if (args[0] == "quit") {
        resp = "BYE";
        ExitProcess(0);
    } else {
        resp = "ERR cmd";
    }
}

static void pipe_thread() {
    const wchar_t *pipename = L"\\\\.\\pipe\\vbusd";
    while (true) {
        HANDLE hPipe = CreateNamedPipeW(
            pipename,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            4096, 4096, 0, NULL);
        if (hPipe == INVALID_HANDLE_VALUE) {
            std::cerr << "CreateNamedPipe failed\n";
            return;
        }
        BOOL connected = ConnectNamedPipe(hPipe, NULL) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (connected) {
            std::thread([hPipe]() {
                char  buf[2048];
                DWORD read = 0;
                while (ReadFile(hPipe, buf, sizeof(buf) - 1, &read, NULL)) {
                    buf[read] = 0;
                    std::string line(buf);
                    // trim CRLF
                    if (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
                        line.pop_back();
                    std::string resp;
                    handle_cmd(line, resp);
                    resp.push_back('\n');
                    DWORD written = 0;
                    WriteFile(hPipe, resp.data(), (DWORD)resp.size(), &written, NULL);
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
    // Event loop placeholder
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        g_sched.run_until(g_clk.Now());
    }
    return 0;
}
