#pragma once

#include "capture_base.h"
#include "../bus/ibus.h"

#include <string>
#include <thread>
#include <atomic>
#include <cstdint>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <unistd.h>
using SOCKET = int;
static constexpr SOCKET INVALID_SOCKET = -1;
#endif

namespace vbus {

// Transparent TCP proxy. Listens on 0.0.0.0:<bindPort>.
// When a client connects, simultaneously opens a connection to
// <targetHost>:<targetPort> and bidirectionally relays data.
//
// Every chunk of data read in either direction is recorded into the bus
// as a Frame{Proto::TCP}:
//   Tag = 0  → data flowing from client to server
//   Tag = 1  → data flowing from server to client
//
// Handles one active proxied connection at a time (sufficient for recording
// a single TCP stream). A new connection is accepted after the previous
// one closes.
class TcpProxy : public ICaptureEndpoint {
public:
    TcpProxy(IBus &bus, uint16_t bindPort,
             std::string targetHost, uint16_t targetPort);
    ~TcpProxy() override { stop(); }

    bool start() override;
    void stop()  override;

private:
    IBus        &m_Bus;
    uint16_t     m_BindPort;
    std::string  m_TargetHost;
    uint16_t     m_TargetPort;

    SOCKET m_Listener{INVALID_SOCKET};
    std::atomic<bool> m_Running{false};
    std::thread       m_AcceptThread;

    void acceptLoop();
    void proxyConnection(SOCKET client, SOCKET server);
    void relayHalf(SOCKET src, SOCKET dst, uint64_t direction);

    static SOCKET connectTo(const std::string &host, uint16_t port);
};

} // namespace vbus
