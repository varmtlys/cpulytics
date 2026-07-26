#pragma once

#include <string>
#include <vector>

namespace cpulytics {

// All tunables live here. Loaded from an INI file next to %APPDATA%\cpulytics.
struct Config {
    // sampling
    int sample_interval_ms = 2000;   // how often the process table is read
    int window_seconds = 600;        // sliding window length (10 min)
    int min_history_seconds = 120;   // no decisions until the window is this full

    // thresholds, percent of total CPU capacity (all cores = 100%)
    double demote_percent = 20.0;
    double restore_percent = 8.0;    // hysteresis: must be well below demote_percent

    // timing
    int startup_grace_seconds = 60;  // ignore the burst right after a process starts
    int action_cooldown_seconds = 60;// min gap between two changes for one process
    int restore_after_seconds = 120; // how long a process must stay calm to get a step back

    // how deep we are allowed to go: 1 = below normal, 2 = idle
    int max_steps = 2;
    int system_max_steps = 1;        // system processes get one gentle step at most

    // behaviour
    bool enabled = true;             // master switch, toggled from the tray menu
    bool protect_foreground = true;  // never demote the process owning the foreground window
    bool notifications = true;       // balloon on every change
    bool restore_on_exit = true;     // put every touched process back on shutdown
    bool log_enabled = true;

    // safety limits
    int max_tracked = 2048;          // hard cap on the history map
    int log_max_kb = 512;            // log is truncated when it grows past this

    // lowercase executable names that are never touched, in addition to the built-in list
    std::vector<std::wstring> whitelist;

    // Path of the INI file (%APPDATA%\cpulytics\config.ini).
    static std::wstring path();

    // Reads the INI, writing a default one first if it does not exist.
    // Invalid values fall back to the defaults, a broken file never stops the app.
    static Config load();

    // Writes the current values with comments. Used to seed a missing file.
    bool save() const;

    // Clamps every field into a sane range. Called by load().
    void sanitize();
};

}  // namespace cpulytics
