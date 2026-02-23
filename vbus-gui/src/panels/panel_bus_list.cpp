#include "panel_bus_list.h"
#include <imgui.h>
#include <string>

using namespace ovb;
using namespace ovb::ui;

void panels::BusList(Context &ctx) {
    ImGui::Begin("Buses");

    // ── Global record-all toolbar ────────────────────────────────────────────
    bool anyBus = !ctx.s.buses.empty();
    // Keep global_recording in sync if someone stopped individual buses
    if (ctx.s.global_recording) {
        bool stillAny = false;
        for (auto &b : ctx.s.buses) if (b.recording) { stillAny = true; break; }
        ctx.s.global_recording = stillAny;
    }

    if (!ctx.s.global_recording) {
        ImGui::BeginDisabled(!anyBus || !ctx.s.daemon_connected);
        if (ImGui::Button("[REC] ALL")) ctx.m.startRecordAll();
        ImGui::EndDisabled();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, {0.8f, 0.1f, 0.1f, 1.f});
        if (ImGui::Button("[STOP] ALL")) ctx.m.stopRecordAll();
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextColored({1.f, 0.35f, 0.35f, 1.f}, "* REC");
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##prefix", ctx.s.record_all_prefix,
                     sizeof(ctx.s.record_all_prefix),
                     ImGuiInputTextFlags_None);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Output prefix — each bus writes: {prefix}_{busname}.vbuscap");

    // ── Global replay-all toolbar ────────────────────────────────────────────
    {
        // Mode combo
        const char *modes[]    = {"exact", "burst", "scale"};
        int         curMode    = 0;
        for (int i = 0; i < 3; ++i)
            if (std::string(ctx.s.replay_all_mode) == modes[i]) { curMode = i; break; }
        ImGui::SetNextItemWidth(70);
        if (ImGui::Combo("##rmode", &curMode, modes, 3))
            std::snprintf(ctx.s.replay_all_mode, sizeof(ctx.s.replay_all_mode), "%s", modes[curMode]);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Replay timing: exact (original pacing) | burst (no delay) | scale (stretch/compress)");
        ImGui::SameLine();
        if (std::string(ctx.s.replay_all_mode) == "scale") {
            ImGui::SetNextItemWidth(55);
            ImGui::InputFloat("##rscale", &ctx.s.replay_all_scale, 0.0f, 0.0f, "%.2f");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Scale factor: 2.0 = 2x slower, 0.5 = 2x faster");
            ImGui::SameLine();
        }
        // Count buses that have both a source file and a forward destination
        // Uses same logic as startReplayAll: prefer replay_path, fall back to record_path
        int readyStreams = 0;
        for (auto &b : ctx.s.buses) {
            const char *srcFile = (b.replay_path[0] != '\0') ? b.replay_path : b.record_path;
            if (srcFile[0] != '\0' && b.forward_host[0] != '\0' && b.forward_port != 0)
                ++readyStreams;
        }
        ImGui::BeginDisabled(!anyBus || !ctx.s.daemon_connected || readyStreams == 0);
        if (ImGui::Button("[REPLAY] ALL"))
            ctx.m.startReplayAll();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) {
            if (readyStreams == 0)
                ImGui::SetTooltip("Per bus: set 'Replay file' (or Record file) + 'Forward host:port' in Inspector");
            else
                ImGui::SetTooltip("Sends replay-sync to daemon\nFile source: replay_path or record_path\nDest: forward host:port per bus");
        }
        if (readyStreams > 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("(%d stream%s)", readyStreams, readyStreams == 1 ? "" : "s");
        }
        if (ctx.s.global_replaying) {
            ImGui::SameLine();
            ImGui::TextColored({0.4f, 1.f, 0.4f, 1.f}, "-> PLAYING");
            ImGui::SameLine();
            if (ImGui::SmallButton("done"))
                ctx.s.global_replaying = false;
        }
    }

    ImGui::Separator();

    if (ImGui::Button("+ New Bus"))
        ctx.m.newBus("Bus" + std::to_string(ctx.s.buses.size() + 1));
    ImGui::Separator();

    // Collect IDs to delete (can't erase while iterating)
    uint32_t delete_id = 0;
    for (auto &b : ctx.s.buses) {
        bool selected = (ctx.s.selected_bus == b.id);
        // Show small REC badge next to recording buses
        std::string label = b.name;
        if (b.recording) label += "  *";
        if (ImGui::Selectable((label + "##sel" + std::to_string(b.id)).c_str(), selected,
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