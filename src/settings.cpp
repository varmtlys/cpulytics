#include "settings.h"

#include <cstdlib>
#include <string>
#include <vector>

#include "util.h"

namespace cpulytics {
namespace {

// The window is built from this table instead of a resource script: one row per
// setting, so adding a setting is one line here and one line in Config.
enum class Kind { Int, Real, Bool, Text };

struct Field {
    const wchar_t* label;
    const wchar_t* hint;
    Kind kind;
    void* ptr;
};

std::vector<Field> fields_of(Config& c) {
    return {
        {L"Manage priorities", L"master switch", Kind::Bool, &c.enabled},
        {L"Sample interval", L"ms between reads of the process table", Kind::Int, &c.sample_interval_ms},
        {L"Window", L"seconds of history the average is taken over", Kind::Int, &c.window_seconds},
        {L"Minimum history", L"seconds of data before anything is decided", Kind::Int, &c.min_history_seconds},
        {L"Demote above", L"% of all cores, averaged over the window", Kind::Real, &c.demote_percent},
        {L"Restore below", L"% of all cores, must stay under the demote level", Kind::Real, &c.restore_percent},
        {L"Startup grace", L"seconds a freshly started process is left alone", Kind::Int, &c.startup_grace_seconds},
        {L"Cooldown", L"seconds between two changes of one process", Kind::Int, &c.action_cooldown_seconds},
        {L"Calm for", L"seconds of quiet before a step is given back", Kind::Int, &c.restore_after_seconds},
        {L"Steps down", L"0 off, 1 below normal, 2 down to idle", Kind::Int, &c.max_steps},
        {L"Steps for system", L"same cap for session 0 processes", Kind::Int, &c.system_max_steps},
        {L"Steps for fullscreen", L"games and players: 0 leaves them untouched", Kind::Int, &c.fullscreen_max_steps},
        {L"Protect foreground", L"never demote the window you are using", Kind::Bool, &c.protect_foreground},
        {L"Notifications", L"balloon on every change", Kind::Bool, &c.notifications},
        {L"Restore on exit", L"put everything back when cpulytics stops", Kind::Bool, &c.restore_on_exit},
        {L"Write log", L"cpulytics.log next to the settings file", Kind::Bool, &c.log_enabled},
        {L"Max tracked", L"upper bound on the history map", Kind::Int, &c.max_tracked},
        {L"Log size", L"KB, the log is truncated past this", Kind::Int, &c.log_max_kb},
        {L"Never touch", L"executable names, comma separated", Kind::Text, &c.whitelist},
    };
}

constexpr int kRowH = 26;
constexpr int kLabelW = 130;
constexpr int kCtrlW = 90;
constexpr int kHintX = kLabelW + kCtrlW + 24;
constexpr int kWidth = 580;
constexpr int kIdFirst = 1000;
constexpr int kIdSave = IDOK;          // Enter saves
constexpr int kIdCancel = IDCANCEL;    // Escape closes
constexpr int kIdDefaults = 3;

std::wstring field_text(const Field& f) {
    switch (f.kind) {
        case Kind::Int: return std::to_wstring(*(int*)f.ptr);
        case Kind::Real: {
            wchar_t buf[32];
            const double v = *(double*)f.ptr;
            wsprintfW(buf, L"%d.%d", (int)v, (int)((v - (int)v) * 10 + 0.5));
            return buf;
        }
        case Kind::Bool: return *(bool*)f.ptr ? L"1" : L"0";
        case Kind::Text: {
            const auto& list = *(std::vector<std::wstring>*)f.ptr;
            std::wstring s;
            for (size_t i = 0; i < list.size(); ++i) {
                if (i) s += L", ";
                s += list[i];
            }
            return s;
        }
    }
    return L"";
}

struct State {
    Config cfg;
    std::vector<Field> fields;
    std::vector<HWND> ctrl;
    bool saved = false;
    HFONT font = nullptr;
};

State* state_of(HWND h) { return reinterpret_cast<State*>(GetWindowLongPtrW(h, GWLP_USERDATA)); }

void fill_controls(State* st) {
    for (size_t i = 0; i < st->fields.size(); ++i) {
        if (st->fields[i].kind == Kind::Bool)
            SendMessageW(st->ctrl[i], BM_SETCHECK, *(bool*)st->fields[i].ptr ? BST_CHECKED : BST_UNCHECKED, 0);
        else
            SetWindowTextW(st->ctrl[i], field_text(st->fields[i]).c_str());
    }
}

// Reads every control back into the config. Anything unparsable keeps its previous
// value, and sanitize() clamps the rest into a range that cannot hurt.
void read_controls(State* st) {
    wchar_t buf[512];
    for (size_t i = 0; i < st->fields.size(); ++i) {
        const Field& f = st->fields[i];
        if (f.kind == Kind::Bool) {
            *(bool*)f.ptr = SendMessageW(st->ctrl[i], BM_GETCHECK, 0, 0) == BST_CHECKED;
            continue;
        }
        GetWindowTextW(st->ctrl[i], buf, ARRAYSIZE(buf));
        const std::wstring v = buf;
        wchar_t* end = nullptr;
        if (f.kind == Kind::Int) {
            const long n = wcstol(v.c_str(), &end, 10);
            if (end != v.c_str()) *(int*)f.ptr = (int)n;
        } else if (f.kind == Kind::Real) {
            const double d = wcstod(v.c_str(), &end);
            if (end != v.c_str()) *(double*)f.ptr = d;
        } else {
            auto& list = *(std::vector<std::wstring>*)f.ptr;
            list.clear();
            size_t start = 0;
            while (start <= v.size()) {
                size_t comma = v.find(L',', start);
                if (comma == std::wstring::npos) comma = v.size();
                const std::wstring item = v.substr(start, comma - start);
                const size_t a = item.find_first_not_of(L" \t");
                const size_t b = item.find_last_not_of(L" \t");
                if (a != std::wstring::npos) list.push_back(lower(item.substr(a, b - a + 1)));
                start = comma + 1;
            }
        }
    }
    st->cfg.sanitize();
}

void create_controls(HWND hwnd, HINSTANCE inst, State* st) {
    const int count = (int)st->fields.size();
    st->ctrl.assign(st->fields.size(), nullptr);

    for (int i = 0; i < count; ++i) {
        const Field& f = st->fields[i];
        const int y = 12 + i * kRowH;
        const bool wide = f.kind == Kind::Text;
        const HMENU id = (HMENU)(INT_PTR)(kIdFirst + i);

        if (f.kind == Kind::Bool) {
            st->ctrl[i] = CreateWindowExW(0, L"BUTTON", f.label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                          14, y + 2, kLabelW + kCtrlW, 20, hwnd, id, inst, nullptr);
        } else {
            CreateWindowExW(0, L"STATIC", f.label, WS_CHILD | WS_VISIBLE | SS_RIGHT, 8, y + 4, kLabelW, 18, hwnd,
                            nullptr, inst, nullptr);
            st->ctrl[i] = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, kLabelW + 14, y,
                                          wide ? kWidth - kLabelW - 44 : kCtrlW, 22, hwnd, id, inst, nullptr);
        }
        if (!wide)
            CreateWindowExW(0, L"STATIC", f.hint, WS_CHILD | WS_VISIBLE, kHintX, y + 4, kWidth - kHintX - 16, 18, hwnd,
                            nullptr, inst, nullptr);
    }

