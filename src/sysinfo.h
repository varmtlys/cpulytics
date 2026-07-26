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

// 0 when the process is gone or out of reach.
uint32_t priority_class(uint32_t pid);

// Applies step levels of demotion below orig_class. step 0 restores orig_class.
// Returns false when the process cannot or must not be touched.
bool apply_step(uint32_t pid, uint32_t orig_class, int step);

// Priority class this many steps below the original, for logs and menus.
uint32_t class_for_step(uint32_t orig_class, int step);
const wchar_t* class_name(uint32_t cls);

}  // namespace sys
}  // namespace cpulytics
