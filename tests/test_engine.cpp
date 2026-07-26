// Decision logic tests: no real process is touched, the clock and the CPU
// counters are synthetic, so the whole file runs in milliseconds.
#include <cstdio>
#include <string>
#include <vector>

#include "engine.h"

using namespace cpulytics;

namespace {

int g_failed = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
            ++g_failed;                                                        \
        }                                                                      \
    } while (0)

constexpr unsigned kCpus = 8;
constexpr uint32_t kNormal = 32;  // NORMAL_PRIORITY_CLASS

Config base_config() {
    Config c;
    c.sample_interval_ms = 1000;
    c.window_seconds = 20;
    c.min_history_seconds = 10;
    c.demote_percent = 20.0;
    c.restore_percent = 5.0;
    c.startup_grace_seconds = 5;
    c.action_cooldown_seconds = 5;
    c.restore_after_seconds = 10;
    c.max_steps = 2;
    c.system_max_steps = 1;
    c.protect_foreground = true;
    return c;
}

struct Sim {
    Engine eng;
    std::vector<ProcInfo> procs;
    uint64_t now = 0;
    int interval = 1000;

    explicit Sim(const Config& c) : eng(c, kCpus) {}

    ProcInfo& add(uint32_t pid, const wchar_t* name, uint64_t age_ms = 3600000) {
        ProcInfo p;
        p.pid = pid;
        p.name = name;
        p.create_time = 1000 + pid;
        p.age_ms = age_ms;
        procs.push_back(p);
        return procs.back();
    }

    // One sample where every process burned `percent` of the whole machine.
    std::vector<Action> tick(const std::vector<double>& percent, uint32_t fg = 0, uint32_t fs = 0,
                             bool auto_apply = true) {
        now += (uint64_t)interval;
        for (size_t i = 0; i < procs.size(); ++i) {
            procs[i].cpu_time += (uint64_t)(percent[i] / 100.0 * interval * kCpus * 10000.0);
            procs[i].age_ms += (uint64_t)interval;
        }
        auto acts = eng.update(now, procs, fg, fs);
        if (auto_apply)
            for (const Action& a : acts) eng.applied(a.pid, a.to_step, kNormal);
        return acts;
    }

    std::vector<Action> run(int ticks, const std::vector<double>& percent, uint32_t fg = 0, uint32_t fs = 0) {
        std::vector<Action> all;
        for (int i = 0; i < ticks; ++i) {
            auto acts = tick(percent, fg, fs);
            all.insert(all.end(), acts.begin(), acts.end());
        }
        return all;
    }
};

void test_sustained_load_is_demoted() {
    Sim s(base_config());
    s.add(1000, L"hog.exe");
    auto early = s.run(9, {50.0});
    CHECK(early.empty());  // window not full enough yet

    auto acts = s.run(6, {50.0});
    CHECK(acts.size() == 1);
    CHECK(acts[0].kind == ActionKind::Demote);
    CHECK(acts[0].pid == 1000);
    CHECK(acts[0].to_step == 1);
    CHECK(acts[0].percent > 40.0);

    // Second step only after the cooldown, and never past max_steps.
    auto more = s.run(30, {50.0});
    int demotions = 0;
    for (const Action& a : more)
        if (a.kind == ActionKind::Demote) ++demotions;
    CHECK(demotions == 1);
    CHECK(s.eng.top(1)[0].step == 2);
}

void test_quiet_process_is_left_alone() {
    Sim s(base_config());
    s.add(1000, L"quiet.exe");
    CHECK(s.run(40, {3.0}).empty());
}

void test_startup_burst_is_ignored() {
    Config c = base_config();
    c.startup_grace_seconds = 60;
    Sim s(c);
    s.add(1000, L"launching.exe", 0);
    CHECK(s.run(40, {90.0}).empty());  // 40 s old, still inside the grace period

    auto later = s.run(30, {90.0});  // now past 60 s and still burning
    CHECK(!later.empty() && later[0].kind == ActionKind::Demote);
}

void test_system_process_gets_one_gentle_step() {
    Sim s(base_config());
    s.add(1000, L"svchost.exe").system = true;
    auto acts = s.run(60, {70.0});
    int demotions = 0;
    for (const Action& a : acts)
        if (a.kind == ActionKind::Demote) ++demotions;
    CHECK(demotions == 1);
    CHECK(s.eng.top(1)[0].step == 1);
}

void test_critical_and_whitelisted_are_untouchable() {
    Config c = base_config();
    c.whitelist.push_back(L"mygame.exe");
    Sim s(c);
    s.add(1000, L"csrss.exe").critical = true;
    s.add(1001, L"mygame.exe");
    CHECK(s.run(60, {80.0, 80.0}).empty());
}

void test_calm_process_gets_priority_back() {
    Sim s(base_config());
    s.add(1000, L"hog.exe");
    s.run(20, {50.0});
    CHECK(s.eng.top(1)[0].step > 0);

    auto acts = s.run(60, {0.0});
    int restores = 0;
    for (const Action& a : acts)
        if (a.kind == ActionKind::Restore) ++restores;
    CHECK(restores >= 1);
    CHECK(s.eng.top(1)[0].step == 0);
    CHECK(s.eng.modified().empty());
}

