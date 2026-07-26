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
- Settings live in a window with a hint and a tooltip per option, in seven
  languages, following the Windows light and dark theme. One instance only.

## Build

Needs MSVC (`cl` and `rc` on PATH, from a Developer PowerShell) or MinGW `g++`
with `windres`. The script picks whichever it finds and compiles the resources -
the icon and the manifest - with the matching resource compiler.

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

The version is taken from `git describe`, there is no version file to bump. The
icon is generated, not committed as an opaque blob: `python tools/make_icon.py`
redraws `res/cpulytics.ico` from the code in that script.

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

| Key | Default | Range | Meaning |
|---|---|---|---|
| `enabled` | true | | master switch, also in the tray menu |
| `autostart` | false | | start cpulytics at logon |
| `sample_interval_ms` | 2000 | 250 - 60000 | how often the process table is read |
| `window_seconds` | 600 | 30 - 7200 | length of the sliding window |
| `min_history_seconds` | 120 | 5 - window | data needed before anything is decided |
| `demote_percent` | 20 | 1 - 100 | window average that triggers a demotion |
| `restore_percent` | 8 | 0 - demote | window average that earns a step back |
| `startup_grace_seconds` | 60 | 0 - 3600 | how long a fresh process is immune |
| `action_cooldown_seconds` | 60 | 1 - 3600 | minimum gap between two changes of one process |
| `restore_after_seconds` | 120 | 1 - 7200 | how long a process must stay calm to be restored |
| `max_steps` | 2 | 0 - 2 | how deep a demotion may go |
| `system_max_steps` | 1 | 0 - max_steps | the same cap for session 0 processes |
| `fullscreen_max_steps` | 0 | 0 - max_steps | the same cap for fullscreen apps |
| `eco_qos` | false | | also mark demoted processes as low power |
| `protect_foreground` | true | | never demote the window you are using |
| `notifications` | true | | balloon on every change |
| `restore_on_exit` | true | | put everything back on shutdown |
| `log_enabled` | true | | write `cpulytics.log` next to the config |
| `language` | auto | | auto, en, es, ru, zh, ja, ko, ar |
| `max_tracked` | 2048 | 64 - 65536 | upper bound on the history map |
| `log_max_kb` | 512 | 16 - 65536 | log is truncated past this size |
| `whitelist` | empty | | executables that are never touched |

Every value is clamped into the range above when it is read, so a typo in the file
cannot produce a setting that misbehaves - it produces the nearest sane one.

### How the settings work together

Once per `sample_interval_ms` the whole process table is read and each process gets
one more point in its window. From the oldest and the newest point in the window
comes the average CPU, as a percentage of the whole machine, where all cores busy
is 100%.

A process is demoted one step when all of this holds: the window really holds
`min_history_seconds` of data, the process is older than `startup_grace_seconds`,
its average is at or above `demote_percent`, `action_cooldown_seconds` have passed
since the last change, it is not protected (critical, whitelisted, foreground), and
its current step is below the cap that applies to it (`max_steps`,
`system_max_steps` or `fullscreen_max_steps`).

It gets a step back when the average stays at or below `restore_percent` for
`restore_after_seconds`, and it gets everything back at once when it becomes the
foreground window or when a cap is lowered below its current step.

**`autostart`** - writes this executable into the per user
`HKCU\...\CurrentVersion\Run` key, which is the same list the task manager startup
tab shows. No administrator right and no scheduled task is involved, and for the
same reason the copy started at logon is never elevated - see the rights section
below if you need that. The setting is the truth: the entry is rewritten on every
start, so moving or rebuilding the exe fixes itself, and removing the entry by hand
while the setting is on brings it back on the next start.

### Sampling

**`sample_interval_ms`** - how often the process table is read. It is one syscall
for all processes, so the cost is negligible either way; the value decides how
finely short spikes are resolved and how many points a window holds (window divided
by interval, which is what the history costs in memory). Lower it to 500 while
tuning to see the numbers move, put it back afterwards.

**`window_seconds`** - the heart of the whole thing. CPU usage is averaged over
this much history, so a ten minute window ignores anything that burns the machine
briefly and reacts only to load that keeps going. Shorten it to react faster, at
the price of catching honest work like a build or an export; lengthen it to punish
only chronic offenders.

**`min_history_seconds`** - no decision is made until the window actually holds
this much data. It is what keeps cpulytics quiet for the first two minutes after it
starts, and what stops a process that appeared seconds ago from being judged on a
handful of samples. Clamped to at most `window_seconds`.

### Thresholds

**`demote_percent`** - the window average at which a process gets one step down,
in percent of the whole machine. Scale it to your cpu: on a 16 thread laptop one
fully busy core is about 6%, so 20% means roughly three cores pinned for the entire
window. Lower is more aggressive.

**`restore_percent`** - the average below which a demoted process counts as calm.
It must stay under `demote_percent`, and the gap between the two is the hysteresis
that stops a process from flapping between levels; the loader forces at least half
a percent of gap.

### Timing

