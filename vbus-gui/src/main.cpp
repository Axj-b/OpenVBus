#include <cstdio>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include "imgui_layer.h"
#include "project.h"

using namespace ovb;

static void glfw_error_callback(int error, const char *description) {
    std::fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

int main() {
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return 1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow *window = glfwCreateWindow(1280, 800, "OpenVBus", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
        return 2;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 460 core");

    AppState state{};
    Model    model(state);

    // ── Auto-load last project ────────────────────────────────────────────────
    {
        auto recent = ovb::ProjectIO::readRecent();
        if (!recent.empty() && model.loadProject(recent)) {
            // project loaded, buses restored
        } else {
            model.newBus("Bus1"); // default first-run bus
        }
    }

    ovb::ui::Context ui(state, model);

    double last = glfwGetTime();

    while (!glfwWindowShouldClose(window) && !state.request_exit) {
        glfwPollEvents();
        double now = glfwGetTime();
        model.tick(now - last);
        last = now;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ovb::ui::DrawDockspace(ui);

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    // ── Auto-save project on exit ─────────────────────────────────────────────
    if (state.project_path[0])
        model.saveProject(state.project_path);

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}