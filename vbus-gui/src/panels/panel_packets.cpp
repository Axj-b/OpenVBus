#include "panel_packets.h"
#include <imgui.h>
#include <cstdio>

using namespace ovb;
using namespace ovb::ui;

void panels::Packets(Context &ctx) {
    ImGui::Begin("Packets");
    auto *bus = ctx.m.getBus(ctx.s.selected_bus);
    if (!bus) {
        ImGui::TextUnformatted("No bus selected");
        ImGui::End();
        return;
    }

    ImGui::Text("Ring: %zu packets", bus->ring.size());
    if (!bus->filters.empty())
        ImGui::TextDisabled("(%zu filter rule(s) active)", bus->filters.size());
    ImGui::Separator();

    ImGui::BeginChild("pktlist", ImVec2(0, 0), true);
    int idx = 0;
    for (const auto &p : bus->ring) {
        // Ring already contains only packets that passed Model::tick filters.
        char preview[33]{};
        for (int i = 0; i < 16; ++i)
            std::snprintf(preview + i * 2, 3, "%02x", p.preview[i]);
        ImGui::Text("%04d | %10.6f s | vlan=%-4u | %4u B | %s...",
            idx++,
            p.timestamp_ns / 1e9,
            p.vlan,
            p.size,
            preview);
    }
    ImGui::EndChild();
    ImGui::End();
}