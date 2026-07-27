#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

#include "config.h"

namespace cpulytics {

// One process as seen in a single sample. Filled by the sampler (sysinfo.cpp) or,
// in tests, by hand - the engine never touches the OS itself.
struct ProcInfo {
    uint32_t pid = 0;
    uint64_t create_time = 0;  // FILETIME ticks, identifies a pid incarnation
    uint64_t cpu_time = 0;     // kernel + user, 100 ns ticks, monotonic per process
    uint64_t age_ms = 0;       // time since the process was created
    std::wstring name;         // executable name
    bool critical = false;     // never touched at all
    bool system = false;       // touched gently: at most system_max_steps
};

enum class ActionKind { Demote, Restore };

// A decision. The host applies it and reports back through applied()/failed().
struct Action {
    ActionKind kind = ActionKind::Demote;
    uint32_t pid = 0;
    std::wstring name;
    int from_step = 0;
    int to_step = 0;
    double percent = 0.0;  // window average that caused the decision
    bool system = false;
};

// A process we currently hold demoted. create_time makes the pid unambiguous
// after a restart, when another process may already own that number.
struct Held {
    uint32_t pid = 0;
    uint64_t create_time = 0;
    uint32_t orig_class = 0;
    int step = 0;
};

// A row for the tray menu / tooltip.
struct Usage {
    uint32_t pid = 0;
    std::wstring name;
    double percent = 0.0;
    int step = 0;
};

class Engine {
public:
    explicit Engine(const Config& cfg, unsigned cpu_count = 0);

    void set_config(const Config& cfg);
    const Config& config() const { return cfg_; }

    // Feeds one sample of the whole process table. now is a monotonic millisecond
    // clock, foreground_pid may be 0. Returns the changes the host should apply.
    std::vector<Action> update(uint64_t now, const std::vector<ProcInfo>& procs, uint32_t foreground_pid,
                               uint32_t fullscreen_pid = 0);

    // Host feedback. orig_class is remembered on the first successful demotion so
    // the process can be put back exactly where it was.
    void applied(uint32_t pid, int step, uint32_t orig_class);
    // Could not change this process (no rights, gone): stop trying until it restarts.
    void failed(uint32_t pid);

    // Processes we currently hold demoted, with the class they had before.
    std::vector<Held> modified() const;
    void forget_all();

    // Priority class remembered before the first demotion, 0 when untouched.
    uint32_t orig_class(uint32_t pid) const;

    std::vector<Usage> top(size_t n) const;
    bool is_fullscreen(uint32_t pid) const;
    size_t tracked() const { return procs_.size(); }

private:
    struct Point {
        uint64_t t;
        uint64_t cpu;
    };
    struct Track {
        uint64_t create_time = 0;
        std::deque<Point> hist;
        std::wstring name;
        bool system = false;
        bool critical = false;
        bool fullscreen = false;  // seen owning a fullscreen window at least once
        int step = 0;
        uint32_t orig_class = 0;
        uint64_t last_action = 0;
        uint64_t calm_since = 0;  // 0 = not calm right now
        uint64_t last_seen = 0;
        uint64_t age_ms = 0;
        double percent = 0.0;
        bool blocked = false;  // priority change failed once, do not retry
    };

    int max_steps_for(const Track& t) const;
    bool whitelisted(const std::wstring& name) const;
    double window_percent(const Track& t) const;

    Config cfg_;
    unsigned cpus_ = 1;
    std::unordered_map<uint32_t, Track> procs_;
};

}  // namespace cpulytics
