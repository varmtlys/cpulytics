#pragma once

#include <windows.h>

#include <vector>

#include "engine.h"

namespace cpulytics {
namespace sys {

// Reads the whole process table in one syscall (NtQuerySystemInformation), which is
// what task manager itself does: no per process handle, nothing to leak, no access
// denied on protected processes.
class Sampler {
public:
    bool sample(std::vector<ProcInfo>& out);

private:
    std::vector<char> buf_;
};

uint32_t foreground_pid();

// Pid of the app whose window covers a whole monitor (a fullscreen game or player),
// 0 when the foreground window is an ordinary one or belongs to the shell.
uint32_t fullscreen_pid();

// Autostart through the per user Run key: no admin, no scheduled task, and the
// same entry the task manager startup tab shows. The name is the registry value,
// it is a parameter so the tests can use one of their own.
bool set_autostart(bool on, const wchar_t* name = L"cpulytics");
bool autostart_enabled(const wchar_t* name = L"cpulytics");

// True when the app runs with an elevated token. Without it only processes of the
// same user and integrity level can be re-prioritised.
bool is_elevated();

// 0 when the process is gone or out of reach.
uint32_t priority_class(uint32_t pid);

// Applies step levels of demotion below orig_class. step 0 restores orig_class.
// With eco, a demoted process is also marked as low power (EcoQoS): the scheduler
// stops boosting for it and prefers efficient cores where the cpu has them.
// Returns false when the process cannot or must not be touched.
bool apply_step(uint32_t pid, uint32_t orig_class, int step, bool eco = false);

// EcoQoS on its own. False when the process is out of reach or windows is too old.
bool set_eco_qos(uint32_t pid, bool on);
bool eco_qos(uint32_t pid);

// Priority class this many steps below the original, for logs and menus.
uint32_t class_for_step(uint32_t orig_class, int step);
const wchar_t* class_name(uint32_t cls);

}  // namespace sys
}  // namespace cpulytics
