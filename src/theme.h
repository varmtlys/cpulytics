#pragma once

#include <windows.h>

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

// Reads AppsUseLightTheme every time it is called: the setting can change while
// the app runs, and the settings window rebuilds itself when it does.
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
