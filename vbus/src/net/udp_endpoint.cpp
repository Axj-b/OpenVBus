#include "udp_endpoint.h"
#include "../bus/frame.h"

#include <cstring>
#include <iostream>
#include <chrono>

#ifdef _WIN32
#  pragma comment(lib, "ws2_32.lib")
#else
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <fcntl.h>
#  define closesocket close
#endif

namespace vbus {

UdpEndpoint::UdpEndpoint(IBus &bus, std::string bindHost, uint16_t port)
    : m_Bus(bus), m_BindHost(std::move(bindHost)), m_Port(port) {}

bool UdpEndpoint::start() {
    if (m_Running.load()) return true;

#ifdef _WIN32
    WSADATA wd{};
    WSAStartup(MAKEWORD(2, 2), &wd);
#endif

    m_Sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_Sock == INVALID_SOCKET) {
        std::cerr << "[UdpEndpoint] socket() failed\n";
        return false;
    }

    // Allow address reuse
    int opt = 1;
    ::setsockopt(m_Sock, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char *>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(m_Port);
    if (inet_pton(AF_INET, m_BindHost.c_str(), &addr.sin_addr) != 1)
        addr.sin_addr.s_addr = INADDR_ANY; // fall back if host is empty/invalid

    if (::bind(m_Sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        std::cerr << "[UdpEndpoint] bind() failed on " << m_BindHost << ":" << m_Port << "\n";
        closesocket(m_Sock);
        m_Sock = INVALID_SOCKET;
        return false;
    }

    // Set receive timeout so the loop can check m_Running periodically
#ifdef _WIN32
    DWORD tv = 500; // milliseconds
    ::setsockopt(m_Sock, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char *>(&tv), sizeof(tv));
#else
    timeval tv{0, 500000}; // 500 ms
    ::setsockopt(m_Sock, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char *>(&tv), sizeof(tv));
#endif

    m_Running.store(true);
    m_Thread = std::thread(&UdpEndpoint::recvLoop, this);
    std::cout << "[UdpEndpoint] listening on UDP " << m_BindHost << ":" << m_Port << "\n";
    return true;
}

void UdpEndpoint::stop() {
    if (!m_Running.exchange(false)) return;
    if (m_Sock != INVALID_SOCKET) {
        closesocket(m_Sock);
        m_Sock = INVALID_SOCKET;
    }
    if (m_Thread.joinable())
        m_Thread.join();
    std::cout << "[UdpEndpoint] stopped\n";
}

void UdpEndpoint::recvLoop() {
    constexpr size_t kMaxDgram = 65535;
    std::vector<std::byte> buf(kMaxDgram);

    while (m_Running.load()) {
        sockaddr_in src{};
        socklen_t   srcLen = sizeof(src);

        int n = static_cast<int>(
            ::recvfrom(m_Sock,
                       reinterpret_cast<char *>(buf.data()),
                       static_cast<int>(buf.size()),
                       0,
                       reinterpret_cast<sockaddr *>(&src),
                       &srcLen));

        if (n <= 0) continue; // timeout or shutdown

        // Build Frame
        Frame f;
        f.Proto   = Proto::UDP;
        f.Ts_ns   = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());

        // Pack tag: [src_ip 32][src_port 16][dst_port 16]
        uint64_t src_ip   = static_cast<uint64_t>(src.sin_addr.s_addr); // network order
        uint64_t src_port = static_cast<uint64_t>(ntohs(src.sin_port));
        f.Tag = (src_ip << 32) | (src_port << 16) | static_cast<uint64_t>(m_Port);

        f.Payload.assign(buf.begin(), buf.begin() + n);

        // Inject into bus (→ triggers Recorder tap if attached)
        m_Bus.Send(nullptr, std::move(f));
    }
}

} // namespace vbus
