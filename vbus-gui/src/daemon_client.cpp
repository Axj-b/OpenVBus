#include "daemon_client.h"
#include <cstring>
#include <iostream>

namespace ovb {

/*static*/ HANDLE DaemonClient::openPipe() {
    for (int attempt = 0; attempt < 5; ++attempt) {
        HANDLE h = CreateFileW(
            kPipeName,
            GENERIC_READ | GENERIC_WRITE,
            0, nullptr,
            OPEN_EXISTING,
            0,           // synchronous I/O
            nullptr);

        if (h != INVALID_HANDLE_VALUE)
            return h;

        DWORD err = GetLastError();
        if (err == ERROR_PIPE_BUSY) {
            WaitNamedPipeW(kPipeName, 250);
            continue;
        }
        if (err == ERROR_FILE_NOT_FOUND) {
            // Daemon not running yet or pipe instance not ready – brief wait
            Sleep(50);
            continue;
        }
        break;
    }
    return INVALID_HANDLE_VALUE;
}

bool DaemonClient::sendCmd(const std::string &cmd, std::string &resp) const {
    HANDLE h = openPipe();
    if (h == INVALID_HANDLE_VALUE) return false;

    // Switch to message-read mode so the response arrives as one discrete message.
    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(h, &mode, nullptr, nullptr);

    std::string line = cmd + "\n";
    DWORD written = 0;
    if (!WriteFile(h, line.data(), static_cast<DWORD>(line.size()), &written, nullptr)) {
        CloseHandle(h);
        return false;
    }

    char buf[4096]{};
    DWORD nread = 0;
    ReadFile(h, buf, sizeof(buf) - 1, &nread, nullptr);
    resp.assign(buf, nread);
    // Trim trailing newline
    while (!resp.empty() && (resp.back() == '\n' || resp.back() == '\r'))
        resp.pop_back();

    CloseHandle(h);
    return true;
}

bool DaemonClient::subscribe(const std::string &busName,
                             std::function<void(const RawFrame &)> cb) {
    unsubscribe(); // stop any previous subscription

    HANDLE h = openPipe();
    if (h == INVALID_HANDLE_VALUE) return false;

    // Enable message-read mode for the subscription pipe too.
    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(h, &mode, nullptr, nullptr);

    // Send the subscribe command.
    std::string cmd = "subscribe " + busName + "\n";
    DWORD written = 0;
    WriteFile(h, cmd.data(), static_cast<DWORD>(cmd.size()), &written, nullptr);

    // Read the acknowledgement ("OK stream\n")
    char ack[64]{};
    DWORD nread = 0;
    if (!ReadFile(h, ack, sizeof(ack) - 1, &nread, nullptr) ||
        std::strncmp(ack, "OK", 2) != 0) {
        CloseHandle(h);
        return false;
    }

    m_SubPipe   = h;
    m_SubRunning.store(true);

    m_SubThread = std::thread([this, cb = std::move(cb)]() {
        // Frames arrive as one message per frame: WireHdr(24) + payload
        std::vector<uint8_t> buf(65536 + sizeof(WireHdr));
        while (m_SubRunning.load()) {
            DWORD nread = 0;
            BOOL  ok    = ReadFile(m_SubPipe, buf.data(),
                                   static_cast<DWORD>(buf.size()), &nread, nullptr);
            if (!ok) {
                DWORD err = GetLastError();
                if (err == ERROR_MORE_DATA) continue; // partial – shouldn't happen
                break; // pipe closed
            }
            if (nread < static_cast<DWORD>(sizeof(WireHdr))) continue;

            WireHdr hdr{};
            std::memcpy(&hdr, buf.data(), sizeof(WireHdr));
            if (nread < sizeof(WireHdr) + hdr.len) continue; // malformed

            RawFrame frame;
            frame.proto  = hdr.proto;
            frame.tag    = hdr.tag;
            frame.ts_ns  = hdr.ts_ns;
            frame.payload.assign(buf.data() + sizeof(WireHdr),
                                 buf.data() + sizeof(WireHdr) + hdr.len);
            if (cb) cb(frame);
        }
    });
    return true;
}

void DaemonClient::unsubscribe() {
    if (!m_SubRunning.exchange(false)) return;
    if (m_SubPipe != INVALID_HANDLE_VALUE) {
        CancelIoEx(m_SubPipe, nullptr); // unblock ReadFile
        CloseHandle(m_SubPipe);
        m_SubPipe = INVALID_HANDLE_VALUE;
    }
    if (m_SubThread.joinable())
        m_SubThread.join();
}

bool DaemonClient::isConnected() const {
    std::string resp;
    return sendCmd("list", resp);
}

} // namespace ovb
