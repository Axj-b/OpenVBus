#include "imgui_layer.h"
#include "panels/panel_bus_list.h"
#include "panels/panel_inspector.h"
#include "panels/panel_filters.h"
#include "panels/panel_packets.h"
#include "panels/panel_vlan.h"
#include "panels/panel_log.h"
#include <imgui.h>

using namespace ovb;
using namespace ovb::ui;

void ovb::ui::DrawDockspace(Context &ctx) {
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

    ImGui::End();
}