void test_foreground_is_protected_and_restored_at_once() {
    Sim s(base_config());
    s.add(1000, L"editor.exe");
    CHECK(s.run(40, {60.0}, 1000).empty());  // in front the whole time

    s.run(20, {60.0});  // user switched away, it gets demoted
    CHECK(s.eng.top(1)[0].step > 0);

    auto back = s.tick({60.0}, 1000);  // and back in front again
    CHECK(back.size() == 1);
    CHECK(back[0].kind == ActionKind::Restore);
    CHECK(back[0].to_step == 0);
}

void test_fullscreen_app_is_immune_by_default() {
    Sim s(base_config());  // fullscreen_max_steps defaults to 0
    s.add(1000, L"game.exe");
    CHECK(s.run(60, {90.0}, 0, 1000).empty());
    CHECK(s.eng.is_fullscreen(1000));

    // Still immune after alt-tab: the flag sticks to the process.
    CHECK(s.run(30, {90.0}).empty());
}

void test_fullscreen_app_can_be_demoted_when_allowed() {
    Config c = base_config();
    c.fullscreen_max_steps = 1;
    Sim s(c);
    s.add(1000, L"game.exe");
    auto acts = s.run(60, {90.0}, 0, 1000);
    int demotions = 0;
    for (const Action& a : acts)
        if (a.kind == ActionKind::Demote) ++demotions;
    CHECK(demotions == 1);  // one step, not two
    CHECK(s.eng.top(1)[0].step == 1);
}

void test_lowering_the_cap_restores_immediately() {
    Config c = base_config();
    c.fullscreen_max_steps = 2;
    Sim s(c);
    s.add(1000, L"game.exe");
    s.run(40, {90.0}, 0, 1000);
    CHECK(s.eng.top(1)[0].step == 2);

    c.fullscreen_max_steps = 0;  // user changed the setting
    s.eng.set_config(c);
    auto acts = s.run(2, {90.0});
    CHECK(!acts.empty() && acts[0].kind == ActionKind::Restore);
    CHECK(s.eng.top(1)[0].step == 0);
}

void test_dead_processes_are_forgotten() {
    Sim s(base_config());
    s.add(1000, L"hog.exe");
    s.add(1001, L"other.exe");
    s.run(5, {50.0, 50.0});
    CHECK(s.eng.tracked() == 2);

    s.procs.pop_back();  // 1001 exits
    s.run(2, {50.0});
    CHECK(s.eng.tracked() == 1);
}

void test_recycled_pid_starts_from_scratch() {
    Sim s(base_config());
    s.add(1000, L"hog.exe");
    s.run(15, {50.0});
    CHECK(s.eng.top(1)[0].step > 0);

    // Same pid, different process: history and demotion state must not carry over.
    s.procs[0].create_time += 777;
    s.procs[0].name = L"newcomer.exe";
    s.procs[0].cpu_time = 0;
    s.procs[0].age_ms = 0;
    auto acts = s.run(9, {50.0});
    CHECK(acts.empty());
    CHECK(s.eng.top(1)[0].step == 0);
    CHECK(s.eng.modified().empty());
}

void test_disabled_engine_does_nothing() {
    Config c = base_config();
    c.enabled = false;
    Sim s(c);
    s.add(1000, L"hog.exe");
    CHECK(s.run(60, {90.0}).empty());
}

void test_config_is_sanitized() {
    Config c;
    c.window_seconds = 5;          // below the minimum
    c.demote_percent = 900.0;      // impossible
    c.restore_percent = 999.0;     // above demote
    c.max_steps = 7;               // deeper than the ladder
    c.system_max_steps = 7;
    c.sanitize();
    CHECK(c.window_seconds >= 30);
    CHECK(c.demote_percent <= 100.0);
    CHECK(c.restore_percent < c.demote_percent);
    CHECK(c.max_steps == 2);
    CHECK(c.system_max_steps <= c.max_steps);
    CHECK(c.min_history_seconds <= c.window_seconds);
}

}  // namespace

int main() {
    test_sustained_load_is_demoted();
    test_quiet_process_is_left_alone();
    test_startup_burst_is_ignored();
    test_system_process_gets_one_gentle_step();
    test_critical_and_whitelisted_are_untouchable();
    test_calm_process_gets_priority_back();
    test_foreground_is_protected_and_restored_at_once();
    test_fullscreen_app_is_immune_by_default();
    test_fullscreen_app_can_be_demoted_when_allowed();
    test_lowering_the_cap_restores_immediately();
    test_dead_processes_are_forgotten();
    test_recycled_pid_starts_from_scratch();
    test_disabled_engine_does_nothing();
    test_config_is_sanitized();

    std::printf(g_failed ? "engine tests: %d failure(s)\n" : "engine tests: ok\n", g_failed);
    return g_failed ? 1 : 0;
}
