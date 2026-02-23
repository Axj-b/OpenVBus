#pragma once

namespace vbus {

// Base interface for all live-capture endpoints (UDP listener, TCP proxy, etc.)
// Owned by BusWrap; lifetime tied to the bus.
struct ICaptureEndpoint {
    virtual ~ICaptureEndpoint() = default;
    virtual bool start() = 0;
    virtual void stop()  = 0;
};

} // namespace vbus
