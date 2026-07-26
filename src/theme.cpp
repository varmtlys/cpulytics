#include "theme.h"

#include <dwmapi.h>
#include <uxtheme.h>

namespace cpulytics {
namespace theme {
namespace {

// Undocumented uxtheme entry points, by ordinal. They are how every dark mode
// win32 app gets dark menus; if they are missing the app simply stays light.
enum class AppMode { Default = 0, AllowDark = 1, ForceDark = 2, ForceLight = 3 };
using SetPreferredAppMode_t = AppMode(WINAPI*)(AppMode);
using FlushMenuThemes_t = void(WINAPI*)();
using AllowDarkModeForWindow_t = bool(WINAPI*)(HWND, bool);

HMODULE uxtheme() {
    static HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    return ux;  // kept loaded on purpose, the dark controls need it for good
}

// Without this the DarkMode_* themes below are ignored for the window children.
void allow_dark(HWND hwnd) {
    static auto fn = []() -> AllowDarkModeForWindow_t {
        HMODULE ux = uxtheme();
        return ux ? (AllowDarkModeForWindow_t)(void*)GetProcAddress(ux, MAKEINTRESOURCEA(133)) : nullptr;
    }();
    if (fn) fn(hwnd, true);
}

constexpr DWORD kDwmUseDarkMode = 20;      // DWMWA_USE_IMMERSIVE_DARK_MODE
constexpr DWORD kDwmCornerPreference = 33; // DWMWA_WINDOW_CORNER_PREFERENCE
constexpr DWORD kCornerRound = 2;          // DWMWCP_ROUND

const Palette kDark{RGB(32, 32, 32), RGB(45, 45, 45), RGB(240, 240, 240), RGB(160, 160, 160), RGB(60, 60, 60)};
const Palette kLight{RGB(243, 243, 243), RGB(255, 255, 255), RGB(26, 26, 26), RGB(96, 96, 96), RGB(210, 210, 210)};

HBRUSH g_window = nullptr;
HBRUSH g_surface = nullptr;
bool g_dark_cached = false;

void refresh_brushes(bool is_dark) {
    if (g_window && g_dark_cached == is_dark) return;
    if (g_window) DeleteObject(g_window);
    if (g_surface) DeleteObject(g_surface);
    const Palette& p = is_dark ? kDark : kLight;
    g_window = CreateSolidBrush(p.window);
    g_surface = CreateSolidBrush(p.surface);
    g_dark_cached = is_dark;
}

}  // namespace

bool dark() {
    DWORD value = 1;  // the key is missing on older windows, which is light
    DWORD size = sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return value == 0;
}

const Palette& palette() { return dark() ? kDark : kLight; }

HBRUSH window_brush() {
    refresh_brushes(dark());
    return g_window;
}

HBRUSH surface_brush() {
    refresh_brushes(dark());
    return g_surface;
}

void init_process() {
    HMODULE ux = uxtheme();
    if (!ux) return;
    auto set_mode = (SetPreferredAppMode_t)(void*)GetProcAddress(ux, MAKEINTRESOURCEA(135));
    auto flush = (FlushMenuThemes_t)(void*)GetProcAddress(ux, MAKEINTRESOURCEA(136));
    if (set_mode) set_mode(AppMode::AllowDark);  // follow the system, do not force
    if (flush) flush();
}

void apply_window(HWND hwnd) {
    allow_dark(hwnd);
    const BOOL on = dark() ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, kDwmUseDarkMode, &on, sizeof(on));
    const DWORD corner = kCornerRound;  // ignored before windows 11
    DwmSetWindowAttribute(hwnd, kDwmCornerPreference, &corner, sizeof(corner));
}

void apply_control(HWND ctrl, bool is_input) {
    allow_dark(ctrl);
    if (!dark()) {
        SetWindowTheme(ctrl, nullptr, nullptr);
        return;
    }
    // CFD is what the shell uses for dark edit and combo boxes, Explorer for the
    // rest: checkbox glyphs, buttons, scrollbars.
    SetWindowTheme(ctrl, is_input ? L"DarkMode_CFD" : L"DarkMode_Explorer", nullptr);
}

int dpi_of(HWND hwnd) {
    using GetDpiForWindow_t = UINT(WINAPI*)(HWND);
    static auto fn = []() -> GetDpiForWindow_t {
        HMODULE u = GetModuleHandleW(L"user32.dll");
        return u ? (GetDpiForWindow_t)(void*)GetProcAddress(u, "GetDpiForWindow") : nullptr;
    }();
    if (fn && hwnd) {
        const UINT dpi = fn(hwnd);
        if (dpi) return (int)dpi;
    }
    HDC dc = GetDC(hwnd);
    const int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(hwnd, dc);
    return dpi ? dpi : 96;
}

}  // namespace theme
}  // namespace cpulytics
