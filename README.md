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
- **Fullscreen apps** (games, players) are immune by default. A process that is
  ever seen owning a window covering a whole monitor keeps that immunity for its
  whole life, so alt-tab does not cost it. `fullscreen_max_steps` lifts the immunity
  and says how far such an app may be demoted.
- **System processes** (session 0: services, the security stack) get one gentle
  step at most. Kernel and session critical processes (`csrss`, `wininit`,
  `services`, `lsass`, `dwm`, `audiodg`, ...) are never touched at all, and neither
  is anything running at realtime priority.
- Can additionally mark a demoted process as **low power** (EcoQoS, `eco_qos`):
  Windows stops boosting for it and puts it on efficient cores where the cpu has
  them. Off by default, and cleared together with the priority on restore.
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
.\scripts\build.ps1                # build\x64\cpulytics.exe
.\scripts\build.ps1 -Arch x86      # build\x86\cpulytics.exe
.\scripts\build.ps1 -Arch arm64    # build\arm64\cpulytics.exe (cross build)
.\scripts\build.ps1 -Test          # build, then run both test suites
.\scripts\build.ps1 -Run           # build and start it
```

x64, x86 and arm64 are built and released. With MSVC the architecture comes from
the developer prompt (`vcvars64`, `vcvarsall x86`, `vcvarsall x64_arm64`) and
`-Arch` has to agree with it; with g++ it is `-m64` / `-m32`, so the 32 bit build
needs a multilib toolchain. An arm64 build made on an x64 machine is a cross build:
its tests are compiled but not run there.

The version is taken from `git describe`, there is no version file to bump.

## Settings

"Settings..." in the tray menu opens a window with every option, a one line hint
next to each of them and a tooltip with the allowed range and the default. The
interface speaks English, Spanish, Russian, Chinese, Japanese, Korean and Arabic
(mirrored layout); "Auto" follows the Windows display language.

The window and the tray menu follow the Windows light and dark setting, including
the title bar and the rounded Windows 11 corners, and switch over as soon as the
system does. The application manifest asks for common controls 6 and per monitor
v2 dpi awareness, so the window is themed and sharp on a scaled display.

The same values live in `%APPDATA%\cpulytics\config.ini`, written with comments on
first run - edit it by hand and pick "Reload settings file" in the tray menu.

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
| `fullscreen_max_steps` | 0 | the same cap for fullscreen apps, 0 leaves games alone |
| `eco_qos` | false | also mark demoted processes as low power (EcoQoS) |
| `protect_foreground` | true | never demote the window you are using |
| `notifications` | true | balloon on every change |
| `restore_on_exit` | true | put everything back on shutdown |
| `log_enabled` | true | write `cpulytics.log` next to the config |
| `max_tracked` | 2048 | upper bound on the history map |
| `log_max_kb` | 512 | log is truncated past this size |
| `whitelist` | empty | comma separated executables that are never touched |
| `language` | auto | ui language: auto, en, es, ru, zh, ja, ko, ar |

## Tests

`tests/test_engine.cpp` drives the decision logic on a synthetic clock: startup
bursts, hysteresis, the system and fullscreen caps, recycled pids, foreground
protection.

`tests/test_integration.cpp` is the real thing: it starts a copy of itself that
burns a core, runs the real sampler and engine over the real process table, and
checks that the child is demoted to below normal and restored to normal once it
goes quiet. It also checks that an exited process leaves nothing behind in memory,
that the embedded icon decodes, and that every interface string exists in every
language.

## Releases

Versions are git tags: `vMAJOR.MINOR` for features, `vMAJOR.MINOR.PATCH` for fixes.
Pushing a tag runs `.github/workflows/release.yml`, which builds and tests all
three architectures, packages `cpulytics-<tag>-windows-x64.zip`,
`cpulytics-<tag>-windows-x86.zip` and `cpulytics-<tag>-windows-arm64.zip`, and
publishes a release with `RELEASE_NOTES.md` as its body.

## What it can touch, and administrator rights

Lowering a priority is `SetPriorityClass` on a handle opened with
`PROCESS_SET_INFORMATION`. Windows grants that through the process DACL, which for
your own processes at the same integrity level lets you do it without any special
right - that is the same thing task manager does when you set a priority by hand,
and it needs no elevation. So without administrator cpulytics can already manage
everything you started yourself, which is where a runaway background task usually
lives.

It cannot touch what it has no right to: processes of another user, services and
everything else in session 0, and elevated (high integrity) programs. Those return
`ERROR_ACCESS_DENIED`, which is written to the log once, after which the process is
left alone until it restarts. To manage them too, use "Restart as administrator" in
the tray menu, or start cpulytics from a scheduled task with the highest privileges
if you want it elevated at logon without a UAC prompt. The tray menu header shows
whether the current instance is elevated.

Protected processes (anti-cheat, some anti-malware services) refuse even for an
administrator; nothing can be done about that, and it is why the critical list
exists in the first place.

## Notes

- The icon is resource 1 of the executable, so explorer and the tray show the same
  image: `res/cpulytics.ico` (16 to 256 px, drawn by `tools/make_icon.py`, the two
  large sizes PNG compressed). It is linked in together with the manifest.
- Dark mode uses the documented dwm attributes plus the two undocumented uxtheme
  ordinals every dark win32 app uses; where they are missing the app simply stays
  light (theme.cpp).
- Only one instance can run at a time; two of them would fight over the same
  priorities. A second launch pings the running one, which says so with a balloon,
  and exits. "Restart as administrator" hands the singleton over first.
- Processes owned by another user or elevated beyond our rights simply fail to
  change once and are then left alone.
