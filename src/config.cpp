#include "config.h"

#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "util.h"

namespace cpulytics {
namespace {

std::wstring dir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return L".";
    return std::wstring(buf) + L"\\cpulytics";
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool as_bool(const std::string& v, bool def) {
    std::string s = trim(v);
    if (s == "1" || s == "true" || s == "yes" || s == "on") return true;
    if (s == "0" || s == "false" || s == "no" || s == "off") return false;
    return def;
}

int as_int(const std::string& v, int def) {
    try {
        return std::stoi(trim(v));
    } catch (...) {
        return def;
    }
}

double as_double(const std::string& v, double def) {
    try {
        return std::stod(trim(v));
    } catch (...) {
        return def;
    }
}

template <typename T>
void clamp_to(T& v, T lo, T hi) {
    v = std::min(hi, std::max(lo, v));
}

}  // namespace

std::wstring Config::path() { return dir() + L"\\config.ini"; }

void Config::sanitize() {
    clamp_to(sample_interval_ms, 250, 60000);
    clamp_to(window_seconds, 30, 7200);
    clamp_to(min_history_seconds, 5, window_seconds);
    clamp_to(demote_percent, 1.0, 100.0);
    clamp_to(restore_percent, 0.0, demote_percent - 0.5);
    clamp_to(startup_grace_seconds, 0, 3600);
    clamp_to(action_cooldown_seconds, 1, 3600);
    clamp_to(restore_after_seconds, 1, 7200);
    clamp_to(max_steps, 0, 2);
    clamp_to(system_max_steps, 0, max_steps);
    clamp_to(fullscreen_max_steps, 0, max_steps);
    clamp_to(max_tracked, 64, 65536);
    clamp_to(log_max_kb, 16, 65536);
}

Config Config::load() {
    Config c;
    const std::filesystem::path file(path());
    std::ifstream in(file);
    if (!in) {
        c.save();
        return c;
    }
    std::string line;
    while (std::getline(in, line)) {
        std::string s = trim(line);
        if (s.empty() || s[0] == '#' || s[0] == ';' || s[0] == '[') continue;
        size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(s.substr(0, eq));
        std::string v = trim(s.substr(eq + 1));

        if (k == "sample_interval_ms") c.sample_interval_ms = as_int(v, c.sample_interval_ms);
        else if (k == "window_seconds") c.window_seconds = as_int(v, c.window_seconds);
        else if (k == "min_history_seconds") c.min_history_seconds = as_int(v, c.min_history_seconds);
        else if (k == "demote_percent") c.demote_percent = as_double(v, c.demote_percent);
        else if (k == "restore_percent") c.restore_percent = as_double(v, c.restore_percent);
        else if (k == "startup_grace_seconds") c.startup_grace_seconds = as_int(v, c.startup_grace_seconds);
        else if (k == "action_cooldown_seconds") c.action_cooldown_seconds = as_int(v, c.action_cooldown_seconds);
        else if (k == "restore_after_seconds") c.restore_after_seconds = as_int(v, c.restore_after_seconds);
        else if (k == "max_steps") c.max_steps = as_int(v, c.max_steps);
        else if (k == "system_max_steps") c.system_max_steps = as_int(v, c.system_max_steps);
        else if (k == "fullscreen_max_steps") c.fullscreen_max_steps = as_int(v, c.fullscreen_max_steps);
        else if (k == "enabled") c.enabled = as_bool(v, c.enabled);
        else if (k == "protect_foreground") c.protect_foreground = as_bool(v, c.protect_foreground);
        else if (k == "notifications") c.notifications = as_bool(v, c.notifications);
        else if (k == "restore_on_exit") c.restore_on_exit = as_bool(v, c.restore_on_exit);
        else if (k == "log_enabled") c.log_enabled = as_bool(v, c.log_enabled);
        else if (k == "language") c.language = lower(widen(trim(v)));
        else if (k == "max_tracked") c.max_tracked = as_int(v, c.max_tracked);
        else if (k == "log_max_kb") c.log_max_kb = as_int(v, c.log_max_kb);
        else if (k == "whitelist") {
            std::stringstream ss(v);
            std::string item;
            while (std::getline(ss, item, ',')) {
                std::string t = trim(item);
                if (!t.empty()) c.whitelist.push_back(lower(widen(t)));
            }
        }
    }
    c.sanitize();
    return c;
}

bool Config::save() const {
    CreateDirectoryW(dir().c_str(), nullptr);
    const std::filesystem::path file(path());
    std::ofstream out(file, std::ios::trunc);
    if (!out) return false;

    std::string wl;
    for (size_t i = 0; i < whitelist.size(); ++i) {
        if (i) wl += ", ";
        wl += narrow(whitelist[i]);
    }

    out << "; cpulytics settings. Restart or \"Reload settings\" from the tray menu to apply.\n"
        << "[cpulytics]\n\n"
        << "; master switch, also toggled from the tray menu\n"
        << "enabled = " << (enabled ? "true" : "false") << "\n\n"
        << "; how often the process table is sampled, milliseconds\n"
        << "sample_interval_ms = " << sample_interval_ms << "\n"
        << "; sliding window used to average CPU usage, seconds\n"
        << "window_seconds = " << window_seconds << "\n"
        << "; refuse to decide anything before the window holds this much data, seconds\n"
        << "min_history_seconds = " << min_history_seconds << "\n\n"
        << "; average CPU over the window that triggers a demotion, percent of all cores\n"
        << "demote_percent = " << demote_percent << "\n"
        << "; average CPU below which a demoted process is given a step back\n"
        << "restore_percent = " << restore_percent << "\n\n"
        << "; a process is left alone for this long after it starts, seconds (startup bursts)\n"
        << "startup_grace_seconds = " << startup_grace_seconds << "\n"
        << "; minimum gap between two changes of the same process, seconds\n"
        << "action_cooldown_seconds = " << action_cooldown_seconds << "\n"
        << "; how long a process must stay calm before a step is given back, seconds\n"
        << "restore_after_seconds = " << restore_after_seconds << "\n\n"
        << "; demotion depth: 0 = off, 1 = below normal, 2 = down to idle\n"
        << "max_steps = " << max_steps << "\n"
        << "; same, for system processes - they are never pushed harder than this\n"
        << "system_max_steps = " << system_max_steps << "\n"
        << "; same, for apps seen running fullscreen (games): 0 leaves them untouched\n"
        << "fullscreen_max_steps = " << fullscreen_max_steps << "\n\n"
        << "; never demote the process that owns the foreground window\n"
        << "protect_foreground = " << (protect_foreground ? "true" : "false") << "\n"
        << "; show a balloon on every change\n"
        << "notifications = " << (notifications ? "true" : "false") << "\n"
        << "; restore every touched process when the app exits\n"
        << "restore_on_exit = " << (restore_on_exit ? "true" : "false") << "\n"
        << "; write cpulytics.log next to this file\n"
        << "log_enabled = " << (log_enabled ? "true" : "false") << "\n\n"
        << "; upper bound on tracked processes, protects memory\n"
        << "max_tracked = " << max_tracked << "\n"
        << "; log is truncated once it grows past this, kilobytes\n"
        << "log_max_kb = " << log_max_kb << "\n\n"
        << "; comma separated executable names that are never touched\n"
        << "whitelist = " << wl << "\n";
    return out.good();
}

}  // namespace cpulytics
