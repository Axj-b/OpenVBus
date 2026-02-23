#include "project.h"
#include "util/id_gen.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdlib>
#include <cstdio>
#include <filesystem>

using nlohmann::json;
namespace fs = std::filesystem;

namespace ovb {

bool ProjectIO::save(const std::string &path, const AppState &state) {
    try {
        json j;
        j["version"]           = 1;
        j["record_all_prefix"] = state.record_all_prefix;
        j["replay_all_mode"]   = state.replay_all_mode;
        j["replay_all_scale"]  = state.replay_all_scale;

        json buses = json::array();
        for (const auto &b : state.buses) {
            json jb;
            jb["name"]         = b.name;
            jb["bind_host"]    = b.bind_host;
            jb["bind_port"]    = b.bind_port;
            jb["target_host"]  = b.target_host;
            jb["target_port"]  = b.target_port;
            jb["record_path"]  = b.record_path;
            jb["replay_path"]  = b.replay_path;
            jb["forward_host"] = b.forward_host;
            jb["forward_port"] = b.forward_port;
            buses.push_back(jb);
        }
        j["buses"] = buses;

        std::ofstream f(path);
        if (!f) return false;
        f << j.dump(2) << '\n';
        return true;
    } catch (...) {
        return false;
    }
}

bool ProjectIO::load(const std::string &path, AppState &state) {
    try {
        std::ifstream f(path);
        if (!f) return false;
        auto j = json::parse(f);

        // ── AppState-level settings ──────────────────────────────────────────
        if (j.contains("record_all_prefix"))
            std::snprintf(state.record_all_prefix, sizeof(state.record_all_prefix),
                          "%s", j["record_all_prefix"].get<std::string>().c_str());
        if (j.contains("replay_all_mode"))
            std::snprintf(state.replay_all_mode, sizeof(state.replay_all_mode),
                          "%s", j["replay_all_mode"].get<std::string>().c_str());
        if (j.contains("replay_all_scale"))
            state.replay_all_scale = j["replay_all_scale"].get<float>();

        // ── Buses ────────────────────────────────────────────────────────────
        state.buses.clear();
        state.selected_bus = 0;

        auto cp = [](char *dst, size_t dsz, const std::string &src) {
            std::snprintf(dst, dsz, "%s", src.c_str());
        };

        for (const auto &jb : j.value("buses", json::array())) {
            Bus b;
            b.id   = ovb::next_id();
            b.name = jb.value("name", "Bus");

            cp(b.bind_host,   sizeof(b.bind_host),   jb.value("bind_host",   "0.0.0.0"));
            b.bind_port      = jb.value("bind_port",   static_cast<uint16_t>(9000));
            cp(b.target_host, sizeof(b.target_host),  jb.value("target_host", "127.0.0.1"));
            b.target_port    = jb.value("target_port", static_cast<uint16_t>(0));
            cp(b.record_path, sizeof(b.record_path),  jb.value("record_path", ""));
            cp(b.replay_path, sizeof(b.replay_path),  jb.value("replay_path", ""));
            cp(b.forward_host,sizeof(b.forward_host), jb.value("forward_host","127.0.0.1"));
            b.forward_port   = jb.value("forward_port",static_cast<uint16_t>(9000));

            state.buses.push_back(b);
        }

        if (!state.buses.empty())
            state.selected_bus = state.buses.front().id;

        // Signal model that daemon-side buses need to be (re)created.
        state.needs_daemon_sync = true;
        return true;
    } catch (...) {
        return false;
    }
}

std::string ProjectIO::recentPath() {
    const char *appdata = std::getenv("APPDATA");
    if (appdata) {
        auto dir = std::string(appdata) + "\\OpenVBus";
        std::error_code ec;
        fs::create_directories(dir, ec);
        return dir + "\\last_project.txt";
    }
    return "openvbus_last_project.txt";
}

std::string ProjectIO::readRecent() {
    std::ifstream f(recentPath());
    if (!f) return {};
    std::string line;
    std::getline(f, line);
    // trim trailing whitespace / CR
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
        line.pop_back();
    return line;
}

void ProjectIO::writeRecent(const std::string &projectPath) {
    std::ofstream f(recentPath());
    if (f) f << projectPath << '\n';
}

} // namespace ovb
