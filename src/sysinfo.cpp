#include "sysinfo.h"

#include <algorithm>

#include "util.h"

namespace cpulytics {
namespace sys {
namespace {

// Kernel side layout of SYSTEM_PROCESS_INFORMATION. winternl.h ships a truncated
// version of it, so the fields we need are declared here instead.
struct UnicodeStr {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR Buffer;
};

struct SysProcInfo {
    ULONG NextEntryOffset;
    ULONG NumberOfThreads;
    LARGE_INTEGER WorkingSetPrivateSize;
    ULONG HardFaultCount;
    ULONG NumberOfThreadsHighWatermark;
    ULONGLONG CycleTime;
    LARGE_INTEGER CreateTime;
    LARGE_INTEGER UserTime;
    LARGE_INTEGER KernelTime;
    UnicodeStr ImageName;
    LONG BasePriority;
    HANDLE UniqueProcessId;
    HANDLE InheritedFromUniqueProcessId;
    ULONG HandleCount;
    ULONG SessionId;
};

#ifdef _WIN64
static_assert(offsetof(SysProcInfo, CreateTime) == 32, "unexpected layout");
static_assert(offsetof(SysProcInfo, ImageName) == 56, "unexpected layout");
static_assert(offsetof(SysProcInfo, UniqueProcessId) == 80, "unexpected layout");
static_assert(offsetof(SysProcInfo, SessionId) == 100, "unexpected layout");
#endif

using NtQuerySystemInformation_t = LONG(NTAPI*)(ULONG, PVOID, ULONG, PULONG);
constexpr ULONG kSystemProcessInformation = 5;
constexpr LONG kStatusInfoLengthMismatch = (LONG)0xC0000004L;

NtQuerySystemInformation_t query() {
    static NtQuerySystemInformation_t fn = []() -> NtQuerySystemInformation_t {
        HMODULE m = GetModuleHandleW(L"ntdll.dll");  // always loaded, never freed here
        return m ? (NtQuerySystemInformation_t)(void*)GetProcAddress(m, "NtQuerySystemInformation") : nullptr;
    }();
    return fn;
}

// Processes that keep the session alive. Slowing any of these down is a way to
// hang or bluescreen the machine, so they are never touched at all.
const wchar_t* const kCritical[] = {
    L"system", L"registry", L"idle", L"memory compression", L"smss.exe", L"csrss.exe",
    L"wininit.exe", L"winlogon.exe", L"services.exe", L"lsass.exe", L"lsaiso.exe",
    L"dwm.exe", L"audiodg.exe", L"ntoskrnl.exe", L"fontdrvhost.exe", L"sihost.exe",
    L"secure system", L"system idle process",
};

bool is_critical(const std::wstring& lname) {
    for (const wchar_t* c : kCritical)
        if (lname == c) return true;
    return false;
}

struct Handle {
    HANDLE h = nullptr;
    explicit Handle(HANDLE x) : h(x) {}
    ~Handle() {
        if (h) CloseHandle(h);
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    explicit operator bool() const { return h != nullptr; }
};

// Ordered ladder. A demotion moves this many entries down, never past idle.
const DWORD kLadder[] = {
    REALTIME_PRIORITY_CLASS, HIGH_PRIORITY_CLASS, ABOVE_NORMAL_PRIORITY_CLASS,
    NORMAL_PRIORITY_CLASS,   BELOW_NORMAL_PRIORITY_CLASS, IDLE_PRIORITY_CLASS,
};

int ladder_index(uint32_t cls) {
    for (int i = 0; i < (int)(sizeof(kLadder) / sizeof(kLadder[0])); ++i)
        if (kLadder[i] == cls) return i;
    return -1;
}

}  // namespace

bool Sampler::sample(std::vector<ProcInfo>& out) {
    auto fn = query();
    if (!fn) return false;

    if (buf_.size() < 512 * 1024) buf_.resize(512 * 1024);
    ULONG need = 0;
    LONG st = 0;
    for (int attempt = 0; attempt < 6; ++attempt) {
        st = fn(kSystemProcessInformation, buf_.data(), (ULONG)buf_.size(), &need);
        if (st != kStatusInfoLengthMismatch) break;
        // The table grew between the two calls; ask for headroom, not the exact size.
        buf_.resize(std::max<size_t>(buf_.size() * 2, (size_t)need + 64 * 1024));
    }
    if (st < 0) return false;

    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    const uint64_t now_ft = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    const uint32_t self = GetCurrentProcessId();

    out.clear();
    size_t offset = 0;
    while (offset + sizeof(SysProcInfo) <= buf_.size()) {
        const auto* p = reinterpret_cast<const SysProcInfo*>(buf_.data() + offset);

        ProcInfo pi;
        pi.pid = (uint32_t)(uintptr_t)p->UniqueProcessId;
        pi.create_time = (uint64_t)p->CreateTime.QuadPart;
        pi.cpu_time = (uint64_t)p->KernelTime.QuadPart + (uint64_t)p->UserTime.QuadPart;
        pi.age_ms = now_ft > pi.create_time ? (now_ft - pi.create_time) / 10000 : 0;
        if (p->ImageName.Buffer && p->ImageName.Length)
            pi.name.assign(p->ImageName.Buffer, p->ImageName.Length / sizeof(wchar_t));
        else if (pi.pid == 0)
            pi.name = L"System Idle Process";
        else
            pi.name = L"System";

        const std::wstring lname = lower(pi.name);
        pi.critical = pi.pid == self || pi.pid <= 4 || is_critical(lname);
        // Session 0 is where services and the security stack live: not off limits,
        // but they only ever get the gentle treatment.
        pi.system = p->SessionId == 0;
        out.push_back(std::move(pi));

        if (p->NextEntryOffset == 0) break;
        offset += p->NextEntryOffset;
    }
    return !out.empty();
}

uint32_t foreground_pid() {
    HWND w = GetForegroundWindow();
    if (!w) return 0;
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    return pid;
}

uint32_t fullscreen_pid() {
    HWND w = GetForegroundWindow();
    if (!w || w == GetDesktopWindow() || w == GetShellWindow()) return 0;

    RECT wr;
    if (!GetWindowRect(w, &wr)) return 0;
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST), &mi)) return 0;
    // Exclusive and borderless fullscreen both end up covering exactly the monitor.
    if (wr.left > mi.rcMonitor.left || wr.top > mi.rcMonitor.top || wr.right < mi.rcMonitor.right ||
        wr.bottom < mi.rcMonitor.bottom)
        return 0;

