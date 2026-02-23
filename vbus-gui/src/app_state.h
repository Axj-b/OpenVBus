#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include <deque>
#include <array>
#include "util/id_gen.h"

namespace ovb {

    struct Packet {
        uint64_t                timestamp_ns{};
        uint32_t                vlan{}; // 0 if none
        uint16_t                size{};
        uint8_t                 proto{};  // vbus::Proto cast: 1=ETH2 2=CAN20 3=CANFD 4=UDP 5=TCP
        std::array<uint8_t, 64> preview{}; // first bytes
    };

    struct FilterRule {
        enum class Type { Include,
            Exclude } type{Type::Include};
        std::string expr; // simple glob or BPF-like stub
    };

    struct InterfaceDesc {
        std::string name; // user friendly
        std::string driver; // mock, pcap, custom
    };

    struct Bus {
        uint32_t                     id{};
        std::string                  name;
        std::optional<InterfaceDesc> iface; // attached physical
        std::vector<FilterRule>      filters;
        std::deque<Packet>           ring; // recent packets
        bool                         enabled{true};

        // Capture configuration (filled in Inspector before clicking Attach)
        char     bind_host[64]{"0.0.0.0"};   // local IP to bind (UDP/TCP listen)
        uint16_t bind_port{9000};
        char     target_host[64]{"127.0.0.1"};
        uint16_t target_port{0};

        // Recording state
        bool recording{false};
        char record_path[256]{};
        char replay_path[256]{};

        // UDP forward/output state
        char     forward_host[64]{"127.0.0.1"};
        uint16_t forward_port{9000};
        bool     forwarding{false};
    };

    struct AppState {
        std::vector<Bus>         buses;
        uint32_t                 selected_bus{};
        bool                     show_demo{false};
        bool                     request_exit{false};
        bool                     daemon_connected{false};
        std::vector<std::string> log_lines;

        // Global record-all state
        char record_all_prefix[256]{"capture"}; // files: {prefix}_{busname}.vbuscap
        bool global_recording{false};

        // Global replay-all state
        char  replay_all_mode[16]{"exact"};  // exact | burst | scale
        float replay_all_scale{1.0f};        // used when mode == "scale"
        bool  global_replaying{false};

        // Project persistence
        char project_path[512]{};   // current .ovbproj path (empty = unsaved)
        bool needs_daemon_sync{false}; // set after load; model syncs on daemon connect
    };

} // namespace ovb