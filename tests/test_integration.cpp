// End to end test against the real Windows process table: this binary starts a
// copy of itself that burns a core, then runs the real sampler and the real
// engine over it and checks that the child is actually demoted and, once it goes
// quiet, actually restored.
#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

#include "config.h"
#include "i18n.h"
#include "icon.h"
#include "engine.h"
#include "sysinfo.h"
#include "theme.h"
#include "util.h"

using namespace cpulytics;

namespace {

int g_failed = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failed;                                                 \
        }                                                               \
    } while (0)

constexpr int kBurnSeconds = 10;

void burn() {
    const uint64_t until = now_ms() + kBurnSeconds * 1000;
    volatile double x = 1.0;
    while (now_ms() < until) {
        for (int i = 0; i < 100000; ++i) x = x * 1.000001 + 0.5;
    }
    Sleep(120000);  // stay alive and idle until the parent kills us
}

Config test_config() {
    Config c;
    c.sample_interval_ms = 200;
    c.window_seconds = 6;
    c.min_history_seconds = 3;
    c.demote_percent = 4.0;  // one busy core is well above this on any core count
    c.restore_percent = 2.0;
    c.startup_grace_seconds = 0;
    c.action_cooldown_seconds = 1;
    c.restore_after_seconds = 2;
    c.max_steps = 1;
    c.protect_foreground = false;
    c.sanitize();
    return c;
}

struct Child {
    PROCESS_INFORMATION pi{};
    ~Child() {
        if (pi.hProcess) {
            TerminateProcess(pi.hProcess, 0);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
    }
};

bool spawn(Child& c) {
    wchar_t exe[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) return false;
    std::wstring cmd = L"\"" + std::wstring(exe) + L"\" --burn";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    return CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &c.pi) != 0;
}

// Samples until pred() says stop or the deadline passes. Returns the actions seen.
std::vector<Action> pump(Engine& eng, sys::Sampler& sampler, const Config& cfg, int seconds,
                         bool (*stop)(const std::vector<Action>&)) {
    std::vector<Action> seen;
    std::vector<ProcInfo> procs;
    const uint64_t deadline = now_ms() + (uint64_t)seconds * 1000;
    while (now_ms() < deadline) {
        Sleep((DWORD)cfg.sample_interval_ms);
        if (!sampler.sample(procs)) continue;
        for (const Action& a : eng.update(now_ms(), procs, 0)) {
            const uint32_t orig = a.from_step > 0 ? eng.orig_class(a.pid) : sys::priority_class(a.pid);
            if (!orig || !sys::apply_step(a.pid, orig, a.to_step)) {
                eng.failed(a.pid);
                continue;
            }
            eng.applied(a.pid, a.to_step, orig);
            seen.push_back(a);
        }
        if (stop && stop(seen)) break;
    }
    return seen;
}

bool saw_demote(const std::vector<Action>& v) {
    for (const Action& a : v)
        if (a.kind == ActionKind::Demote) return true;
    return false;
}

bool saw_restore(const std::vector<Action>& v) {
    for (const Action& a : v)
        if (a.kind == ActionKind::Restore) return true;
    return false;
}

void test_sampler_sees_this_process() {
    sys::Sampler s;
    std::vector<ProcInfo> procs;
    CHECK(s.sample(procs));
    CHECK(procs.size() > 10);

    const uint32_t self = GetCurrentProcessId();
    bool found_self = false, found_critical = false;
    for (const ProcInfo& p : procs) {
        if (p.pid == self) {
            found_self = true;
            CHECK(p.critical);  // we never manage ourselves
            CHECK(!p.name.empty());
        }
        if (lower(p.name) == L"csrss.exe" || lower(p.name) == L"lsass.exe") found_critical = p.critical;
    }
    CHECK(found_self);
    CHECK(found_critical);
}

void test_priority_ladder() {
    CHECK(sys::class_for_step(NORMAL_PRIORITY_CLASS, 1) == BELOW_NORMAL_PRIORITY_CLASS);
    CHECK(sys::class_for_step(NORMAL_PRIORITY_CLASS, 2) == IDLE_PRIORITY_CLASS);
    CHECK(sys::class_for_step(NORMAL_PRIORITY_CLASS, 9) == IDLE_PRIORITY_CLASS);
    CHECK(sys::class_for_step(BELOW_NORMAL_PRIORITY_CLASS, 1) == IDLE_PRIORITY_CLASS);
    CHECK(sys::class_for_step(HIGH_PRIORITY_CLASS, 1) == ABOVE_NORMAL_PRIORITY_CLASS);
    CHECK(sys::class_for_step(NORMAL_PRIORITY_CLASS, 0) == NORMAL_PRIORITY_CLASS);
    CHECK(!sys::apply_step(GetCurrentProcessId(), REALTIME_PRIORITY_CLASS, 1));
    CHECK(sys::priority_class(GetCurrentProcessId()) != 0);
    CHECK(sys::apply_step(0xFFFFFF00u, NORMAL_PRIORITY_CLASS, 1) == false);  // no such process
}

void test_embedded_icon_decodes() {
    const auto raw = decode_base64(icon_base64());
    CHECK(raw.size() > 1000);
    CHECK(raw[0] == 0 && raw[1] == 0 && raw[2] == 1 && raw[3] == 0);  // ICONDIR, type 1
    for (int size : {16, 24, 32, 64}) {
        HICON h = load_icon(size, size);
        CHECK(h != nullptr);
        if (h) DestroyIcon(h);
    }
}

