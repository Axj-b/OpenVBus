#include "panel_inspector.h"
#include <imgui.h>
#include <cstring>

using namespace ovb;
using namespace ovb::ui;

static const char *protoLabel(uint8_t p) {
    switch (p) {
    case 1: return "ETH";  case 2: return "CAN";
    case 3: return "CANFD"; case 4: return "UDP"; case 5: return "TCP";
    default: return "?";
    }
}

void panels::Inspector(Context &ctx) {
    ImGui::Begin("Inspector");

    // Daemon status banner
    if (ctx.s.daemon_connected)
        ImGui::TextColored({0.3f, 1.f, 0.3f, 1.f}, "● vbusd connected");
    else
        ImGui::TextColored({1.f, 0.4f, 0.4f, 1.f}, "● vbusd offline (mock mode)");
    ImGui::Separator();

    auto *bus = ctx.m.getBus(ctx.s.selected_bus);
    if (!bus) {
        ImGui::TextUnformatted("No bus selected");
        ImGui::End();
        return;
    }

    // Bus name
    char nameBuf[128]{};
    std::strncpy(nameBuf, bus->name.c_str(), sizeof(nameBuf) - 1);
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
        bus->name = nameBuf;

    ImGui::Checkbox("Enabled", &bus->enabled);
    ImGui::Text("Packets in ring: %zu", bus->ring.size());

    // ── Capture ─────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Capture", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (!bus->iface) {
            // Driver selector
            static int s_sel = 0; // 0=UDP, 1=TCP, 2=Mock
            ImGui::Combo("Type", &s_sel, "UDP\0TCP proxy\0Mock\0");

            if (s_sel == 0) {  // UDP
                ImGui::InputScalar("Bind port", ImGuiDataType_U16, &bus->bind_port);
                if (ImGui::Button("Attach UDP")) {
                    InterfaceDesc d{"UDP :" + std::to_string(bus->bind_port), "udp"};
                    ctx.m.attachIface(*bus, d);
                }
            } else if (s_sel == 1) { // TCP proxy
                ImGui::InputScalar("Bind port##tcp",  ImGuiDataType_U16, &bus->bind_port);
                ImGui::InputText("Target host", bus->target_host, sizeof(bus->target_host));
                ImGui::InputScalar("Target port", ImGuiDataType_U16, &bus->target_port);
                if (ImGui::Button("Attach TCP proxy")) {
                    InterfaceDesc d{
                        "TCP :" + std::to_string(bus->bind_port) +
                        " → " + std::string(bus->target_host) + ":" + std::to_string(bus->target_port),
                        "tcp"};
                    ctx.m.attachIface(*bus, d);
                }
            } else { // Mock
                if (ImGui::Button("Attach mock"))
                    ctx.m.attachIface(*bus, {"Mock", "mock"});
            }
        } else {
            ImGui::Text("Active: %s (%s)",
                        bus->iface->name.c_str(), bus->iface->driver.c_str());
            if (ImGui::Button("Detach"))
                ctx.m.detachIface(*bus);
        }
    }

    // ── Recording ────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Recording", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::InputText("Output file##rec", bus->record_path, sizeof(bus->record_path));
        if (!bus->recording) {
            if (ImGui::Button("Start recording"))
                ctx.m.startRecord(*bus);
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, {0.8f, 0.2f, 0.2f, 1.f});
            if (ImGui::Button("Stop recording"))
                ctx.m.stopRecord(*bus);
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextColored({1.f, 0.4f, 0.4f, 1.f}, "● REC");
        }
    }

    // ── Replay ───────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Replay")) {
        static int s_mode = 0; // 0=exact, 1=burst, 2=scale
        static float s_scale = 1.0f;

        ImGui::InputText("Capture file##replay", bus->replay_path, sizeof(bus->replay_path));
        ImGui::Combo("Mode##replay", &s_mode, "Exact timing\0Burst (no delay)\0Scaled timing\0");
        if (s_mode == 2)
            ImGui::InputFloat("Scale##replay", &s_scale, 0.1f, 1.f, "%.2f");

        if (ImGui::Button("Start Replay")) {
            std::string mode;
            if (s_mode == 0)       mode = "exact";
            else if (s_mode == 1)  mode = "burst";
            else                   mode = "scale:" + std::to_string(s_scale);
            ctx.m.replayFile(*bus, mode);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(injects frames back into the bus)");
    }

    ImGui::End();
}