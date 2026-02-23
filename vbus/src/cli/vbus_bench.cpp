// vbus_bench – UDP / TCP traffic generator for OpenVBus capture testing
//
// Usage:
//   vbus_bench udp  <host> <port> <rate> [duration_sec]
//   vbus_bench tcp  <host> <port> <rate> [duration_sec]
//   vbus_bench recv <port>                              (passive sink, prints stats)
//
// Rate examples:  1g  10g  100m  500k  (bits/s suffix: g/m/k, default=bits)
// Duration default: 10 seconds  (0 = run until Ctrl-C)
//
// Examples:
//   vbus_bench udp 127.0.0.1 9000 1g 30
//   vbus_bench tcp 127.0.0.1 9001 10g
//   vbus_bench recv 9000

#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <chrono>
#include <algorithm>

using Clock = std::chrono::steady_clock;
using ns    = std::chrono::nanoseconds;

// ─── Helpers ──────────────────────────────────────────────────────────────────

static uint64_t parse_rate(const char *s) {
    double v = std::stod(s);
    std::string str(s);
    char suf = str.empty() ? 0 : (char)std::tolower((unsigned char)str.back());
    if (suf == 'g') v *= 1e9;
    else if (suf == 'm') v *= 1e6;
    else if (suf == 'k') v *= 1e3;
    return static_cast<uint64_t>(v);
}

static void usage() {
    std::fprintf(stderr,
        "Usage:\n"
        "  vbus_bench udp  <host> <port> <rate> [duration_sec]\n"
        "  vbus_bench tcp  <host> <port> <rate> [duration_sec]\n"
        "  vbus_bench recv <port>\n"
        "\n"
        "Rate: 1g  10g  100m  500k  (bits/sec)\n"
        "Examples:\n"
        "  vbus_bench udp  127.0.0.1 9000 1g 30\n"
        "  vbus_bench tcp  127.0.0.1 9000 10g\n"
        "  vbus_bench recv 9000\n");
}

