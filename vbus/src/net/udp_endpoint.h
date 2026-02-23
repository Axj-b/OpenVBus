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

// Binds a UDP socket to 0.0.0.0:<port> and feeds every received datagram
// into the bus as a Frame{Proto::UDP}.
//
// Frame.Tag encoding (64-bit):
//   bits 63-32  source IPv4 address  (network byte order)
//   bits 31-16  source port          (host byte order)
//   bits 15-0   bound (destination) port (host byte order)
//
// Frames flow: recvfrom() → bus->Send(nullptr, frame)
// Any Recorder tap attached to the bus will capture them automatically.
class UdpEndpoint : public ICaptureEndpoint {
public:
    UdpEndpoint(IBus &bus, uint16_t bindPort);
    ~UdpEndpoint() override { stop(); }

    bool start() override;
    void stop()  override;

private:
    IBus     &m_Bus;
    uint16_t  m_Port;
    SOCKET    m_Sock{INVALID_SOCKET};
    std::atomic<bool> m_Running{false};
    std::thread       m_Thread;

    void recvLoop();
};

} // namespace vbus