    // The desktop background and the task bar also cover the monitor.
    wchar_t cls[64] = {};
    GetClassNameW(w, cls, ARRAYSIZE(cls));
    const std::wstring c = cls;
    if (c == L"Progman" || c == L"WorkerW" || c == L"Shell_TrayWnd" || c == L"Windows.UI.Core.CoreWindow") return 0;

    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    return pid;
}

namespace {

constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

std::wstring quoted_exe_path() {
    wchar_t exe[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    return L"\"" + std::wstring(exe, n) + L"\"";
}

}  // namespace

bool autostart_enabled(const wchar_t* name) {
    wchar_t value[MAX_PATH + 2] = {};
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, name, RRF_RT_REG_SZ, nullptr, value, &size) != ERROR_SUCCESS)
        return false;
    // An entry left behind by an older copy in another folder is not autostart
    // for this executable, and rewriting it is exactly what set_autostart does.
    return quoted_exe_path() == value;
}

bool set_autostart(bool on, const wchar_t* name) {
    if (on == autostart_enabled(name)) return true;  // nothing to write

    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS)
        return false;

    LSTATUS st;
    if (on) {
        const std::wstring path = quoted_exe_path();
        st = path.empty() ? ERROR_INVALID_DATA
                          : RegSetValueExW(key, name, 0, REG_SZ, (const BYTE*)path.c_str(),
                                           (DWORD)((path.size() + 1) * sizeof(wchar_t)));
    } else {
        st = RegDeleteValueW(key, name);
        if (st == ERROR_FILE_NOT_FOUND) st = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return st == ERROR_SUCCESS;
}

