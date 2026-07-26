#pragma once

#include <windows.h>

#include <cstdint>
#include <cwctype>
#include <string>

namespace cpulytics {

inline std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)(n > 0 ? n : 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

inline std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

inline std::wstring lower(std::wstring s) {
    for (wchar_t& c : s) c = (wchar_t)towlower(c);
    return s;
}

// Monotonic milliseconds, immune to wall clock jumps and to the 49-day wrap.
inline uint64_t now_ms() { return GetTickCount64(); }

}  // namespace cpulytics
