#pragma once

#include "../bus/ibus.h"
#include "../bus/eth_bus.h"
#include "../bus/can_bus.h"

#include <string>
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

// UdpSink registers a forward callback (m_Fwd_cb) on the bus that fires
// IMMEDIATELY inside Bus::Send(), on the sender's thread, BEFORE the
// scheduler delay.  This guarantees zero added latency and no packet
// bursting — essential for transparent UDP video forwarding.
//
// Compare with connecting as an IEndpoint, which would route through
// the scheduler and introduce up to 1 ms of buffering + burst delivery.
class UdpSink {
public:
    UdpSink(IBus &bus, std::string dstHost, uint16_t dstPort);
    ~UdpSink() { stop(); }

    bool start();
    void stop();

private:
    IBus         &m_Bus;
    std::string   m_DstHost;
    uint16_t      m_DstPort;
    SOCKET        m_Sock{INVALID_SOCKET};
    sockaddr_in   m_Dst{};
    bool          m_Active{false};

    void forward(const Frame &f);
};

} // namespace vbus
