v0.7.1

Fixes

The tray tooltip, the tray menu rows and the log line are built as strings of any length now. They were formatted into fixed size buffers, and an executable with a long enough name wrote past the end of them.

A maximised window no longer counts as fullscreen. With an auto-hidden taskbar a maximised window covers the whole monitor exactly, so every browser or editor that was ever maximised became immune to demotion for the rest of its life, as if it were a game.

A process that already sits at idle is no longer announced again on every cooldown. The step is still counted, but a change that leaves the priority class where it was produces neither a log line nor a balloon.

The log size cap counts what the file already holds when the app starts. Before, every start counted from zero and the file could grow to twice `log_max_kb`.

A decimal setting such as `demote_percent` is rounded to tenths as a whole in the settings window: 7.96 read as 7.10 before, now it reads 8.0.

Improvements

`--all` in the build script no longer prints the complaint of `vcvarsall.bat` about a `vswhere.exe` it cannot find on the PATH; the build was never affected by it.

Docs

The readme lists the `theme` setting in the settings table and gives the toolchain folder `%LOCALAPPDATA%\cpulytics\toolchain` its lost backslash.

Full Changelog: v0.7...v0.7.1