    const int by = 12 + count * kRowH + 16;
    CreateWindowExW(0, L"BUTTON", L"Save", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, kWidth - 216, by, 92,
                    26, hwnd, (HMENU)(INT_PTR)kIdSave, inst, nullptr);
    CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, kWidth - 116, by, 92, 26, hwnd,
                    (HMENU)(INT_PTR)kIdCancel, inst, nullptr);
    CreateWindowExW(0, L"BUTTON", L"Defaults", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 16, by, 92, 26, hwnd,
                    (HMENU)(INT_PTR)kIdDefaults, inst, nullptr);

    EnumChildWindows(
        hwnd,
        [](HWND child, LPARAM p) -> BOOL {
            SendMessageW(child, WM_SETFONT, (WPARAM)p, TRUE);
            return TRUE;
        },
        (LPARAM)st->font);
}

LRESULT CALLBACK settings_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    State* st = state_of(hwnd);
    if (st) {
        switch (msg) {
            case WM_COMMAND:
                switch (LOWORD(wp)) {
                    case kIdSave:
                        read_controls(st);
                        st->cfg.save();
                        st->saved = true;
                        DestroyWindow(hwnd);
                        return 0;
                    case kIdCancel:
                        DestroyWindow(hwnd);
                        return 0;
                    case kIdDefaults: {
                        const bool keep = st->cfg.enabled;
                        st->cfg = Config{};
                        st->cfg.enabled = keep;
                        fill_controls(st);
                        return 0;
                    }
                }
                return 0;

            case WM_CLOSE:
                DestroyWindow(hwnd);
                return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

bool show_settings(HINSTANCE inst, Config& cfg) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = settings_proc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"cpulytics_settings";
        if (!RegisterClassExW(&wc)) return false;
        registered = true;
    }

    State st;
    st.cfg = cfg;
    st.fields = fields_of(st.cfg);

    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    st.font = CreateFontIndirectW(&ncm.lfMessageFont);

    const int height = 12 + (int)st.fields.size() * kRowH + 16 + 26 + 20;
    RECT r{0, 0, kWidth, height};
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    AdjustWindowRect(&r, style, FALSE);
    HWND hwnd = CreateWindowExW(0, L"cpulytics_settings", L"cpulytics settings", style, CW_USEDEFAULT, CW_USEDEFAULT,
                                r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, nullptr);
    if (!hwnd) {
        DeleteObject(st.font);
        return false;
    }
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)&st);
    create_controls(hwnd, inst, &st);
    fill_controls(&st);
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);

    // Own message loop, but every message is dispatched, so the tray icon and the
    // sampling timer keep working while the window is open. The loop ends as soon
    // as the window is gone, which happens inside one of these dispatches.
    MSG msg;
    while (IsWindow(hwnd)) {
        const BOOL got = GetMessageW(&msg, nullptr, 0, 0);
        if (got == 0) {              // the tray menu asked the app to exit
            PostQuitMessage((int)msg.wParam);
            break;
        }
        if (got < 0) break;
        if (IsDialogMessageW(hwnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (IsWindow(hwnd)) DestroyWindow(hwnd);
    DeleteObject(st.font);
    if (st.saved) cfg = st.cfg;
    return st.saved;
}

}  // namespace cpulytics
