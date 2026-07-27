#pragma once

#include <windows.h>

#include "config.h"

namespace cpulytics {

// Shows the settings window and pumps messages until it closes, so the sampling
// timer keeps running while it is open. Returns true when the user saved, in
// which case cfg holds the new (already clamped) values and the INI is written.
bool show_settings(HINSTANCE inst, Config& cfg);

// The about window: name, version, author and licence. Opened from the tray menu,
// owner may be nullptr. Returns when the user closes it.
void show_about(HINSTANCE inst, HWND owner);

}  // namespace cpulytics
