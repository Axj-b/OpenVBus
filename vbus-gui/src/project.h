#pragma once
#include "app_state.h"
#include <string>

namespace ovb {

// Serialises / deserialises the full GUI project to a JSON .ovbproj file.
// Runtime-only state (recording, forwarding, selected bus, log lines) is
// intentionally omitted – those are reset on every launch.
struct ProjectIO {
    // Writes a JSON project file.  Returns false on I/O failure.
    static bool save(const std::string &path, const AppState &state);

    // Reads a JSON project file into state.buses / global fields.
    // Does NOT create buses on the daemon – the caller must call
    // Model::syncBusesToDaemon() (or it will auto-fire when the daemon connects).
    // Returns false if the file cannot be opened / parsed.
    static bool load(const std::string &path, AppState &state);

    // Path to the small one-line sentinel file that records the last opened
    // project path (stored in %APPDATA%\OpenVBus\last_project.txt on Windows).
    static std::string recentPath();

    // Reads the sentinel file; returns "" when absent.
    static std::string readRecent();

    // Writes the sentinel file with the given project path.
    static void writeRecent(const std::string &projectPath);
};

} // namespace ovb
