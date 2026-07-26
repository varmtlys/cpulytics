#pragma once

#include <string>

namespace cpulytics {

// Every string the user can see. The table lives in i18n.cpp, one row per id.
enum Str {
    S_SETTINGS_TITLE,
    S_LANGUAGE,
    S_SAVE,
    S_CANCEL,
    S_DEFAULTS,
    S_RANGE,
    S_DEFAULT,
    // tray
    S_MENU_MANAGING,
    S_MENU_RESTORE_ALL,
    S_MENU_SETTINGS,
    S_MENU_RELOAD,
    S_MENU_ELEVATE,
    S_MENU_LOG,
    S_MENU_EXIT,
    S_ADMINISTRATOR,
    S_PAUSED,
    S_TOP,
    S_TAG_LOWERED,
    S_TAG_IDLE,
    S_TAG_FULLSCREEN,
    S_BALLOON_LOWERED,
    S_BALLOON_RESTORED,
    S_BALLOON_MORE,
    // settings, label and hint per row
    S_L_ENABLED, S_H_ENABLED,
    S_L_INTERVAL, S_H_INTERVAL,
    S_L_WINDOW, S_H_WINDOW,
    S_L_MINHIST, S_H_MINHIST,
    S_L_DEMOTE, S_H_DEMOTE,
    S_L_RESTORE, S_H_RESTORE,
    S_L_GRACE, S_H_GRACE,
    S_L_COOLDOWN, S_H_COOLDOWN,
    S_L_CALM, S_H_CALM,
    S_L_STEPS, S_H_STEPS,
    S_L_SYSSTEPS, S_H_SYSSTEPS,
    S_L_FSSTEPS, S_H_FSSTEPS,
    S_L_FOREGROUND, S_H_FOREGROUND,
    S_L_NOTIFY, S_H_NOTIFY,
    S_L_RESTORE_EXIT, S_H_RESTORE_EXIT,
    S_L_LOG, S_H_LOG,
    S_L_TRACKED, S_H_TRACKED,
    S_L_LOGSIZE, S_H_LOGSIZE,
    S_L_WHITELIST, S_H_WHITELIST,
    S_L_ECO, S_H_ECO,
    S_COUNT
};

// Codes: "auto", "en", "es", "ru", "zh", "ja", "ko", "ar". Anything else is English.
// "auto" follows the Windows UI language.
void set_language(const std::wstring& code);
const wchar_t* tr(Str id);
bool rtl();  // true for Arabic, the settings window mirrors itself

// The eight codes above, for the language picker.
const wchar_t* const* language_codes();  // nullptr terminated
const wchar_t* language_name(size_t index);

}  // namespace cpulytics
