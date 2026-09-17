#include "settings.h"

#include <commctrl.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "i18n.h"
#include "sysinfo.h"
#include "theme.h"
#include "util.h"

namespace cpulytics {
namespace {

// The window is built from this table instead of a resource script: one row per
// setting, so adding a setting is one line here and one line in Config.
enum class Kind { Int, Real, Bool, Text };

struct Field {
    Str label;
    Str hint;
    Kind kind;
    void* ptr;
    const wchar_t* range = nullptr;     // shown in the tooltip, matches Config::sanitize
    const wchar_t* fallback = nullptr;  // the default value, also for the tooltip
    bool available = true;              // greyed out when the machine cannot do it
    Str unavailable = S_COUNT;          // why, in place of the hint
};

std::vector<Field> fields_of(Config& c) {
    return {
        {S_L_ENABLED, S_H_ENABLED, Kind::Bool, &c.enabled, nullptr, nullptr},
        {S_L_AUTOSTART, S_H_AUTOSTART, Kind::Bool, &c.autostart, nullptr, nullptr},
        {S_L_INTERVAL, S_H_INTERVAL, Kind::Int, &c.sample_interval_ms, L"250 - 60000", L"2000"},
        {S_L_WINDOW, S_H_WINDOW, Kind::Int, &c.window_seconds, L"30 - 7200", L"600"},
        {S_L_MINHIST, S_H_MINHIST, Kind::Int, &c.min_history_seconds, L"5 - 7200", L"120"},
        {S_L_DEMOTE, S_H_DEMOTE, Kind::Real, &c.demote_percent, L"1 - 100", L"20"},
        {S_L_RESTORE, S_H_RESTORE, Kind::Real, &c.restore_percent, L"0 - 99.5", L"8"},
        {S_L_GRACE, S_H_GRACE, Kind::Int, &c.startup_grace_seconds, L"0 - 3600", L"60"},
        {S_L_COOLDOWN, S_H_COOLDOWN, Kind::Int, &c.action_cooldown_seconds, L"1 - 3600", L"60"},
        {S_L_CALM, S_H_CALM, Kind::Int, &c.restore_after_seconds, L"1 - 7200", L"120"},
        {S_L_STEPS, S_H_STEPS, Kind::Int, &c.max_steps, L"0 - 2", L"2"},
        {S_L_SYSSTEPS, S_H_SYSSTEPS, Kind::Int, &c.system_max_steps, L"0 - 2", L"1"},
        {S_L_FSSTEPS, S_H_FSSTEPS, Kind::Int, &c.fullscreen_max_steps, L"0 - 2", L"0"},
        // EcoQoS can only move work to efficient cores where there are any.
        {S_L_ECO, S_H_ECO, Kind::Bool, &c.eco_qos, nullptr, nullptr, sys::has_efficiency_cores(), S_H_ECO_NO_CORES},
        {S_L_FOREGROUND, S_H_FOREGROUND, Kind::Bool, &c.protect_foreground, nullptr, nullptr},
        {S_L_NOTIFY, S_H_NOTIFY, Kind::Bool, &c.notifications, nullptr, nullptr},
        {S_L_RESTORE_EXIT, S_H_RESTORE_EXIT, Kind::Bool, &c.restore_on_exit, nullptr, nullptr},
        {S_L_LOG, S_H_LOG, Kind::Bool, &c.log_enabled, nullptr, nullptr},
        {S_L_TRACKED, S_H_TRACKED, Kind::Int, &c.max_tracked, L"64 - 65536", L"2048"},
        {S_L_LOGSIZE, S_H_LOGSIZE, Kind::Int, &c.log_max_kb, L"16 - 65536", L"512"},
        {S_L_WHITELIST, S_H_WHITELIST, Kind::Text, &c.whitelist, nullptr, nullptr},
    };
}

constexpr int kRowH = 26;
constexpr int kLabelW = 170;
constexpr int kCtrlW = 90;
constexpr int kHintX = kLabelW + kCtrlW + 24;
constexpr int kWidth = 660;
constexpr int kIdFirst = 1000;
constexpr int kIdLang = 900;
constexpr int kIdTheme = 901;
constexpr int kIdSave = IDOK;        // Enter saves
constexpr int kIdCancel = IDCANCEL;  // Escape closes
constexpr int kIdDefaults = 3;

}  // namespace

// The about window: what this is, who wrote it, and under which licence.
void show_about(HINSTANCE inst, HWND owner) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
            switch (m) {
                case WM_ERASEBKGND: {
                    RECT rc;
                    GetClientRect(h, &rc);
                    FillRect((HDC)w, &rc, theme::window_brush());
                    return 1;
                }
                case WM_CTLCOLORSTATIC:
                case WM_CTLCOLORBTN:
                    SetTextColor((HDC)w, theme::palette().text);
                    SetBkMode((HDC)w, TRANSPARENT);
                    return (LRESULT)theme::window_brush();
                case WM_COMMAND:
                    if (LOWORD(w) == IDOK || LOWORD(w) == IDCANCEL) DestroyWindow(h);
                    return 0;
                case WM_CLOSE:
                    DestroyWindow(h);
                    return 0;
            }
            return DefWindowProcW(h, m, w, l);
        };
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"cpulytics_about";
        if (!RegisterClassExW(&wc)) return;
        registered = true;
    }

    // The screen the user is looking at: the one holding the window that opened
    // this, or the one under the mouse, which is where the tray was just clicked.
    POINT cursor{};
    GetCursorPos(&cursor);
    HMONITOR mon = owner ? MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST)
                         : MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi)) return;

    const int dpi = theme::dpi_of(owner);
    const auto S = [dpi](int v) { return MulDiv(v, dpi, 96); };
    const int w = 480, h = 214;  // wide enough for the longest translation
    const DWORD style = WS_POPUPWINDOW | WS_CAPTION;
    RECT r{0, 0, S(w), S(h)};
    AdjustWindowRect(&r, style, FALSE);
    const int width = r.right - r.left, height = r.bottom - r.top;
    // A popup window ignores CW_USEDEFAULT and would land in the top left corner,
    // so it is centred on the work area by hand.
    const int x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - width) / 2;
    const int y = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - height) / 2;
    HWND hwnd = CreateWindowExW(rtl() ? WS_EX_LAYOUTRTL : 0, L"cpulytics_about", tr(S_ABOUT), style, x, y, width,
                                height, owner, nullptr, inst, nullptr);
    if (!hwnd) return;
    theme::apply_window(hwnd);

    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    LOGFONTW lf = ncm.lfMessageFont;
    lf.lfHeight = MulDiv(lf.lfHeight, dpi, 96);
    HFONT font = CreateFontIndirectW(&lf);

    // The same icon the tray and explorer use, at the size this dpi asks for.
    const int icon_px = S(48);
    HICON icon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, icon_px, icon_px, LR_DEFAULTCOLOR);
    HWND pic = CreateWindowExW(0, L"STATIC", nullptr, WS_CHILD | WS_VISIBLE | SS_ICON | SS_REALSIZEIMAGE, S(20), S(20),
                               icon_px, icon_px, hwnd, nullptr, inst, nullptr);
    if (icon) SendMessageW(pic, STM_SETICON, (WPARAM)icon, 0);

    const int text_x = S(88);
    const wchar_t* lines[] = {L"cpulytics " CPULYTICS_VERSION_W, tr(S_ABOUT_TAGLINE),
                              L"© 2026 Ildar Latypov <varmtlys@gmail.com>", tr(S_ABOUT_LICENSE)};
    for (int i = 0; i < 4; ++i) {
        // The licence line is the long one: it gets room for two lines, and the
        // static wraps it on its own where a translation needs them.
        const int line_h = i == 3 ? S(38) : S(20);
        CreateWindowExW(0, L"STATIC", lines[i], WS_CHILD | WS_VISIBLE, text_x, S(20 + i * 22),
                        S(w) - text_x - S(16), line_h, hwnd, nullptr, inst, nullptr);
    }

    CreateWindowExW(0, L"BUTTON", tr(S_CLOSE), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, S(w - 120),
                    S(h - 54), S(100), S(30), hwnd, (HMENU)(INT_PTR)IDOK, inst, nullptr);

    EnumChildWindows(
        hwnd,
        [](HWND child, LPARAM p) -> BOOL {
            SendMessageW(child, WM_SETFONT, (WPARAM)p, TRUE);
            theme::apply_control(child, false);
            return TRUE;
        },
        (LPARAM)font);
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);

    MSG msg;
    while (IsWindow(hwnd)) {
        const BOOL got = GetMessageW(&msg, nullptr, 0, 0);
        if (got == 0) {
            PostQuitMessage((int)msg.wParam);
            break;
        }
        if (got < 0) break;
        if (IsDialogMessageW(hwnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (IsWindow(hwnd)) DestroyWindow(hwnd);
    if (icon) DestroyIcon(icon);
    if (font) DeleteObject(font);
}

namespace {

std::wstring field_text(const Field& f) {
    switch (f.kind) {
        case Kind::Int: return std::to_wstring(*(int*)f.ptr);
        case Kind::Real: {
            // Rounded to tenths as a whole, so 7.96 reads 8.0 and not 7.10.
            const int tenths = (int)(*(double*)f.ptr * 10.0 + 0.5);
            return std::to_wstring(tenths / 10) + L"." + std::to_wstring(tenths % 10);
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
    std::vector<HWND> hints;         // painted in the muted colour
    std::vector<std::wstring> tips;  // the tooltip control keeps pointers into these
    HWND tooltip = nullptr;
    bool saved = false;
    bool relaunch = false;  // language, theme or dpi changed: build the window again
    HFONT font = nullptr;
    int dpi = 96;

    int s(int v) const { return MulDiv(v, dpi, 96); }
    bool is_hint(HWND h) const {
        for (HWND x : hints)
            if (x == h) return true;
        return false;
    }
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
            *(bool*)f.ptr = f.available && SendMessageW(st->ctrl[i], BM_GETCHECK, 0, 0) == BST_CHECKED;
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

void add_tip(State* st, HWND ctrl, HWND owner, const std::wstring& text) {
    if (!st->tooltip || text.empty()) return;
    st->tips.push_back(text);
    TOOLINFOW ti{};
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = owner;
    ti.uId = (UINT_PTR)ctrl;
    ti.lpszText = (LPWSTR)st->tips.back().c_str();
    SendMessageW(st->tooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);
}

void create_controls(HWND hwnd, HINSTANCE inst, State* st) {
    const int count = (int)st->fields.size();
    st->ctrl.assign(st->fields.size(), nullptr);
    st->hints.clear();
    st->tips.clear();
    st->tips.reserve(st->fields.size() + 1);
    const auto S = [st](int v) { return st->s(v); };

    st->tooltip = CreateWindowExW(0, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, 0, 0, 0, 0,
                                  hwnd, nullptr, inst, nullptr);
    SendMessageW(st->tooltip, TTM_SETMAXTIPWIDTH, 0, S(420));  // also enables the line break
    SendMessageW(st->tooltip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 20000);

    // Language and theme on the first two rows. Both change how the window itself
    // looks, so picking one rebuilds it rather than trying to repaint in place.
    const struct {
        Str label;
        int id;
        const wchar_t* const* codes;
        const wchar_t* (*name)(size_t);
        const std::wstring* value;
    } pickers[] = {
        {S_LANGUAGE, kIdLang, language_codes(), language_name, &st->cfg.language},
        {S_THEME, kIdTheme, theme::modes(), theme::mode_name, &st->cfg.theme},
    };
    for (int row = 0; row < 2; ++row) {
        const auto& p = pickers[row];
        const int y = 12 + row * kRowH;
        CreateWindowExW(0, L"STATIC", tr(p.label), WS_CHILD | WS_VISIBLE | SS_RIGHT, S(8), S(y + 5), S(kLabelW), S(18),
                        hwnd, nullptr, inst, nullptr);
        HWND combo =
            CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                            S(kLabelW + 14), S(y), S(200), S(280), hwnd, (HMENU)(INT_PTR)p.id, inst, nullptr);
        theme::apply_control(combo, true);
        int selected = 0;
        for (int i = 0; p.codes[i]; ++i) {
            SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)p.name((size_t)i));
            if (*p.value == p.codes[i]) selected = i;
        }
        SendMessageW(combo, CB_SETCURSEL, selected, 0);
    }

    for (int i = 0; i < count; ++i) {
        const Field& f = st->fields[i];
        const int y = 12 + (i + 2) * kRowH + 10;
        const bool wide = f.kind == Kind::Text;
        const HMENU id = (HMENU)(INT_PTR)(kIdFirst + i);

        if (f.kind == Kind::Bool) {
            st->ctrl[i] = CreateWindowExW(0, L"BUTTON", tr(f.label),
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, S(14), S(y + 3),
                                          S(kLabelW + kCtrlW), S(20), hwnd, id, inst, nullptr);
        } else {
            CreateWindowExW(0, L"STATIC", tr(f.label), WS_CHILD | WS_VISIBLE | SS_RIGHT, S(8), S(y + 5), S(kLabelW),
                            S(18), hwnd, nullptr, inst, nullptr);
            st->ctrl[i] = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, S(kLabelW + 14), S(y),
                                          S(wide ? kWidth - kLabelW - 44 : kCtrlW), S(23), hwnd, id, inst, nullptr);
        }
        theme::apply_control(st->ctrl[i], f.kind != Kind::Bool);
        if (!f.available) EnableWindow(st->ctrl[i], FALSE);
        if (!wide) {
            const Str hint_id = f.available ? f.hint : f.unavailable;
            HWND hint = CreateWindowExW(0, L"STATIC", tr(hint_id), WS_CHILD | WS_VISIBLE, S(kHintX), S(y + 5),
                                        S(kWidth - kHintX - 16), S(18), hwnd, nullptr, inst, nullptr);
            st->hints.push_back(hint);
        }

        // Hovering a control repeats the hint and adds the range and the default,
        // which the single line next to it has no room for.
        std::wstring tip = tr(f.hint);
        if (f.range)
            tip += std::wstring(L"\n") + tr(S_RANGE) + L": " + f.range + L"    " + tr(S_DEFAULT) + L": " + f.fallback;
        add_tip(st, st->ctrl[i], hwnd, tip);
    }

    const int by = 12 + (count + 2) * kRowH + 10 + 18;
    const struct {
        const wchar_t* text;
        int x;
        int id;
        DWORD extra;
    } buttons[] = {
        {tr(S_SAVE), kWidth - 240, kIdSave, BS_DEFPUSHBUTTON},
        {tr(S_CANCEL), kWidth - 124, kIdCancel, 0},
        {tr(S_DEFAULTS), 16, kIdDefaults, 0},
    };
    for (const auto& b : buttons) {
        HWND w = CreateWindowExW(0, L"BUTTON", b.text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | b.extra, S(b.x), S(by),
                                 S(104), S(30), hwnd, (HMENU)(INT_PTR)b.id, inst, nullptr);
        theme::apply_control(w, false);
    }

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
                if ((LOWORD(wp) == kIdLang || LOWORD(wp) == kIdTheme) && HIWORD(wp) == CBN_SELCHANGE) {
                    const int sel = (int)SendMessageW((HWND)lp, CB_GETCURSEL, 0, 0);
                    if (sel >= 0) {
                        read_controls(st);  // keep whatever the user typed so far
                        if (LOWORD(wp) == kIdLang) {
                            st->cfg.language = language_codes()[sel];
                            set_language(st->cfg.language);
                        } else {
                            st->cfg.theme = theme::modes()[sel];
                            theme::set_mode(st->cfg.theme);
                        }
                        st->relaunch = true;
                        DestroyWindow(hwnd);
                    }
                    return 0;
                }
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
                        const std::wstring lang = st->cfg.language, ui_theme = st->cfg.theme;
                        st->cfg = Config{};
                        st->cfg.enabled = keep;
                        st->cfg.language = lang;
                        st->cfg.theme = ui_theme;
                        fill_controls(st);
                        return 0;
                    }
                }
                return 0;

            case WM_ERASEBKGND: {
                RECT rc;
                GetClientRect(hwnd, &rc);
                FillRect((HDC)wp, &rc, theme::window_brush());
                return 1;
            }

            // Labels and checkbox captions sit on the window background, hints get
            // the muted colour, inputs the surface one.
            case WM_CTLCOLORSTATIC:
            case WM_CTLCOLORBTN: {
                const auto& p = theme::palette();
                SetTextColor((HDC)wp, st->is_hint((HWND)lp) ? p.hint : p.text);
                SetBkMode((HDC)wp, TRANSPARENT);
                return (LRESULT)theme::window_brush();
            }
            case WM_CTLCOLOREDIT:
            case WM_CTLCOLORLISTBOX: {
                const auto& p = theme::palette();
                SetTextColor((HDC)wp, p.text);
                SetBkColor((HDC)wp, p.surface);
                return (LRESULT)theme::surface_brush();
            }

            // The user switched light and dark, or moved the window to a screen
            // with another scaling: both are easiest to follow by rebuilding.
            case WM_SETTINGCHANGE:
                if (lp && lstrcmpiW((const wchar_t*)lp, L"ImmersiveColorSet") == 0) {
                    st->relaunch = true;
                    DestroyWindow(hwnd);
                }
                return 0;
            case WM_DPICHANGED:
                st->relaunch = true;
                DestroyWindow(hwnd);
                return 0;

            case WM_CLOSE:
                DestroyWindow(hwnd);
                return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// One window: created, pumped, destroyed. Returns false when the caller has to
// build it again because the language changed.
bool run_window(HINSTANCE inst, State& st, bool& quit) {
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    // Arabic mirrors the whole layout, the way the shell does on an Arabic system.
    const DWORD ex = rtl() ? (WS_EX_LAYOUTRTL | WS_EX_RTLREADING) : 0;
    HWND hwnd = CreateWindowExW(ex, L"cpulytics_settings", tr(S_SETTINGS_TITLE), style, CW_USEDEFAULT, CW_USEDEFAULT,
                                100, 100, nullptr, nullptr, inst, nullptr);
    if (!hwnd) return true;

    // The size is known only once the window has a monitor, and with it a scaling.
    st.dpi = theme::dpi_of(hwnd);
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    LOGFONTW lf = ncm.lfMessageFont;
    lf.lfHeight = MulDiv(lf.lfHeight, st.dpi, 96);
    st.font = CreateFontIndirectW(&lf);

    const int height = 12 + ((int)st.fields.size() + 2) * kRowH + 10 + 18 + 30 + 22;
    RECT r{0, 0, st.s(kWidth), st.s(height)};
    AdjustWindowRect(&r, style, FALSE);
    SetWindowPos(hwnd, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER);
    theme::apply_window(hwnd);

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
        if (got == 0) {  // the tray menu asked the app to exit
            PostQuitMessage((int)msg.wParam);
            quit = true;
            break;
        }
        if (got < 0) break;
        if (IsDialogMessageW(hwnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (IsWindow(hwnd)) DestroyWindow(hwnd);
    DeleteObject(st.font);
    st.font = nullptr;
    return !st.relaunch;
}

}  // namespace

bool show_settings(HINSTANCE inst, Config& cfg) {
    static bool registered = false;
    if (!registered) {
        INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_WIN95_CLASSES};
        InitCommonControlsEx(&icc);
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

    bool quit = false;
    while (!quit) {
        st.relaunch = false;
        if (run_window(inst, st, quit)) break;
    }
    if (st.saved) cfg = st.cfg;
    // A cancelled pick must not stick.
    set_language(cfg.language);
    theme::set_mode(cfg.theme);
    return st.saved;
}

}  // namespace cpulytics
