#include "panel_filters.h"
#include <imgui.h>
#include <cstring>

using namespace ovb;
using namespace ovb::ui;

void panels::Filters(Context &ctx) {
    ImGui::Begin("Filters");
    auto *bus = ctx.m.getBus(ctx.s.selected_bus);
    if (!bus) {
        ImGui::TextUnformatted("No bus selected");
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("Glob patterns: * matches anything, ? matches one char");
    ImGui::TextDisabled("Token examples: vlan:100  size:1400");
    ImGui::Separator();

    if (ImGui::Button("+ Include"))
        bus->filters.push_back({ovb::FilterRule::Type::Include, "vlan:*"});
    ImGui::SameLine();
    if (ImGui::Button("+ Exclude"))
        bus->filters.push_back({ovb::FilterRule::Type::Exclude, "size:*"});

    int del_idx = -1;
    for (int i = 0; i < (int)bus->filters.size(); ++i) {
        auto &f = bus->filters[i];
        ImGui::PushID(i);

        int t = (f.type == ovb::FilterRule::Type::Include) ? 0 : 1;
        if (ImGui::Combo("Type", &t, "Include\0Exclude\0"))
            f.type = t ? ovb::FilterRule::Type::Exclude : ovb::FilterRule::Type::Include;

        // Safe InputText with fixed-size buffer
        char exprBuf[128]{};
        std::strncpy(exprBuf, f.expr.c_str(), sizeof(exprBuf) - 1);
        if (ImGui::InputText("Expr", exprBuf, sizeof(exprBuf)))
            f.expr = exprBuf;

        if (ImGui::SmallButton("Delete"))
            del_idx = i;

        ImGui::Separator();
        ImGui::PopID();
    }
    if (del_idx >= 0)
        bus->filters.erase(bus->filters.begin() + del_idx);

    ImGui::End();
}