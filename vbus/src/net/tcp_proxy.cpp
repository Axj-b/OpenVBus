#include "tcp_proxy.h"
#include "../bus/frame.h"

#include <cstring>
#include <iostream>
#include <chrono>

#ifdef _WIN32
#  pragma comment(lib, "ws2_32.lib")
#else
#  include <arpa/inet.h>
#  include <netdb.h>
#  define closesocket close
#  define SOCKET_ERROR (-1)
#endif

namespace vbus {

TcpProxy::TcpProxy(IBus &bus, uint16_t bindPort,
                   std::string targetHost, uint16_t targetPort)
    : m_Bus(bus)
    , m_BindPort(bindPort)
    , m_TargetHost(std::move(targetHost))
    , m_TargetPort(targetPort) {}

bool TcpProxy::start() {
    if (m_Running.load()) return true;

#ifdef _WIN32
    WSADATA wd{};
    WSAStartup(MAKEWORD(2, 2), &wd);
#endif

    m_Listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_Listener == INVALID_SOCKET) {
        std::cerr << "[TcpProxy] socket() failed\n";
        return false;
    }

    int opt = 1;
    ::setsockopt(m_Listener, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char *>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(m_BindPort);

    if (::bind(m_Listener, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        std::cerr << "[TcpProxy] bind() failed on port " << m_BindPort << "\n";
        closesocket(m_Listener);
        m_Listener = INVALID_SOCKET;
        return false;
    }

    if (::listen(m_Listener, 1) != 0) {
        std::cerr << "[TcpProxy] listen() failed\n";
        closesocket(m_Listener);
        m_Listener = INVALID_SOCKET;
        return false;
    }

    m_Running.store(true);
    m_AcceptThread = std::thread(&TcpProxy::acceptLoop, this);
    std::cout << "[TcpProxy] listening on TCP port " << m_BindPort
              << " → " << m_TargetHost << ":" << m_TargetPort << "\n";
    return true;
}

void TcpProxy::stop() {
    if (!m_Running.exchange(false)) return;
    if (m_Listener != INVALID_SOCKET) {
        closesocket(m_Listener);
        m_Listener = INVALID_SOCKET;
    }
    if (m_AcceptThread.joinable())
        m_AcceptThread.join();
    std::cout << "[TcpProxy] stopped\n";
}

void TcpProxy::acceptLoop() {
    while (m_Running.load()) {
        sockaddr_in clientAddr{};
        socklen_t   clientLen = sizeof(clientAddr);
        SOCKET clientSock     = ::accept(m_Listener,
                                         reinterpret_cast<sockaddr *>(&clientAddr),
                                         &clientLen);
        if (clientSock == INVALID_SOCKET) break; // listener closed

        char clientIp[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET, &clientAddr.sin_addr, clientIp, sizeof(clientIp));
        std::cout << "[TcpProxy] client connected: " << clientIp
                  << ":" << ntohs(clientAddr.sin_port) << "\n";

        SOCKET serverSock = connectTo(m_TargetHost, m_TargetPort);
        if (serverSock == INVALID_SOCKET) {
            std::cerr << "[TcpProxy] could not connect to "
                      << m_TargetHost << ":" << m_TargetPort << "\n";
            closesocket(clientSock);
            continue;
        }

        proxyConnection(clientSock, serverSock);
        closesocket(clientSock);
        closesocket(serverSock);
        std::cout << "[TcpProxy] connection closed\n";
    }
}

void TcpProxy::proxyConnection(SOCKET client, SOCKET server) {
    // Relay both halves concurrently; wait for both to finish.
    std::thread t([&] { relayHalf(server, client, 1 /*server→client*/); });
    relayHalf(client, server, 0 /*client→server*/);
    // Close both sockets to unblock the other thread
    closesocket(client);
    closesocket(server);
    if (t.joinable()) t.join();
    // Reopen sockets would break, but this is fine — the
    // accept loop will create fresh sockets for the next connection.
}

void TcpProxy::relayHalf(SOCKET src, SOCKET dst, uint64_t direction) {
    constexpr int kBufSize = 65536;
    std::vector<std::byte> buf(kBufSize);

    while (m_Running.load()) {
        int n = static_cast<int>(
            ::recv(src, reinterpret_cast<char *>(buf.data()), kBufSize, 0));
        if (n <= 0) break; // connection closed or error

        // Forward to the other side
        int sent = static_cast<int>(
            ::send(dst, reinterpret_cast<const char *>(buf.data()), n, 0));
        if (sent <= 0) break;

        // Record into bus
        Frame f;
        f.Proto   = Proto::TCP;
        f.Tag     = direction;
        f.Ts_ns   = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        f.Payload.assign(buf.begin(), buf.begin() + n);
        m_Bus.Send(nullptr, std::move(f));
    }
}

/*static*/ SOCKET TcpProxy::connectTo(const std::string &host, uint16_t port) {
    addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    std::string portStr = std::to_string(port);

    if (::getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res) != 0 || !res)
        return INVALID_SOCKET;

    SOCKET s = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s == INVALID_SOCKET) { freeaddrinfo(res); return INVALID_SOCKET; }

    if (::connect(s, res->ai_addr, static_cast<int>(res->ai_addrlen)) != 0) {
        closesocket(s);
        freeaddrinfo(res);
        return INVALID_SOCKET;
    }
    freeaddrinfo(res);
    return s;
}

} // namespace vbus
