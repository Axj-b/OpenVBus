#include "udp_sink.h"
#include "../bus/frame.h"

#include <cstring>
#include <iostream>

#ifdef _WIN32
#  pragma comment(lib, "ws2_32.lib")
#else
#  include <arpa/inet.h>
#  include <netdb.h>
#  define closesocket close
#endif

namespace vbus {

UdpSink::UdpSink(IBus &bus, std::string dstHost, uint16_t dstPort)
    : m_Bus(bus), m_DstHost(std::move(dstHost)), m_DstPort(dstPort) {}

bool UdpSink::start() {
    if (m_Active) return true;

#ifdef _WIN32
    WSADATA wd{};
    WSAStartup(MAKEWORD(2, 2), &wd);
#endif

    addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    char portstr[8];
    std::snprintf(portstr, sizeof(portstr), "%u", m_DstPort);
    if (getaddrinfo(m_DstHost.c_str(), portstr, &hints, &res) != 0 || !res) {
        std::cerr << "[UdpSink] cannot resolve host: " << m_DstHost << "\n";
        return false;
    }
    m_Dst = *reinterpret_cast<sockaddr_in *>(res->ai_addr);
    freeaddrinfo(res);

    m_Sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_Sock == INVALID_SOCKET) {
        std::cerr << "[UdpSink] socket() failed\n";
        return false;
    }
    int sndbuf = 4 * 1024 * 1024;
    ::setsockopt(m_Sock, SOL_SOCKET, SO_SNDBUF,
                 reinterpret_cast<const char *>(&sndbuf), sizeof(sndbuf));

    // Register as a forward callback — fires immediately in Send(),
    // on the sender's thread, before any scheduler delay is applied.
    auto cb = [this](const Frame &f) { forward(f); };
    if (auto *eth = dynamic_cast<EthHub *>(&m_Bus))
        eth->SetFwdCb(cb);
    else if (auto *can = dynamic_cast<CanBus *>(&m_Bus))
        can->SetFwdCb(cb);

    m_Active = true;
    std::cout << "[UdpSink] forwarding to " << m_DstHost << ":" << m_DstPort << "\n";
    return true;
}

void UdpSink::stop() {
    if (!m_Active) return;
    // Clear the forward callback.
    if (auto *eth = dynamic_cast<EthHub *>(&m_Bus))
        eth->SetFwdCb(nullptr);
    else if (auto *can = dynamic_cast<CanBus *>(&m_Bus))
        can->SetFwdCb(nullptr);
    if (m_Sock != INVALID_SOCKET) {
        closesocket(m_Sock);
        m_Sock = INVALID_SOCKET;
    }
    m_Active = false;
    std::cout << "[UdpSink] stopped\n";
}

void UdpSink::forward(const Frame &f) {
    if (f.Payload.empty() || m_Sock == INVALID_SOCKET) return;
    ::sendto(m_Sock,
             reinterpret_cast<const char *>(f.Payload.data()),
             static_cast<int>(f.Payload.size()), 0,
             reinterpret_cast<const sockaddr *>(&m_Dst), sizeof(m_Dst));
}

} // namespace vbus
