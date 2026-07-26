# cpulytics

A tray application for Windows that watches what actually eats the CPU over a
sliding window and quietly lowers the priority of whatever is hogging it, then
gives the priority back once the process calms down.

It was written for a laptop that stutters when some background process decides to
spin for ten minutes. Instead of killing anything, cpulytics moves the offender
below the work you are actually doing.

## What it does

- Samples the whole process table every two seconds with one syscall
  (`NtQuerySystemInformation`), so it costs nothing and needs no handle per process.
- Averages CPU usage over a **sliding window** (10 minutes by default), as a
  percentage of the whole machine (all cores = 100%).
- Ignores **startup bursts**: a process is left alone for its first minute, and the
  window has to be filled with at least two minutes of data before anything happens.
- Demotes in **steps**: normal -> below normal -> idle, one step at a time, with a
  cooldown between steps.
- **System processes** (session 0: services, the security stack) get one gentle
  step at most. Kernel and session critical processes (`csrss`, `wininit`,
  `services`, `lsass`, `dwm`, `audiodg`, ...) are never touched at all, and neither
  is anything running at realtime priority.
- **Restores** the original priority class - the one the process had before
  cpulytics ever saw it - once the window average drops below the calm threshold and
  stays there. The process you are working in right now (foreground window) is
  restored immediately.
- Shows a balloon on every change, keeps a size capped log, and puts everything back
  when it exits.

## Build

Needs MSVC (`cl` on PATH, from a Developer PowerShell) or MinGW `g++`. The script
picks whichever it finds.

```powershell
.\scripts\build.ps1          # build\cpulytics.exe
.\scripts\build.ps1 -Test    # build, then run both test suites
.\scripts\build.ps1 -Run     # build and start it
```

The version is taken from `git describe`, there is no version file to bump.

## Settings

`%APPDATA%\cpulytics\config.ini`, written with comments on first run. Edit it and
pick "Reload settings" in the tray menu.

| Key | Default | Meaning |
|---|---|---|
| `enabled` | true | master switch, also in the tray menu |
| `sample_interval_ms` | 2000 | how often the process table is read |
| `window_seconds` | 600 | length of the sliding window |
| `min_history_seconds` | 120 | no decisions before the window holds this much data |
| `demote_percent` | 20 | window average that triggers a demotion |
| `restore_percent` | 8 | window average that earns a step back |
| `startup_grace_seconds` | 60 | how long a fresh process is immune |
| `action_cooldown_seconds` | 60 | minimum gap between two changes of one process |
| `restore_after_seconds` | 120 | how long a process must stay calm before it is restored |
| `max_steps` | 2 | 0 = off, 1 = below normal, 2 = down to idle |
| `system_max_steps` | 1 | the same cap for session 0 processes |
| `protect_foreground` | true | never demote the window you are using |
| `notifications` | true | balloon on every change |
| `restore_on_exit` | true | put everything back on shutdown |
| `log_enabled` | true | write `cpulytics.log` next to the config |
| `max_tracked` | 2048 | upper bound on the history map |
| `log_max_kb` | 512 | log is truncated past this size |
| `whitelist` | empty | comma separated executables that are never touched |

## Tests

`tests/test_engine.cpp` drives the decision logic on a synthetic clock: startup
bursts, hysteresis, the system process cap, recycled pids, foreground protection.

`tests/test_integration.cpp` is the real thing: it starts a copy of itself that
burns a core, runs the real sampler and engine over the real process table, and
checks that the child is demoted to below normal and restored to normal once it
goes quiet. It also checks that an exited process leaves nothing behind in memory,
and that the embedded icon decodes.

## Releases

Versions are git tags: `vMAJOR.MINOR` for features, `vMAJOR.MINOR.PATCH` for fixes.
Pushing a tag runs `.github/workflows/release.yml`, which builds, tests, packages
`cpulytics-<tag>-windows-x64.zip` and publishes a release with `RELEASE_NOTES.md`
as its body.

## Notes

- The tray icon is a base64 `.ico` embedded in `src/icon.h` (16/24/32 px), so the
  binary has no external assets.
- Only one instance can run at a time; two of them would fight over the same
  priorities.
- Processes owned by another user or elevated beyond our rights simply fail to
  change once and are then left alone.