// Pick payload size to keep per-packet overhead manageable at high rates.
static int payload_size_for_rate(uint64_t bps) {
    if (bps >= 5'000'000'000ULL) return 65000; // ~10G
    if (bps >= 500'000'000ULL)   return 16384; // ~1G
    if (bps >= 50'000'000ULL)    return 4096;  // ~100M
    return 1400;                               // <=100M – near-MTU UDP
}

// ─── Stats reporter ───────────────────────────────────────────────────────────

struct Stats {
    std::atomic<uint64_t> bytes{0};
    std::atomic<uint64_t> packets{0};
    std::atomic<uint64_t> errors{0};
};

static std::atomic<bool> g_stop{false};

static void reporter_thread(const Stats &st, const char *label) {
    auto prev_bytes = 0ULL, prev_pkts = 0ULL;
    auto t0 = Clock::now();
    int  sec = 0;

    std::printf("\n%-6s  %10s  %12s  %10s  %8s\n",
                "Sec", "Pkts/s", "Bits/s", "Total MB", "Errors");
    std::printf("%-6s  %10s  %12s  %10s  %8s\n",
                "------", "----------", "------------", "----------", "--------");

    while (!g_stop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        ++sec;
        uint64_t b   = st.bytes.load(std::memory_order_relaxed);
        uint64_t p   = st.packets.load(std::memory_order_relaxed);
        uint64_t e   = st.errors.load(std::memory_order_relaxed);
        uint64_t db  = b - prev_bytes;
        uint64_t dp  = p - prev_pkts;
        prev_bytes   = b;
        prev_pkts    = p;
        double mbits = static_cast<double>(db) * 8.0 / 1e6;
        std::printf("[%s] %-4d  %10llu  %11.1fM  %10.2f  %8llu\n",
                    label, sec,
                    (unsigned long long)dp,
                    mbits,
                    static_cast<double>(b) / 1e6,
                    (unsigned long long)e);
        std::fflush(stdout);
    }
}

// ─── CTRL-C handler ───────────────────────────────────────────────────────────

static BOOL WINAPI ctrl_handler(DWORD) {
    g_stop.store(true);
    return TRUE;
}

// ─── UDP sender ───────────────────────────────────────────────────────────────

static int run_udp_send(const char *host, uint16_t port,
                        uint64_t rate_bps, uint64_t duration_ns) {
    SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) { std::fprintf(stderr, "socket failed\n"); return 1; }

    // Set send buffer large enough for high-rate bursts
    int sndbuf = 4 * 1024 * 1024;
    setsockopt(s, SOL_SOCKET, SO_SNDBUF, (const char *)&sndbuf, sizeof(sndbuf));

    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port   = htons(port);
    inet_pton(AF_INET, host, &dst.sin_addr);

    const int    psize   = payload_size_for_rate(rate_bps);
    std::vector<char> buf(psize, 0);
    // Fill with a ramp pattern so the receiver can detect corruption
    for (int i = 0; i < psize; i++) buf[i] = static_cast<char>(i & 0x7F);

    // Token bucket: tokens = bits available to send.
    // Refill every iteration based on elapsed time.
    const uint64_t pkt_bits  = static_cast<uint64_t>(psize) * 8;
    uint64_t       tokens    = pkt_bits; // start with one packet worth
    auto           last_fill = Clock::now();
    const auto     t_start   = Clock::now();

    Stats st;
    std::thread rep([&]{ reporter_thread(st, "UDP"); });

    std::printf("UDP sender: %s:%u  payload=%d B  rate=%.2f Gbit/s  duration=%llu s\n",
                host, port, psize,
                static_cast<double>(rate_bps) / 1e9,
                (unsigned long long)(duration_ns / 1'000'000'000ULL));

    while (!g_stop.load()) {
        // Refill tokens
        auto now     = Clock::now();
        auto elapsed = std::chrono::duration_cast<ns>(now - last_fill).count();
        last_fill    = now;
        uint64_t new_tokens = static_cast<uint64_t>(
            (double)rate_bps * (double)elapsed / 1e9);
        tokens += new_tokens;
        // Cap bucket at 4× packet to limit burst
        const uint64_t max_tokens = pkt_bits * 4;
        if (tokens > max_tokens) tokens = max_tokens;

        // Duration check
        if (duration_ns > 0) {
            uint64_t age = static_cast<uint64_t>(
                std::chrono::duration_cast<ns>(now - t_start).count());
            if (age >= duration_ns) break;
        }

        if (tokens >= pkt_bits) {
            int sent = ::sendto(s,
                                buf.data(), psize, 0,
                                reinterpret_cast<const sockaddr*>(&dst), sizeof(dst));
            if (sent > 0) {
                st.bytes.fetch_add(static_cast<uint64_t>(sent), std::memory_order_relaxed);
                st.packets.fetch_add(1, std::memory_order_relaxed);
                tokens -= pkt_bits;
            } else {
                st.errors.fetch_add(1, std::memory_order_relaxed);
            }
        } else {
            // Sleep proportionally to the deficit to avoid spinning the CPU flat-out
            // at lower rates while still being accurate at high rates.
            uint64_t deficit_bits = pkt_bits - tokens;
            uint64_t wait_ns = (rate_bps > 0)
                ? (deficit_bits * 1'000'000'000ULL / rate_bps)
                : 1'000'000ULL;
            if (wait_ns > 2'000'000ULL) { // >2ms – use OS sleep
                std::this_thread::sleep_for(ns(wait_ns - 500'000ULL));
            } else if (wait_ns > 50'000ULL) { // >50µs – yield
                std::this_thread::yield();
            }
            // <50µs – busy-spin (needed for accurate 10G pacing)
        }
    }

    g_stop.store(true);
    rep.join();
    closesocket(s);

    uint64_t total_b = st.bytes.load();
    std::printf("\nDone. Total: %.2f MB  (%.2f Gbit/s avg)\n",
                static_cast<double>(total_b) / 1e6,
                static_cast<double>(total_b) * 8.0 / 1e9 /
                    std::max(1.0, static_cast<double>(
                        std::chrono::duration_cast<std::chrono::seconds>(
                            Clock::now() - t_start).count())));
    return 0;
}

// ─── TCP sender ───────────────────────────────────────────────────────────────

static int run_tcp_send(const char *host, uint16_t port,
                        uint64_t rate_bps, uint64_t duration_ns) {
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) { std::fprintf(stderr, "socket failed\n"); return 1; }

    int sndbuf = 8 * 1024 * 1024;
    setsockopt(s, SOL_SOCKET, SO_SNDBUF, (const char *)&sndbuf, sizeof(sndbuf));

    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port   = htons(port);
    inet_pton(AF_INET, host, &dst.sin_addr);

    if (::connect(s, reinterpret_cast<sockaddr*>(&dst), sizeof(dst)) != 0) {
        std::fprintf(stderr, "connect failed (is vbus_bench recv running?)\n");
        closesocket(s);
        return 1;
    }

    const int psize = payload_size_for_rate(rate_bps);
    std::vector<char> buf(psize, 0);
    for (int i = 0; i < psize; i++) buf[i] = static_cast<char>(i & 0x7F);

    const uint64_t pkt_bits = static_cast<uint64_t>(psize) * 8;
    uint64_t       tokens   = pkt_bits;
    auto           last_fill = Clock::now();
    const auto     t_start   = Clock::now();

    Stats st;
    std::thread rep([&]{ reporter_thread(st, "TCP"); });

    std::printf("TCP sender: %s:%u  chunk=%d B  rate=%.2f Gbit/s  duration=%llu s\n",
                host, port, psize,
                static_cast<double>(rate_bps) / 1e9,
                (unsigned long long)(duration_ns / 1'000'000'000ULL));

    while (!g_stop.load()) {
        auto now     = Clock::now();
        auto elapsed = std::chrono::duration_cast<ns>(now - last_fill).count();
        last_fill    = now;
        uint64_t refill = static_cast<uint64_t>((double)rate_bps * (double)elapsed / 1e9);
        tokens += refill;
        if (tokens > pkt_bits * 4) tokens = pkt_bits * 4;

        if (duration_ns > 0) {
            uint64_t age = static_cast<uint64_t>(
                std::chrono::duration_cast<ns>(now - t_start).count());
            if (age >= duration_ns) break;
        }

        if (tokens >= pkt_bits) {
            int sent = ::send(s, buf.data(), psize, 0);
            if (sent > 0) {
                st.bytes.fetch_add(static_cast<uint64_t>(sent), std::memory_order_relaxed);
                st.packets.fetch_add(1, std::memory_order_relaxed);
                tokens -= static_cast<uint64_t>(sent) * 8;
            } else {
                st.errors.fetch_add(1, std::memory_order_relaxed);
                if (sent < 0) break; // connection dropped
            }
        } else {
            uint64_t deficit_bits = pkt_bits - tokens;
            uint64_t wait_ns = (rate_bps > 0)
                ? (deficit_bits * 1'000'000'000ULL / rate_bps) : 1'000'000ULL;
            if (wait_ns > 2'000'000ULL)
                std::this_thread::sleep_for(ns(wait_ns - 500'000ULL));
            else if (wait_ns > 50'000ULL)
                std::this_thread::yield();
        }
    }

    g_stop.store(true);
    rep.join();
    closesocket(s);

    uint64_t total_b = st.bytes.load();
    std::printf("\nDone. Total: %.2f MB  (%.2f Gbit/s avg)\n",
                static_cast<double>(total_b) / 1e6,
                static_cast<double>(total_b) * 8.0 / 1e9 /
                    std::max(1.0, static_cast<double>(
                        std::chrono::duration_cast<std::chrono::seconds>(
                            Clock::now() - t_start).count())));
    return 0;
}

// ─── Passive sink (recv) ──────────────────────────────────────────────────────
// Accepts a single TCP connection OR receives UDP datagrams and just counts bytes.

static int run_recv(uint16_t port) {
    // Try UDP
    SOCKET udp = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    SOCKET tcp = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    int reuse = 1;
    setsockopt(udp, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
    setsockopt(tcp, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    int rcvbuf = 8 * 1024 * 1024;
    setsockopt(udp, SOL_SOCKET, SO_RCVBUF, (const char*)&rcvbuf, sizeof(rcvbuf));
    setsockopt(tcp, SOL_SOCKET, SO_RCVBUF, (const char*)&rcvbuf, sizeof(rcvbuf));

    bind(udp, (sockaddr*)&addr, sizeof(addr));
    bind(tcp, (sockaddr*)&addr, sizeof(addr));
    listen(tcp, 1);

    std::printf("Passive sink on port %u (UDP + TCP). Press Ctrl-C to stop.\n", port);

    Stats st;
    std::thread rep([&]{ reporter_thread(st, "RECV"); });

    std::vector<char> buf(65536);

    // UDP receive thread
    std::thread udp_t([&]() {
        sockaddr_in src{};
        int srclen = sizeof(src);
        // Set 500ms timeout so stop flag is checked
        DWORD tv = 500;
        setsockopt(udp, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
        while (!g_stop.load()) {
            int n = ::recvfrom(udp, buf.data(), (int)buf.size(), 0,
                               (sockaddr*)&src, &srclen);
            if (n > 0) {
                st.bytes.fetch_add(static_cast<uint64_t>(n), std::memory_order_relaxed);
                st.packets.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    // TCP accept thread
    std::thread tcp_t([&]() {
        // Non-blocking accept via select
        while (!g_stop.load()) {
            fd_set rd;
            FD_ZERO(&rd);
            FD_SET(tcp, &rd);
            timeval tv{0, 200'000}; // 200ms
            if (select(0, &rd, nullptr, nullptr, &tv) <= 0) continue;
            SOCKET client = ::accept(tcp, nullptr, nullptr);
            if (client == INVALID_SOCKET) continue;
            std::printf("[sink] TCP client connected\n");
            DWORD timeout = 500;
            setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
            while (!g_stop.load()) {
                int n = ::recv(client, buf.data(), (int)buf.size(), 0);
                if (n <= 0) break;
                st.bytes.fetch_add(static_cast<uint64_t>(n), std::memory_order_relaxed);
                st.packets.fetch_add(1, std::memory_order_relaxed);
            }
            closesocket(client);
            std::printf("[sink] TCP client disconnected\n");
        }
    });

    udp_t.join();
    tcp_t.join();
    rep.join();

    closesocket(udp);
    closesocket(tcp);
    return 0;
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char **argv) {
    if (argc < 2) { usage(); return 1; }

    WSADATA wd{};
    WSAStartup(MAKEWORD(2, 2), &wd);
    SetConsoleCtrlHandler(ctrl_handler, TRUE);

    std::string mode(argv[1]);

    if (mode == "recv") {
        if (argc < 3) { usage(); return 1; }
        uint16_t port = static_cast<uint16_t>(std::stoul(argv[2]));
        return run_recv(port);
    }

    if (mode == "udp" || mode == "tcp") {
        if (argc < 5) { usage(); return 1; }
        const char *host    = argv[2];
        uint16_t    port    = static_cast<uint16_t>(std::stoul(argv[3]));
        uint64_t    rate    = parse_rate(argv[4]);
        uint64_t    dur_ns  = 0;
        if (argc >= 6) dur_ns = std::stoull(argv[5]) * 1'000'000'000ULL;

        if (mode == "udp") return run_udp_send(host, port, rate, dur_ns);
        else               return run_tcp_send(host, port, rate, dur_ns);
    }

    usage();
    return 1;
}
