#pragma once
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <vector>
#include <cstdint>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

namespace ovb {

// Frame received from the daemon subscription stream.
struct RawFrame {
    uint8_t              proto;   // vbus::Proto cast to uint8
    uint64_t             tag;     // CAN id / UDP src-ip:port encoding / TCP direction
    uint64_t             ts_ns;   // capture timestamp
    std::vector<uint8_t> payload;
};

// Thin client for the vbusd Named Pipe protocol.
//
// Thread-safety:
//   sendCmd()      – call from any single thread (not re-entrant)
//   subscribe()    – sets up a background reader thread
//   unsubscribe()  – safe to call from any thread; blocks until reader exits
class DaemonClient {
public:
    static constexpr const wchar_t *kPipeName = L"\\\\.\\pipe\\vbusd";

    // Send one command line to the daemon and return the response.
    // Returns false if the pipe cannot be opened (daemon not running).
    bool sendCmd(const std::string &cmd, std::string &resp) const;

    // Open a persistent subscription for busName.
    // cb is called from a background thread for every frame the daemon forwards.
    bool subscribe(const std::string &busName,
                   std::function<void(const RawFrame &)> cb);

    // Stop the active subscription (idempotent).
    void unsubscribe();

    // Quick ping – returns true if the daemon is reachable.
    bool isConnected() const;

    ~DaemonClient() { unsubscribe(); }

private:
    // 24-byte on-wire header matching vbus::VCapRec
#pragma pack(push, 1)
    struct WireHdr {
        uint8_t  proto;
        uint8_t  flags;
        uint16_t reserved;
        uint64_t tag;
        uint64_t ts_ns;
        uint32_t len;
    };
    static_assert(sizeof(WireHdr) == 24, "VCapRec layout mismatch");
#pragma pack(pop)

    static HANDLE openPipe();

    HANDLE            m_SubPipe{INVALID_HANDLE_VALUE};
    std::thread       m_SubThread;
    std::atomic<bool> m_SubRunning{false};
};

} // namespace ovb
