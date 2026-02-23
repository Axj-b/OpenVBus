#include "panel_bus_list.h"
#include <imgui.h>
#include <string>

using namespace ovb;
using namespace ovb::ui;

void panels::BusList(Context &ctx) {
    ImGui::Begin("Buses");
    if (ImGui::Button("+ New Bus"))
        ctx.m.newBus("Bus" + std::to_string(ctx.s.buses.size() + 1));
    ImGui::Separator();

    // Collect IDs to delete (can't erase while iterating)
    uint32_t delete_id = 0;
    for (auto &b : ctx.s.buses) {
        bool selected = (ctx.s.selected_bus == b.id);
        if (ImGui::Selectable((b.name + "##sel" + std::to_string(b.id)).c_str(), selected,
                ImGuiSelectableFlags_None, ImVec2(ImGui::GetContentRegionAvail().x - 28, 0)))
            ctx.s.selected_bus = b.id;
        ImGui::SameLine();
        ImGui::PushID((int)b.id);
        if (ImGui::SmallButton("X"))
            delete_id = b.id;
        ImGui::PopID();
    }
    if (delete_id)
        ctx.m.deleteBus(delete_id);

    ImGui::End();
}