#include "imgui_layer.h"
#include "project.h"
#include "panels/panel_bus_list.h"
#include "panels/panel_inspector.h"
#include "panels/panel_filters.h"
#include "panels/panel_packets.h"
#include "panels/panel_vlan.h"
#include "panels/panel_log.h"
#include <imgui.h>
#include <cstring>

using namespace ovb;
using namespace ovb::ui;

void ovb::ui::DrawDockspace(Context &ctx) {
    // ── Keyboard shortcuts ────────────────────────────────────────────────────
    static char s_open_path[512]{};
    static char s_save_path[512]{};
    static bool s_do_open = false;
    static bool s_do_save = false;

    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S)) {
        if (ctx.s.project_path[0])
            ctx.m.saveProject(ctx.s.project_path);
        else {
            std::strncpy(s_save_path, "project.ovbproj", sizeof(s_save_path) - 1);
            s_do_save = true;
        }
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O)) {
        std::strncpy(s_open_path, ctx.s.project_path, sizeof(s_open_path) - 1);
        s_do_open = true;
    }

    ImGuiWindowFlags flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking |
                             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    const ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("DockSpace", nullptr, flags);
    ImGui::PopStyleVar(2);

    ImGuiID dockspace_id = ImGui::GetID("MyDockspace");
    ImGui::DockSpace(dockspace_id, ImVec2(0, 0));

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Project")) {
                ctx.m.newProject();
            }
            if (ImGui::MenuItem("Open Project...", "Ctrl+O")) {
                std::strncpy(s_open_path, ctx.s.project_path, sizeof(s_open_path) - 1);
                s_do_open = true;
            }
            ImGui::Separator();
            bool hasSavePath = ctx.s.project_path[0] != '\0';
            if (ImGui::MenuItem("Save Project", "Ctrl+S", false, hasSavePath))
                ctx.m.saveProject(ctx.s.project_path);
            if (ImGui::MenuItem("Save Project As...")) {
                std::strncpy(s_save_path,
                             hasSavePath ? ctx.s.project_path : "project.ovbproj",
                             sizeof(s_save_path) - 1);
                s_do_save = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("New Bus"))
                ctx.m.newBus("Bus" + std::to_string(ctx.s.buses.size() + 1));
            if (ImGui::MenuItem("Exit"))
                ctx.s.request_exit = true;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("ImGui Demo", nullptr, &ctx.s.show_demo);
            ImGui::EndMenu();
        }
        // Show project name in menu bar (right-aligned is tricky; show as dim text)
        if (ctx.s.project_path[0]) {
            // Extract filename only
            std::string pp(ctx.s.project_path);
            auto slash = pp.find_last_of("/\\");
            std::string fname = (slash != std::string::npos) ? pp.substr(slash + 1) : pp;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8);
            ImGui::TextDisabled("%s", fname.c_str());
        }
        ImGui::EndMenuBar();
    }

    panels::BusList(ctx);
    panels::Inspector(ctx);
    panels::Filters(ctx);
    panels::Packets(ctx);
    panels::VLAN(ctx);
    panels::Log(ctx);

    if (ctx.s.show_demo)
        ImGui::ShowDemoWindow(&ctx.s.show_demo);

    // ── Open-project modal ────────────────────────────────────────────────────
    if (s_do_open) { ImGui::OpenPopup("Open Project"); s_do_open = false; }
    ImGui::SetNextWindowSize({500, 0}, ImGuiCond_Always);
    if (ImGui::BeginPopupModal("Open Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Project file (.ovbproj):");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##op", s_open_path, sizeof(s_open_path));
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere(-1);
        ImGui::Spacing();
        if (ImGui::Button("Open", {120, 0})) {
            if (ctx.m.loadProject(s_open_path))
                ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {120, 0}))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // ── Save-as modal ─────────────────────────────────────────────────────────
    if (s_do_save) { ImGui::OpenPopup("Save Project As"); s_do_save = false; }
    ImGui::SetNextWindowSize({500, 0}, ImGuiCond_Always);
    if (ImGui::BeginPopupModal("Save Project As", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Save to (.ovbproj):");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##sp", s_save_path, sizeof(s_save_path));
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere(-1);
        ImGui::Spacing();
        if (ImGui::Button("Save", {120, 0})) {
            ctx.m.saveProject(s_save_path);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {120, 0}))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::End();
}