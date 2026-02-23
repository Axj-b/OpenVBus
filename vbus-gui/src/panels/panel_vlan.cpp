#include "panel_vlan.h"
#include <imgui.h>
#include <unordered_map>

using namespace ovb;
using namespace ovb::ui;

void panels::VLAN(Context &ctx) {
    ImGui::Begin("VLAN Manager");
    auto *bus = ctx.m.getBus(ctx.s.selected_bus);
    if (!bus) {
        ImGui::TextUnformatted("No bus selected");
        ImGui::End();
        return;
    }

    // Count packets per VLAN from the ring.
    std::unordered_map<uint32_t, size_t> counts;
    for (const auto &p : bus->ring)
        counts[p.vlan]++;

    ImGui::Text("VLANs active: %zu", counts.size());
    ImGui::Separator();
    ImGui::BeginChild("vlanlist", ImVec2(0, 0), true);
    // Sort by VLAN id for stable display.
    std::vector<std::pair<uint32_t, size_t>> rows(counts.begin(), counts.end());
    std::sort(rows.begin(), rows.end());
    for (auto &[vid, cnt] : rows) {
        if (vid == 0)
            ImGui::Text("untagged   : %zu pkts", cnt);
        else
            ImGui::Text("VLAN %-6u : %zu pkts", vid, cnt);
    }
    ImGui::EndChild();
    ImGui::End();
}