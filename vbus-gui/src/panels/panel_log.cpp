#include "panel_log.h"
#include <imgui.h>

using namespace ovb;
using namespace ovb::ui;

void panels::Log(Context &ctx) {
    ImGui::Begin("Log");
    if (ImGui::Button("Clear"))
        ctx.s.log_lines.clear();
    ImGui::SameLine();
    ImGui::Text("%zu entries", ctx.s.log_lines.size());
    ImGui::Separator();
    ImGui::BeginChild("logscroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    for (const auto &line : ctx.s.log_lines)
        ImGui::TextUnformatted(line.c_str());
    // Auto-scroll to bottom
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::End();
}