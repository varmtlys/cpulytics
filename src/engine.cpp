#include "engine.h"

#include <algorithm>
#include <thread>

#include "util.h"

namespace cpulytics {

Engine::Engine(const Config& cfg, unsigned cpu_count) : cfg_(cfg) {
    cpus_ = cpu_count ? cpu_count : std::max(1u, std::thread::hardware_concurrency());
}

void Engine::set_config(const Config& cfg) {
    cfg_ = cfg;
    // A shorter window must not leave stale points behind.
    const uint64_t win = (uint64_t)cfg_.window_seconds * 1000;
    for (auto& kv : procs_) {
        auto& h = kv.second.hist;
        while (h.size() > 2 && h.back().t - h.front().t > win) h.pop_front();
    }
}

bool Engine::whitelisted(const std::wstring& name) const {
    const std::wstring n = lower(name);
    for (const auto& w : cfg_.whitelist)
        if (w == n) return true;
    return false;
}

int Engine::max_steps_for(const Track& t) const {
    // A game owns the screen, so its cap wins over the system one it can never have.
    if (t.fullscreen) return std::min(cfg_.fullscreen_max_steps, cfg_.max_steps);
    if (t.system) return std::min(cfg_.system_max_steps, cfg_.max_steps);
    return cfg_.max_steps;
}

bool Engine::is_fullscreen(uint32_t pid) const {
    auto it = procs_.find(pid);
    return it != procs_.end() && it->second.fullscreen;
}

double Engine::window_percent(const Track& t) const {
    if (t.hist.size() < 2) return 0.0;
    const uint64_t dt = t.hist.back().t - t.hist.front().t;
    if (dt == 0) return 0.0;
    const uint64_t dcpu = t.hist.back().cpu - t.hist.front().cpu;  // 100 ns ticks
    const double busy_ms = (double)dcpu / 10000.0;
    return 100.0 * busy_ms / ((double)dt * (double)cpus_);
}

std::vector<Action> Engine::update(uint64_t now, const std::vector<ProcInfo>& procs, uint32_t foreground_pid,
                                   uint32_t fullscreen_pid) {
    const uint64_t win_ms = (uint64_t)cfg_.window_seconds * 1000;

    for (const ProcInfo& p : procs) {
        Track& t = procs_[p.pid];
        if (t.create_time != p.create_time) {
            // Fresh process, or a recycled pid: everything we knew is void.
            t = Track{};
            t.create_time = p.create_time;
        }
        t.name = p.name;
        t.system = p.system;
        t.critical = p.critical;
        t.last_seen = now;
        t.age_ms = p.age_ms;
        // Sticky: a game stays a game after alt-tab, when its window is no longer
        // the one covering the screen.
        if (fullscreen_pid && p.pid == fullscreen_pid) t.fullscreen = true;
        // A CPU counter can only grow; anything else means we are looking at
        // another process, so the history is dropped rather than trusted.
        if (!t.hist.empty() && (p.cpu_time < t.hist.back().cpu || now < t.hist.back().t)) t.hist.clear();
        t.hist.push_back({now, p.cpu_time});
        while (t.hist.size() > 2 && now - t.hist.front().t > win_ms) t.hist.pop_front();
        t.percent = window_percent(t);
    }

    // Processes that vanished are dropped: the map never grows past what is alive.
    for (auto it = procs_.begin(); it != procs_.end();) {
        if (it->second.last_seen != now) it = procs_.erase(it);
        else ++it;
    }
    // Paranoid upper bound in case a machine really runs that many processes:
    // the quietest untouched entries go first.
    if (procs_.size() > (size_t)cfg_.max_tracked) {
        std::vector<std::pair<double, uint32_t>> quiet;
        quiet.reserve(procs_.size());
        for (const auto& kv : procs_)
            if (kv.second.step == 0) quiet.push_back({kv.second.percent, kv.first});
        std::sort(quiet.begin(), quiet.end());
        for (size_t i = 0; i < quiet.size() && procs_.size() > (size_t)cfg_.max_tracked; ++i)
            procs_.erase(quiet[i].second);
    }

    std::vector<Action> out;
    if (!cfg_.enabled) return out;

    const uint64_t min_hist_ms = (uint64_t)cfg_.min_history_seconds * 1000;
    const uint64_t cooldown_ms = (uint64_t)cfg_.action_cooldown_seconds * 1000;
    const uint64_t grace_ms = (uint64_t)cfg_.startup_grace_seconds * 1000;
    const uint64_t calm_ms = (uint64_t)cfg_.restore_after_seconds * 1000;

    for (auto& kv : procs_) {
        Track& t = kv.second;
        const uint32_t pid = kv.first;
        if (t.critical || t.blocked || whitelisted(t.name)) continue;

        const bool foreground = cfg_.protect_foreground && foreground_pid && pid == foreground_pid;

        // The window the user just clicked on gets its priority back at once -
        // whatever it is doing, it is doing it for them.
        if (foreground && t.step > 0) {
            out.push_back({ActionKind::Restore, pid, t.name, t.step, 0, t.percent, t.system});
            continue;
        }

        if (t.hist.size() < 2) continue;
        const uint64_t span = t.hist.back().t - t.hist.front().t;

        if (t.percent <= cfg_.restore_percent) {
            if (t.calm_since == 0) t.calm_since = now;
        } else {
            t.calm_since = 0;
        }

        const bool cooled = now - t.last_action >= cooldown_ms;

        // The cap can be lowered while a process is already demoted (settings
        // changed, or it just went fullscreen): give the steps back immediately.
        if (t.step > max_steps_for(t)) {
            out.push_back({ActionKind::Restore, pid, t.name, t.step, max_steps_for(t), t.percent, t.system});
            continue;
        }

        if (t.step > 0 && t.calm_since && now - t.calm_since >= calm_ms && cooled) {
            out.push_back({ActionKind::Restore, pid, t.name, t.step, t.step - 1, t.percent, t.system});
            continue;
        }

        if (span < min_hist_ms) continue;                 // not enough data to accuse anyone
        if (t.age_ms < grace_ms) continue;  // still in its startup burst
        if (foreground) continue;
        if (t.percent < cfg_.demote_percent) continue;
        if (t.step >= max_steps_for(t) || !cooled) continue;

        out.push_back({ActionKind::Demote, pid, t.name, t.step, t.step + 1, t.percent, t.system});
    }
    return out;
}

void Engine::applied(uint32_t pid, int step, uint32_t orig_class) {
    auto it = procs_.find(pid);
    if (it == procs_.end()) return;
    Track& t = it->second;
    if (t.step == 0 && step > 0 && orig_class) t.orig_class = orig_class;
    t.step = step;
    if (step == 0) t.orig_class = 0;
    t.last_action = t.hist.empty() ? 0 : t.hist.back().t;
    t.calm_since = 0;
}

void Engine::failed(uint32_t pid) {
    auto it = procs_.find(pid);
    if (it != procs_.end()) it->second.blocked = true;
}

std::vector<std::pair<uint32_t, uint32_t>> Engine::modified() const {
    std::vector<std::pair<uint32_t, uint32_t>> v;
    for (const auto& kv : procs_)
        if (kv.second.step > 0 && kv.second.orig_class) v.push_back({kv.first, kv.second.orig_class});
    return v;
}

uint32_t Engine::orig_class(uint32_t pid) const {
    auto it = procs_.find(pid);
    return it == procs_.end() ? 0 : it->second.orig_class;
}

void Engine::forget_all() {
    for (auto& kv : procs_) {
        kv.second.step = 0;
        kv.second.orig_class = 0;
        kv.second.calm_since = 0;
    }
}

std::vector<Usage> Engine::top(size_t n) const {
    std::vector<Usage> v;
    v.reserve(procs_.size());
    for (const auto& kv : procs_) v.push_back({kv.first, kv.second.name, kv.second.percent, kv.second.step});
    std::sort(v.begin(), v.end(), [](const Usage& a, const Usage& b) { return a.percent > b.percent; });
    if (v.size() > n) v.resize(n);
    return v;
}

}  // namespace cpulytics