// A missing row in the string table would silently fall back to nothing, so every
// id is checked in every language.
void test_every_string_is_translated() {
    for (const wchar_t* const* code = language_codes(); *code; ++code) {
        set_language(*code);
        for (int id = 0; id < S_COUNT; ++id) {
            const wchar_t* s = tr((Str)id);
            if (!s || !*s) {
                std::printf("FAIL empty string id %d for language %ls\n", id, *code);
                ++g_failed;
            }
        }
    }
    set_language(L"ar");
    CHECK(rtl());
    set_language(L"en");
    CHECK(!rtl());
    set_language(L"nonsense");  // unknown codes fall back to english
    CHECK(std::wstring(tr(S_SAVE)) == L"Save");
}

// EcoQoS is a windows 11 / 10 21H1 feature; on anything older it simply cannot be
// set, and that must not be reported as a working one.
void test_eco_qos_round_trip() {
    const uint32_t self = GetCurrentProcessId();
    const bool supported = sys::set_eco_qos(self, true);
    if (!supported) {
        std::printf("note: EcoQoS not available on this windows, skipping\n");
        return;
    }
    CHECK(sys::eco_qos(self));
    CHECK(sys::set_eco_qos(self, false));
    CHECK(!sys::eco_qos(self));

    // A demotion with eco on sets both, a restore clears both.
    CHECK(sys::apply_step(self, NORMAL_PRIORITY_CLASS, 1, true));
    CHECK(sys::priority_class(self) == BELOW_NORMAL_PRIORITY_CLASS);
    CHECK(sys::eco_qos(self));
    CHECK(sys::apply_step(self, NORMAL_PRIORITY_CLASS, 0, true));
    CHECK(sys::priority_class(self) == NORMAL_PRIORITY_CLASS);
    CHECK(!sys::eco_qos(self));
}

void test_theme_palette_and_dpi() {
    theme::init_process();  // must be safe to call even where uxtheme has no dark mode
    const auto& p = theme::palette();
    CHECK(p.window != p.text);   // a palette that cannot be read is useless
    CHECK(p.surface != p.text);
    CHECK(theme::window_brush() != nullptr);
    CHECK(theme::surface_brush() != nullptr);
    CHECK(theme::dpi_of(nullptr) >= 96);

    // Nothing here may crash on a window that is not themed at all.
    HWND w = CreateWindowExW(0, L"STATIC", L"x", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, nullptr, nullptr);
    CHECK(w != nullptr);
    if (w) {
        theme::apply_window(w);
        theme::apply_control(w, true);
        CHECK(theme::dpi_of(w) >= 96);
        DestroyWindow(w);
    }
}

void test_hog_is_demoted_then_restored() {
    Child child;
    if (!spawn(child)) {
        std::printf("FAIL could not start the burner child\n");
        ++g_failed;
        return;
    }
    const uint32_t pid = child.pi.dwProcessId;
    CHECK(sys::priority_class(pid) == NORMAL_PRIORITY_CLASS);

    Config cfg = test_config();
    Engine eng(cfg);
    sys::Sampler sampler;

    auto acts = pump(eng, sampler, cfg, kBurnSeconds + 10, saw_demote);
    bool demoted_child = false;
    for (const Action& a : acts)
        if (a.kind == ActionKind::Demote && a.pid == pid) demoted_child = true;
    CHECK(demoted_child);
    CHECK(sys::priority_class(pid) == BELOW_NORMAL_PRIORITY_CLASS);
    CHECK(eng.modified().size() >= 1);

    // The child stops burning by itself; the window average decays and the
    // priority has to come back on its own.
    auto back = pump(eng, sampler, cfg, kBurnSeconds + 25, saw_restore);
    bool restored_child = false;
    for (const Action& a : back)
        if (a.kind == ActionKind::Restore && a.pid == pid) restored_child = true;
    CHECK(restored_child);
    CHECK(sys::priority_class(pid) == NORMAL_PRIORITY_CLASS);
    CHECK(eng.modified().empty());
}

void test_exited_process_leaves_no_trace() {
    Child child;
    if (!spawn(child)) return;
    const uint32_t pid = child.pi.dwProcessId;

    Config cfg = test_config();
    Engine eng(cfg);
    sys::Sampler sampler;
    std::vector<ProcInfo> procs;
    sampler.sample(procs);
    eng.update(now_ms(), procs, 0);
    const size_t with_child = eng.tracked();

    TerminateProcess(child.pi.hProcess, 0);
    WaitForSingleObject(child.pi.hProcess, 5000);
    Sleep(300);

    sampler.sample(procs);
    eng.update(now_ms(), procs, 0);
    bool still_there = false;
    for (const Usage& u : eng.top(4096))
        if (u.pid == pid) still_there = true;
    CHECK(!still_there);
    CHECK(eng.tracked() <= with_child);
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--burn") {
            burn();
            return 0;
        }
    }

    test_sampler_sees_this_process();
    test_priority_ladder();
    test_embedded_icon_decodes();
    test_every_string_is_translated();
    test_eco_qos_round_trip();
    test_theme_palette_and_dpi();
    test_hog_is_demoted_then_restored();
    test_exited_process_leaves_no_trace();

    std::printf(g_failed ? "integration tests: %d failure(s)\n" : "integration tests: ok\n", g_failed);
    return g_failed ? 1 : 0;
}
