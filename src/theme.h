#pragma once

#include <windows.h>

#include <string>

namespace cpulytics {
namespace theme {

// Windows 11 flavoured palette, one struct per mode.
struct Palette {
    COLORREF window;
    COLORREF surface;  // edit and combo background
    COLORREF text;
    COLORREF hint;
    COLORREF border;
};

// "system" follows AppsUseLightTheme, "dark" and "light" force one. Anything else
// is taken as "system".
void set_mode(const std::wstring& code);
const wchar_t* const* modes();       // "system", "dark", "light", nullptr
const wchar_t* mode_name(size_t i);  // translated, for the picker

// Follows the mode above, and re-reads the system setting every time when that is
// what the mode says: it can change while the app runs.
bool dark();
const Palette& palette();

// Lets the process use the dark controls and the dark popup menu. Called once.
void init_process();

// Dark title bar and rounded corners on the given top level window.
void apply_window(HWND hwnd);

// Dark scrollbars, borders and glyphs for one child control.
void apply_control(HWND ctrl, bool is_input);

// Cached brushes, valid for the lifetime of the process.
HBRUSH window_brush();
HBRUSH surface_brush();

// Dpi of a window, 96 when the system is too old to be asked.
int dpi_of(HWND hwnd);

}  // namespace theme
}  // namespace cpulytics