**`startup_grace_seconds`** - a freshly started process is left alone for this
long. Compilers, browsers, games and installers all burn cpu while they load, and
that burst says nothing about how they will behave a minute later. The age is the
real process creation time, not the moment cpulytics first saw it.

**`action_cooldown_seconds`** - the minimum gap between two changes of the same
process. It stops a process being walked from normal down to idle within two
samples, and it rate limits the balloons.

**`restore_after_seconds`** - how long the average has to stay under
`restore_percent` before one step is given back. Together with the cooldown it sets
how quickly a process that calmed down returns to normal.

### How deep it goes

The ladder is realtime, high, above normal, normal, below normal, idle. A demotion
moves that many entries down from the class the process had when cpulytics first
touched it, and never past idle. That original class is remembered and is what a
restore puts back, so repeated demotions cannot ratchet a process downwards.

**`max_steps`** - 0 turns demotion off completely and leaves a pure monitor, 1
allows normal to below normal, 2 allows the second step down to idle.

**`system_max_steps`** - the cap for processes in session 0: services, the update
stack, Defender. The rest of the system depends on them, so by default they get the
first gentle step and nothing more. Set it to 0 to make services untouchable.

**`fullscreen_max_steps`** - the cap for apps that were ever seen owning a window
covering a whole monitor: games, video players, presentations. The default 0 means
immune. The mark sticks to the process for the rest of its life, so alt-tabbing out
of a game does not silently remove its protection.

### Behaviour

**`eco_qos`** - additionally marks a demoted process as low power (EcoQoS): windows
stops boosting for it and prefers efficient cores where the cpu has them. It is
cleared together with the priority when the process is restored. Needs windows 11
or 10 21H1; where the api is missing the demotion still happens and the flag is
skipped. On a cpu without efficiency cores the effect is on clocks and power draw,
not on which core the work lands.

**`protect_foreground`** - the process owning the foreground window is never
demoted, and if it is already demoted it is put back to its original class at once,
without waiting out `restore_after_seconds`. This is what guarantees cpulytics never
slows down the thing you are looking at.

**`notifications`** - a balloon on every change, naming up to three processes and
counting the rest. Turn it off for a completely silent app; the log still records
every decision.

**`restore_on_exit`** - restore every touched process when cpulytics exits, on the
tray menu Exit as well as on log off and shutdown. Off leaves priorities where they
are. Note that a hard kill cannot restore anything either way, so the next start
after one will find processes it does not know it demoted.

**`log_enabled`** - writes `cpulytics.log` next to the config: start and stop, every
change with the old class, the new one and the average that caused it, and every
process that refused with access denied.

**`language`** - `auto` follows the windows display language, or force one of
`en`, `es`, `ru`, `zh`, `ja`, `ko`, `ar`. Arabic mirrors the settings window.

### Limits and exceptions

**`max_tracked`** - hard upper bound on the history map. Entries are dropped as
soon as a process exits, so this is only a safety net for a machine that spawns
thousands of short lived processes; when the cap is reached the quietest untouched
entries go first.

**`log_max_kb`** - the log is truncated once it grows past this. One file, no
rotation, nothing to clean up.

**`whitelist`** - comma separated executable names that are never touched, for
example `obs64.exe, ableton live.exe`. Case insensitive, file name only, no path.
On top of this list there is a built in one that no setting can override: the
kernel and session critical processes (`csrss`, `wininit`, `services`, `lsass`,
`dwm`, `audiodg`, ...), cpulytics itself, and anything running at realtime priority.

### Tuning

| Symptom | Knob |
|---|---|
| reacts too late to a process that has been eating the cpu for minutes | shorter `window_seconds`, lower `demote_percent` |
| demotes honest work like a build or an export | higher `demote_percent`, longer `startup_grace_seconds`, or the `whitelist` |
| a game or a video stutters | keep `fullscreen_max_steps` at 0 and `protect_foreground` on |
| a demoted process takes too long to come back | lower `restore_after_seconds`, higher `restore_percent` |
| a service keeps eating cpu and one step is not enough | raise `system_max_steps` to 2 |
| want it to watch and report only | `max_steps` = 0 |
| want less battery drain from background work | `eco_qos` = true |

## Tests

`tests/test_engine.cpp` drives the decision logic on a synthetic clock: startup
bursts, hysteresis, the system and fullscreen caps, recycled pids, foreground
protection.

`tests/test_integration.cpp` is the real thing: it starts a copy of itself that
burns a core, runs the real sampler and engine over the real process table, and
checks that the child is demoted to below normal and restored to normal once it
goes quiet. It also checks that an exited process leaves nothing behind in memory,
that the icon resource is linked in every size, that EcoQoS survives a round trip,
that the theme palette and the dpi query answer, and that every interface string
exists in every language.

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

`autostart` cannot start an elevated copy: entries in the Run key always start as
the plain user. For an elevated start at logon, create a scheduled task at logon
with "run with highest privileges" pointing at the exe, and leave `autostart` off
so the two do not both fire.

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
