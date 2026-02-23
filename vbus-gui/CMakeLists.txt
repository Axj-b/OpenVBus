cmake_minimum_required(VERSION 3.20)
project(openvbus_imgui LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


include(FetchContent)


# GLFW
FetchContent_Declare(
glfw
GIT_REPOSITORY https://github.com/glfw/glfw.git
GIT_TAG 3.4
)
FetchContent_MakeAvailable(glfw)


# Dear ImGui
FetchContent_Declare(
imgui
GIT_REPOSITORY https://github.com/ocornut/imgui.git
GIT_TAG v1.92.4-docking
)
FetchContent_MakeAvailable(imgui)

# nlohmann JSON (header-only)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install    OFF CACHE INTERNAL "")
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)


# ImGui sources
set(IMGUI_SRC
${imgui_SOURCE_DIR}/imgui.cpp
${imgui_SOURCE_DIR}/imgui_demo.cpp
${imgui_SOURCE_DIR}/imgui_draw.cpp
${imgui_SOURCE_DIR}/imgui_tables.cpp
${imgui_SOURCE_DIR}/imgui_widgets.cpp
${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)


add_executable(openvbus_imgui
src/main.cpp
#src/glad.c
src/imgui_layer.cpp
src/model.cpp
src/project.cpp
src/daemon_client.cpp
src/panels/panel_bus_list.cpp
src/panels/panel_inspector.cpp
src/panels/panel_filters.cpp
src/panels/panel_packets.cpp
src/panels/panel_vlan.cpp
src/panels/panel_log.cpp
src/backends/iface_mock.cpp
src/backends/iface_pcap.cpp
${IMGUI_SRC}
glad/src/glad.c
)


target_include_directories(openvbus_imgui PRIVATE
${imgui_SOURCE_DIR}
${imgui_SOURCE_DIR}/backends
${glfw_SOURCE_DIR}/include
src
glad/include/
)


# Define for GL 3.0+ shader path
add_compile_definitions(IMGUI_IMPL_OPENGL_LOADER_GLAD)


find_package(OpenGL REQUIRED)


target_link_libraries(openvbus_imgui PRIVATE glfw OpenGL::GL nlohmann_json::nlohmann_json)


if (WIN32)
target_link_libraries(openvbus_imgui PRIVATE ws2_32 iphlpapi)
endif()


# PCAP optional
option(OPENVBUS_WITH_PCAP "Build PCAP interface" OFF)
if(OPENVBUS_WITH_PCAP)
find_path(PCAP_INCLUDE_DIR pcap/pcap.h)
find_library(PCAP_LIBRARY NAMES pcap wpcap)
if(PCAP_INCLUDE_DIR AND PCAP_LIBRARY)
target_compile_definitions(openvbus_imgui PRIVATE OPENVBUS_WITH_PCAP)
target_include_directories(openvbus_imgui PRIVATE ${PCAP_INCLUDE_DIR})
target_link_libraries(openvbus_imgui PRIVATE ${PCAP_LIBRARY})
endif()
endif()