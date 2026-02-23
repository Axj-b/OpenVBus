
#pragma once

#include "core/core.h"

#include <span>
#include <vector>
#include <string_view>

namespace vbus {

    enum class Proto : uint8_t {
        ETH2  = 1,
        CAN20 = 2,
        CANFD = 3,
        UDP   = 4,  // Tag = (src_ip<<32)|(src_port<<16)|dst_port
        TCP   = 5   // Tag = direction: 0=client→server, 1=server→client
    };

    struct FrameView {
        std::span<const std::byte> Bytes;
    };

    struct Frame {
        Proto                  Proto{};
        uint64_t               Tag{}; // ETH: reserved; CAN: identifier
        uint64_t               Ts_ns{}; // delivery ts for recorder
        std::vector<std::byte> Payload;
    };

    inline std::vector<std::byte> hex_to_bytes(std::string_view hex) {
        auto h2n = [](char c) -> int {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return 10 + (c - 'a');
            if (c >= 'A' && c <= 'F')
                return 10 + (c - 'A');
            return 0;
        };
        std::vector<std::byte> out;
        if (hex.size() % 2)
            return out;
        out.reserve(hex.size() / 2);
        for (size_t i = 0; i < hex.size(); i += 2) {
            int v = (h2n(hex[i]) << 4) | h2n(hex[i + 1]);
            out.push_back(static_cast<std::byte>(v & 0xFF));
        }
        return out;
    }

} // namespace vbus
