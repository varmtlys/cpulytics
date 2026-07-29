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

One script builds everything: `scripts\build.ps1`. It finds the toolchain itself,
compiles the resources with the matching resource compiler and, with `--test`, runs
both test suites. Without arguments it builds for the architecture this machine
runs.

```powershell
.\scripts\build.ps1                  # build for this machine
.\scripts\build.ps1 --arch x86       # build 32 bit
.\scripts\build.ps1 --all --test     # every architecture, with the test suites
.\scripts\build.ps1 -h               # the full list of options
```

| Option | Meaning |
|---|---|
| `-a`, `--arch <x64\|x86\|arm64>` | build one architecture; the default is what this machine runs |
| `--all` | build every architecture, opening the right developer environment for each |
| `-t`, `--test` | run both test suites after building |
| `-r`, `--run` | start cpulytics after building |
| `-f`, `--fetch` | download a toolchain without asking, if none is found |
| `-h`, `--help` | usage |

Output goes to `build\<arch>\cpulytics.exe`.

From cmd.exe, or when the execution policy blocks the script:

```
powershell -ExecutionPolicy Bypass -File scripts\build.ps1
```

### What you need

| Component | Needed for | Where |
|---|---|---|
| A C++17 compiler: MinGW-w64 **or** MSVC | everything | see below |
| A resource compiler: `windres` (MinGW) or `rc` (Windows SDK) | the icon and the manifest, without which there is no theme and no dpi awareness | comes with the compiler |
| PowerShell 5.1 | the build script | part of Windows |
| Git | the version number (`git describe`); without it the build is called `dev` | <https://git-scm.com/download/win> |
| Python 3 | only to redraw the icon (`tools/make_icon.py`) | <https://www.python.org/downloads/windows/> |

Nothing else is fetched: the app links against system libraries only
(`user32`, `gdi32`, `shell32`, `comctl32`, `advapi32`, `uxtheme`, `dwmapi`).

**If you have neither**, the script offers to fetch one: it asks first, naming the
package, the url, the size and where it will go, and only downloads after a yes.