bool has_efficiency_cores() {
    static const bool hybrid = [] {
        ULONG size = 0;
        GetSystemCpuSetInformation(nullptr, 0, &size, GetCurrentProcess(), 0);
        if (!size) return false;
        std::vector<char> buf(size);
        auto* info = reinterpret_cast<SYSTEM_CPU_SET_INFORMATION*>(buf.data());
        if (!GetSystemCpuSetInformation(info, size, &size, GetCurrentProcess(), 0)) return false;

        // The list is a sequence of variable sized records; one efficiency class
        // for all of them means every core is the same kind.
        int first = -1;
        for (ULONG off = 0; off + sizeof(SYSTEM_CPU_SET_INFORMATION) <= size;) {
            const auto* e = reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(buf.data() + off);
            if (!e->Size) break;
            if (e->Type == CpuSetInformation) {
                const int klass = e->CpuSet.EfficiencyClass;
                if (first < 0) first = klass;
                else if (klass != first) return true;
            }
            off += e->Size;
        }
        return false;
    }();
    return hybrid;
}

bool is_elevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION info{};
    DWORD got = 0;
    const bool ok = GetTokenInformation(token, TokenElevation, &info, sizeof(info), &got) && info.TokenIsElevated;
    CloseHandle(token);
    return ok;
}

uint32_t priority_class(uint32_t pid) {
    Handle h(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (!h) return 0;
    DWORD c = GetPriorityClass(h.h);
    return c;
}

uint32_t class_for_step(uint32_t orig_class, int step) {
    const int idx = ladder_index(orig_class);
    if (idx < 0 || step <= 0) return orig_class;
    const int last = (int)(sizeof(kLadder) / sizeof(kLadder[0])) - 1;
    return kLadder[std::min(idx + step, last)];
}

bool set_eco_qos(uint32_t pid, bool on) {
    Handle h(OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid));
    if (!h) return false;
    PROCESS_POWER_THROTTLING_STATE s{};
    s.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    s.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    // A cleared bit in StateMask means "back to the default", not "forced fast".
    s.StateMask = on ? PROCESS_POWER_THROTTLING_EXECUTION_SPEED : 0;
    return SetProcessInformation(h.h, ProcessPowerThrottling, &s, sizeof(s)) != 0;
}

bool eco_qos(uint32_t pid) {
    Handle h(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (!h) return false;
    PROCESS_POWER_THROTTLING_STATE s{};
    s.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;  // the query needs it too
    if (!GetProcessInformation(h.h, ProcessPowerThrottling, &s, sizeof(s))) return false;
    return (s.ControlMask & s.StateMask & PROCESS_POWER_THROTTLING_EXECUTION_SPEED) != 0;
}

bool apply_step(uint32_t pid, uint32_t orig_class, int step, bool eco) {
    // A realtime process is doing something we are not qualified to slow down.
    if (orig_class == REALTIME_PRIORITY_CLASS) return false;
    const uint32_t target = class_for_step(orig_class, step);
    if (!target) return false;
    Handle h(OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (!h) return false;
    if (!SetPriorityClass(h.h, target)) return false;
    // Older windows has no EcoQoS; the priority change is what matters, so a
    // failure here is not one.
    set_eco_qos(pid, eco && step > 0);
    return true;
}

const wchar_t* class_name(uint32_t cls) {
    switch (cls) {
        case REALTIME_PRIORITY_CLASS: return L"realtime";
        case HIGH_PRIORITY_CLASS: return L"high";
        case ABOVE_NORMAL_PRIORITY_CLASS: return L"above normal";
        case NORMAL_PRIORITY_CLASS: return L"normal";
        case BELOW_NORMAL_PRIORITY_CLASS: return L"below normal";
        case IDLE_PRIORITY_CLASS: return L"idle";
        default: return L"unknown";
    }
}

}  // namespace sys
}  // namespace cpulytics
