v0.7

Improvements

The release assets are the plain executables now, one per architecture and named after it: `cpulytics-v0.7-windows-x64.exe`, `-x86.exe`, `-arm64.exe`. There is nothing to unpack, the binary carries its icon and manifest, needs no runtime and writes its settings to `%APPDATA%\cpulytics` on first run.

The build script takes unix style options: `--arch <x64|x86|arm64>`, `--all`, `--test`, `--run`, `--fetch`, `--help`, with short forms `-a -t -r -f -h`. Without arguments it builds for the architecture the machine runs instead of assuming x64.

`--all` builds every architecture in one go, finding `vcvarsall.bat` itself and opening the matching developer environment for each target. A target whose msvc toolset is not installed is reported and skipped instead of compiling objects for the wrong machine and failing at the link.

Docs

The licence carries the contact address, and the build section of the readme lists the options, the components needed and what to do when a build fails.

Full Changelog: v0.6...v0.7