The archive is a portable MinGW-w64 (WinLibs). It takes the `.7z` (~104 MB, half
the size of the `.zip`) whenever the `tar.exe` that ships with Windows is there to
unpack it, verifies it against the sha256 published next to it, and unpacks it into
`%LOCALAPPDATA%\cpulytics	oolchain` - **outside the repository**, so cleaning
`build\` or deleting the clone never throws it away, and a second clone reuses it.
Later builds pick it up silently. Set `CPULYTICS_TOOLCHAIN` to put it elsewhere.

The toolchain is published as one archive, so there is nothing to pick and choose
at download time; what the build can do is drop the parts it will never call -
the debugger, the fortran compiler, the documentation and the translations - which
takes about 90 MB off after unpacking.

Nothing is installed system wide, nothing is written to your PATH and no
administrator right is needed. `--fetch` answers yes in advance, for unattended use;
in a non interactive session or with `CI` set nothing is downloaded without it.

**MinGW-w64** is the simplest way in, and the one used for the x64 build. Any of
these works, as long as `g++.exe`, `gcc.exe` and `windres.exe` end up in the same
`bin` directory:

- MinGW-w64 builds (this project is built with 15.2, UCRT, seh):
  <https://github.com/niXman/mingw-builds-binaries/releases>
- WinLibs, the same thing repackaged: <https://winlibs.com/>
- MSYS2, if you want a package manager: <https://www.msys2.org/>, then
  `pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-binutils`
- or `winget install BrechtSanders.WinLibs.POSIX.UCRT`

The script looks for the toolchain next to `g++` on the PATH first, then in
`C:\ProgramData\mingw64\mingw64\bin`, `C:\mingw64\bin`, `C:\msys64\mingw64\bin`
and the chocolatey location, and prints the one it picked. It then calls `g++` and
`windres` by full path and puts that directory first on the PATH for the build, so
another compiler somewhere else in the PATH cannot break it. If nothing is found it
says so instead of failing halfway through.

Note that the usual MinGW-w64 packages are **not** multilib: they build x64 only.
For x86 use MSVC.

**MSVC** covers all three architectures, including arm64. Install Visual Studio
2022 (any edition) or the standalone Build Tools, with the workload *Desktop
development with C++* - it brings `cl.exe`, `rc.exe` from the Windows SDK, and the
optional *MSVC v143 - ARM64 build tools* component for arm64:

- <https://visualstudio.microsoft.com/downloads/> (Build Tools are under
  "Tools for Visual Studio")
- or `winget install Microsoft.VisualStudio.2022.BuildTools`

Then build from a developer prompt, where `cl` and `rc` are on the PATH - the
script prefers MSVC whenever it sees `cl`. The prompt decides the architecture, so
it has to agree with `--arch` (or just use `--all`, which does this for you):

| Start menu entry | `--arch` |
|---|---|
| x64 Native Tools Command Prompt for VS 2022 | `x64` (default) |
| x86 Native Tools Command Prompt for VS 2022 | `x86` |
| ARM64 Cross Tools Command Prompt for VS 2022 | `arm64` |

An arm64 build made on an x64 machine is a cross build: the test binaries are
compiled but not executed there, they run on arm64 hardware.

### Output and version

Binaries land in `build\<arch>\`, which is in `.gitignore`. The version comes from
`git describe --tags --always --dirty` and is written into `build\<arch>\version.h`
by the script, so there is no version file to bump by hand.

The icon is generated, not a committed blob: `python tools/make_icon.py` redraws
`res/cpulytics.ico` (16 to 256 px) from the code in that script.

### When it fails

**`cpulytics is running and holds ... open`** - a running copy keeps its own exe
locked. Exit it from the tray menu, or `taskkill /F /IM cpulytics.exe`.

**`windres: preprocessing failed`** - `windres` preprocesses the `.rc` by running
`gcc` from the PATH. The script puts its own toolchain directory in front of the
PATH for exactly this reason, so this means an incomplete MinGW directory: check
what is actually there:

```
where.exe gcc g++ windres
```

They must all come from the same MinGW `bin` directory.

**`No toolchain found`** - neither `cl` (from a developer prompt) nor a complete
MinGW directory was found. Install one of the two above, or point the PATH at it
for this shell:

```powershell
$env:PATH = "C:\ProgramData\mingw64\mingw64\bin;$env:PATH"
```

**`cl was found but rc ... was not`** - `cl` is on the PATH but the Windows SDK
tools are not, which happens when the environment was set up by hand. Use one of
the developer prompts listed above.

## Settings

"Settings..." in the tray menu opens a window with every option, a one line hint
next to each of them and a tooltip with the allowed range and the default. The
interface speaks English, Spanish, Russian, Chinese, Japanese, Korean and Arabic
(mirrored layout); "Auto" follows the Windows display language.

The window and the tray menu follow the Windows light and dark setting by default,
including the title bar and the rounded Windows 11 corners, and switch over as soon
as the system does. The Theme row overrides that with a fixed dark or light. The application manifest asks for common controls 6 and per monitor
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
stops boosting for it and moves it to the efficient cores. It is cleared together
with the priority when the process is restored, and it needs windows 11 or 10 21H1.

The setting is only offered on a cpu that actually has efficiency cores. cpulytics
asks the scheduler (`GetSystemCpuSetInformation`): if every core reports the same
efficiency class the machine is homogeneous, there is nowhere to move the work to,
and the checkbox is greyed out with that as its hint. On such a machine the flag is
never applied even if the settings file says otherwise.

**`protect_foreground`** - the process owning the foreground window is never
demoted, and if it is already demoted it is put back to its original class at once,
without waiting out `restore_after_seconds`. This is what guarantees cpulytics never
slows down the thing you are looking at.

**`notifications`** - a balloon on every change, naming up to three processes and
counting the rest. Turn it off for a completely silent app; the log still records
every decision.

**`restore_on_exit`** - restore every touched process when cpulytics exits, on the
tray menu Exit as well as on log off and shutdown. Off leaves priorities where they
are. A hard kill cannot run this, so what is held is also written to `state.txt`
next to the config: the next start reads it, puts those processes back (matching
them by pid *and* start time, so a recycled pid is never touched) and deletes the
file.

**`log_enabled`** - writes `cpulytics.log` next to the config: start and stop, every
change with the old class, the new one and the average that caused it, and every
process that refused with access denied. If nothing is happening and you expect it
to, this file is the answer: no `demote:` line means no process ever crossed
`demote_percent` over the whole window, and therefore no balloon either.

**`language`** - `auto` follows the windows display language, or force one of
`en`, `es`, `ru`, `zh`, `ja`, `ko`, `ar`. Arabic mirrors the settings window.

**`theme`** - `system` follows the windows light and dark setting and changes with
it while the app runs; `dark` and `light` pin it. It covers the settings window,
the about window and the tray menu, title bars and all.

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

## About and licence

"About" in the tray menu opens a small window with the version, the author, the
licence and the same icon the tray shows, themed like the rest of the app. cpulytics is MIT licensed, see [LICENSE](LICENSE) - free to
use, change and share, with no warranty.

## Tests

`tests/test_engine.cpp` drives the decision logic on a synthetic clock: startup
bursts, hysteresis, the system and fullscreen caps, recycled pids, foreground
protection.

`tests/test_integration.cpp` is the real thing: it starts a copy of itself that
burns a core, runs the real sampler and engine over the real process table, and
checks that the child is demoted to below normal and restored to normal once it
goes quiet. It also checks that an exited process leaves nothing behind in memory,
that the icon resource is linked in every size, that EcoQoS survives a round trip,
that the autostart entry can be written and removed, that the theme palette, the
dpi and the efficiency core query answer, and that every interface string exists in
every language.

## Releases

Versions are git tags: `vMAJOR.MINOR` for features, `vMAJOR.MINOR.PATCH` for fixes.
Pushing a tag runs `.github/workflows/release.yml`, which builds and tests all
three architectures and publishes a release with `RELEASE_NOTES.md` as its body.
The assets are the plain executables, one per architecture and named after it -
`cpulytics-<tag>-windows-x64.exe`, `-x86.exe`, `-arm64.exe`. There is nothing to
unpack: the binary carries its icon and manifest, needs no runtime and writes its
settings to `%APPDATA%` on first run.

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

- The manifest is compiled into `default-manifest.o` for the MinGW build, which is
  the name gcc links by itself - ours then replaces the one newer toolchains ship
  instead of clashing with it ("multiple non-default manifests").
- The icon is resource 1 of the executable, so explorer and the tray show the same
  image: `res/cpulytics.ico` (16 to 256 px, drawn by `tools/make_icon.py`, the two
  large sizes PNG compressed), linked from `res/icon.rc`.
- Dark mode uses the documented dwm attributes plus the two undocumented uxtheme
  ordinals every dark win32 app uses; where they are missing the app simply stays
  light (theme.cpp).
- Only one instance can run at a time; two of them would fight over the same
  priorities. A second launch pings the running one, which says so with a balloon,
  and exits. "Restart as administrator" hands the singleton over first.
- Processes owned by another user or elevated beyond our rights simply fail to
  change once and are then left alone.
