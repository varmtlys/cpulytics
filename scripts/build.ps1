<#
    Builds cpulytics.exe into build\<arch>\ and, with -Test, the test binaries too.
    Uses MSVC (cl) when it is on PATH, otherwise MinGW g++.

        .\scripts\build.ps1                # x64 release build
        .\scripts\build.ps1 -Arch x86      # 32 bit build
        .\scripts\build.ps1 -Test          # build and run the tests
        .\scripts\build.ps1 -Fetch         # download a toolchain if none is installed

    With MSVC the architecture comes from the developer prompt the script runs in
    (vcvars64, vcvarsall x86, vcvarsall x64_arm64), so -Arch only has to agree with
    it. With g++ it is -m64 / -m32, the 32 bit build needs a multilib toolchain, and
    arm64 needs an aarch64 cross compiler - arm64 is normally built with MSVC.

    An arm64 build made on an x64 machine is a cross build: it is compiled but its
    tests are not run there, they run on arm64 hardware.
#>
[CmdletBinding()]
param(
    [ValidateSet('x64', 'x86', 'arm64')]
    [string]$Arch = 'x64',
    [switch]$Test,
    [switch]$Run,
    # Answers yes to the "download a toolchain?" question, for unattended use.
    [switch]$Fetch
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root "build\$Arch"
New-Item -ItemType Directory -Force $build | Out-Null

# Version comes from the git tag, nothing else. No version file to forget to bump.
$version = 'dev'
if (Get-Command git -ErrorAction SilentlyContinue) {
    # A repository without tags (or without commits) is not an error here.
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $d = & git -C $root describe --tags --always --dirty 2>$null
    $ErrorActionPreference = $prev
    if ($LASTEXITCODE -eq 0 -and $d) { $version = "$d".Trim() }
}
Set-Content -Encoding utf8 (Join-Path $build 'version.h') "#pragma once`n#define CPULYTICS_VERSION `"$version`"`n"
Write-Host "cpulytics $version ($Arch)"

$src = Get-ChildItem (Join-Path $root 'src\*.cpp') | ForEach-Object { $_.FullName }
$engineSrc = $src | Where-Object { $_ -notmatch 'tray\.cpp$' }
$useMsvc = [bool](Get-Command cl -ErrorAction SilentlyContinue)

# A mingw install is one directory holding g++, gcc and windres together. Whatever
# else the PATH of this shell happens to contain, the build uses one toolchain.
function Test-MingwDir($dir) {
    return $dir -and (Test-Path (Join-Path $dir 'g++.exe')) -and (Test-Path (Join-Path $dir 'gcc.exe')) -and
           (Test-Path (Join-Path $dir 'windres.exe'))
}

function Find-Mingw {
    $dirs = @()
    $g = Get-Command g++ -ErrorAction SilentlyContinue
    if ($g) { $dirs += Split-Path $g.Source -Parent }
    $dirs += 'C:\ProgramData\mingw64\mingw64\bin', 'C:\mingw64\bin', 'C:\msys64\mingw64\bin',
             'C:\ProgramData\chocolatey\lib\mingw\tools\install\mingw64\bin'
    # A toolchain downloaded by an earlier run is used again without asking.
    $cached = Get-ChildItem (Get-ToolchainRoot) -Recurse -Filter 'g++.exe' -ErrorAction SilentlyContinue
    foreach ($exe in $cached) { $dirs += Split-Path $exe.FullName -Parent }
    foreach ($d in $dirs) {
        if (Test-MingwDir $d) { return $d }
    }
    return $null
}

# Where a downloaded toolchain lives. Outside the repository on purpose: cleaning
# build\ or deleting the clone must not throw away a toolchain that took minutes
# to fetch, and a second clone reuses the same one.
function Get-ToolchainRoot {
    if ($env:CPULYTICS_TOOLCHAIN) { return $env:CPULYTICS_TOOLCHAIN }
    return (Join-Path $env:LOCALAPPDATA 'cpulytics\toolchain')
}

# Parts of a gcc distribution this project never uses. Removing them is the only
# trimming that is possible: the toolchain is published as one archive, there are
# no per component downloads.
$script:Unused = @(
    'bin\gdb.exe', 'bin\gdbserver.exe', 'bin\gdb-add-index.exe', 'bin\gcore.exe', 'share\gdb',
    'bin\gfortran.exe', 'bin\x86_64-w64-mingw32-gfortran.exe', 'bin\i686-w64-mingw32-gfortran.exe',
    'share\doc', 'share\man', 'share\info', 'share\locale', 'share\gcc-*'
)

function Remove-UnusedParts($dir) {
    $freed = 0
    foreach ($rel in $script:Unused) {
        foreach ($item in (Get-ChildItem (Join-Path $dir $rel) -Force -ErrorAction SilentlyContinue)) {
            $size = (Get-ChildItem $item.FullName -Recurse -File -Force -ErrorAction SilentlyContinue |
                     Measure-Object -Property Length -Sum).Sum
            Remove-Item $item.FullName -Recurse -Force -ErrorAction SilentlyContinue
            if ($size) { $freed += $size }
        }
    }
    # f951 is the fortran compiler proper, next to the c and c++ ones.
    foreach ($f in (Get-ChildItem (Join-Path $dir 'libexec') -Recurse -Filter 'f951.exe' -ErrorAction SilentlyContinue)) {
        $freed += $f.Length
        Remove-Item $f.FullName -Force -ErrorAction SilentlyContinue
    }
    if ($freed) { Write-Host ("  removed {0} MB of parts this build never uses" -f [math]::Round($freed / 1MB)) }
}

# The tar that ships with windows is libarchive, which reads 7z. A tar from git
# or msys on the PATH is gnu tar, which does not, so it is addressed by full path.
function Get-Bsdtar {
    $t = Join-Path $env:SystemRoot 'System32\tar.exe'
    if (-not (Test-Path $t)) { return $null }
    $v = & $t --version 2>$null
    if ("$v" -match 'bsdtar') { return $t }
    return $null
}

# Downloads a portable mingw-w64 (WinLibs) into the toolchain directory, after
# asking. Nothing is installed system wide and nothing is put on the user PATH.
function Get-Mingw {
    if ($Arch -eq 'arm64') {
        Write-Host '  no mingw cross compiler for arm64 exists as a download, use msvc' -ForegroundColor Yellow
        return $null
    }
    $pattern = if ($Arch -eq 'x86') { '^winlibs-i686-posix-dwarf-gcc-.*ucrt.*' }
               else { '^winlibs-x86_64-posix-seh-gcc-.*ucrt.*' }

    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $repo = 'brechtsanders/winlibs_mingw'
    try {
        $release = Invoke-RestMethod -Uri "https://api.github.com/repos/$repo/releases/latest" `
                                     -Headers @{ 'User-Agent' = 'cpulytics-build' }
    } catch {
        Write-Host "  could not reach github: $($_.Exception.Message)" -ForegroundColor Yellow
        return $null
    }

    # The .7z is less than half the size of the .zip, and tar.exe (libarchive)
    # ships with windows 10 and 11, so it is the first choice.
    $assets = $release.assets | Where-Object { $_.name -match ($pattern + '\.(7z|zip)$') }
    $sevenZip = $assets | Where-Object { $_.name -like '*.7z' } | Select-Object -First 1
    $zip = $assets | Where-Object { $_.name -like '*.zip' } | Select-Object -First 1
    $bsdtar = Get-Bsdtar
    $asset = if ($bsdtar -and $sevenZip) { $sevenZip } else { $zip }
    if (-not $asset) {
        Write-Host "  no matching build in $repo $($release.tag_name)" -ForegroundColor Yellow
        return $null
    }

    $dest = Get-ToolchainRoot
    Write-Host ''
    Write-Host 'No C++ toolchain was found on this machine.'
    Write-Host "  package : mingw-w64 $($release.tag_name) (WinLibs, portable, no installer)"
    Write-Host "  from    : $($asset.browser_download_url)"
    Write-Host ("  size    : {0} MB download" -f [math]::Round($asset.size / 1MB))
    Write-Host "  into    : $dest  (kept for later builds)"
    if (-not $Fetch) {
        if ($env:CI -or -not [Environment]::UserInteractive) {
            Write-Host '  not asking in a non interactive session, pass -Fetch to allow it' -ForegroundColor Yellow
            return $null
        }
        if ((Read-Host 'Download it? [y/N]') -notmatch '^(y|yes)$') { return $null }
    }

    New-Item -ItemType Directory -Force $dest | Out-Null
    $archive = Join-Path $dest $asset.name
    Write-Host "  downloading $($asset.name) ..."
    $progress = $ProgressPreference
    $ProgressPreference = 'SilentlyContinue'  # the progress bar makes this many times slower
    try {
        Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $archive -UseBasicParsing
    } catch {
        $ProgressPreference = $progress
        Write-Host "  download failed: $($_.Exception.Message)" -ForegroundColor Yellow
        return $null
    }

    # Every archive is published with a .sha256 next to it, so the download is
    # verified instead of trusted.
    $sum = $release.assets | Where-Object { $_.name -eq ($asset.name + '.sha256') } | Select-Object -First 1
    if ($sum) {
        $body = (Invoke-WebRequest -Uri $sum.browser_download_url -UseBasicParsing).Content
        $text = if ($body -is [byte[]]) { [Text.Encoding]::ASCII.GetString($body) } else { [string]$body }
        if ($text -match '([0-9a-fA-F]{64})') {
            $want = $matches[1].ToLower()
            $got = (Get-FileHash -Algorithm SHA256 $archive).Hash.ToLower()
            if ($got -ne $want) {
                Remove-Item $archive -Force
                $ProgressPreference = $progress
                throw "the downloaded archive does not match the published sha256 ($got)"
            }
            Write-Host '  sha256 ok'
        }
    }
    $ProgressPreference = $progress

    Write-Host '  unpacking ...'
    if ($archive -like '*.7z') {
        & $bsdtar -xf $archive -C $dest
        if ($LASTEXITCODE -ne 0) {
            Write-Host '  tar could not read the 7z archive' -ForegroundColor Yellow
            Remove-Item $archive -Force
            return $null
        }
    } else {
        Expand-Archive -Path $archive -DestinationPath $dest -Force
    }
    Remove-Item $archive -Force

    $found = Find-Mingw
    if (-not $found) { throw 'the downloaded archive did not contain g++, gcc and windres' }
    Remove-UnusedParts (Split-Path $found -Parent)
    return $found
}

$mingw = $null
if ($useMsvc) {
    if (-not (Get-Command rc -ErrorAction SilentlyContinue)) {
        throw ("cl was found but rc (the resource compiler from the windows sdk) was not. " +
               "Run this from a Developer PowerShell / Developer Command Prompt for VS.")
    }
} else {
    $mingw = Find-Mingw
    if (-not $mingw) { $mingw = Get-Mingw }
    if (-not $mingw) {
        throw ("No toolchain found. Either open a Developer Command Prompt for VS (msvc), " +
               "or install mingw-w64 so that g++.exe, gcc.exe and windres.exe live in one " +
               "directory, or re-run and answer yes to the download - see the Build section " +
               "of README.md.")
    }
    # windres shells out to gcc, and the compiler links against its own runtime:
    # both must come from this directory and not from whatever else is in PATH.
    $env:PATH = "$mingw;$env:PATH"
    Write-Host "  toolchain: $mingw"
}
# x86 and x64 binaries run on an arm64 host through emulation, arm64 ones run
# nowhere else, so a cross built arm64 test binary is compiled but not executed.
$canRun = ($Arch -ne 'arm64') -or ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64')

function Invoke-Tool($exe, $argList) {
    & $exe @argList
    if ($LASTEXITCODE -ne 0) { throw "$exe failed with exit code $LASTEXITCODE`n  $exe $($argList -join ' ')" }
}

# Compiles one .rc from res\ into $out. The icon goes into every binary so the
# tests can load it the same way the app does; the manifest is what gives the app
# common controls 6 and per monitor dpi awareness.
function Build-Resource($name, $out) {
    $rc = Join-Path $root "res\$name.rc"
    if ($useMsvc) {
        Invoke-Tool 'rc' @('/nologo', "/fo$out", $rc)
    } else {
        $target = if ($Arch -eq 'x86') { 'pe-i386' } else { 'pe-x86-64' }
        # windres preprocesses the .rc by running gcc from the PATH, which is why
        # the toolchain directory is put in front of it above. Naming the
        # preprocessor explicitly is not portable across binutils versions.
        Invoke-Tool "$mingw\windres.exe" @('-I', (Join-Path $root 'res'), '-F', $target, '-O', 'coff', $rc, $out)
    }
    return $out
}

function Build-Exe($outName, $sources, $gui) {
    $out = Join-Path $build $outName
    if ($useMsvc) {
        $a = @('/nologo', '/std:c++17', '/EHsc', '/O2', '/W3', '/DUNICODE', '/D_UNICODE', '/DNOMINMAX', '/utf-8', '/D_WIN32_WINNT=0x0A00', '/DWINVER=0x0A00',
               "/I$build", "/I$root\src", "/Fo:$build\", "/Fe:$out") + $sources +
             @($resIcon, $resManifest, '/link', 'shell32.lib', 'user32.lib', 'gdi32.lib', 'comctl32.lib', 'advapi32.lib',
               'uxtheme.lib', 'dwmapi.lib', '/MANIFEST:NO')  # the manifest comes from the resource
        if ($gui) { $a += '/SUBSYSTEM:WINDOWS' }
        $a += switch ($Arch) { 'x86' { '/MACHINE:X86' } 'arm64' { '/MACHINE:ARM64' } default { '/MACHINE:X64' } }
        Invoke-Tool 'cl' $a
    } else {
        $a = @('-std=c++17', '-O2', '-Wall', '-Wextra', '-DNOMINMAX', '-DUNICODE', '-D_UNICODE', '-D_WIN32_WINNT=0x0A00', '-DWINVER=0x0A00',
               '-static', '-static-libgcc',
               '-static-libstdc++', '-s', "-I$build", "-I$root\src", '-o', $out) + $sources +
             @('-lshell32', '-luser32', '-lgdi32', '-lcomctl32', '-ladvapi32', '-luxtheme', '-ldwmapi')
        # -B makes gcc take default-manifest.o from here instead of the copy that
        # newer mingw toolchains ship, so a binary never ends up with two manifests.
        $a += @($resIcon, "-B$build")
        if ($Arch -eq 'x86') { $a += '-m32' } elseif ($Arch -eq 'x64') { $a += '-m64' }
        if ($gui) { $a += @('-mwindows', '-municode') }
        Invoke-Tool "$mingw\g++.exe" $a
    }
    Write-Host "  -> $out"
}

# A running copy holds its own exe open, and the linker error for that is cryptic.
if (Get-Process cpulytics -ErrorAction SilentlyContinue) {
    throw "cpulytics is running and holds build\$Arch\cpulytics.exe open. Exit it from the tray menu, or: taskkill /F /IM cpulytics.exe"
}

$resIcon = Build-Resource 'icon' (Join-Path $build 'icon.res')
$resManifest = if ($useMsvc) { Build-Resource 'manifest' (Join-Path $build 'manifest.res') }
               else { Build-Resource 'manifest' (Join-Path $build 'default-manifest.o') }
Build-Exe 'cpulytics.exe' $src $true

if ($Test) {
    Build-Exe 'test_engine.exe' (@("$root\tests\test_engine.cpp") + $engineSrc) $false
    Build-Exe 'test_integration.exe' (@("$root\tests\test_integration.cpp") + $engineSrc) $false
    if (-not $canRun) {
        Write-Host "  tests built but not run: $Arch binaries do not run on $env:PROCESSOR_ARCHITECTURE" -ForegroundColor Yellow
    } else {
        Invoke-Tool (Join-Path $build 'test_engine.exe') @()
        Invoke-Tool (Join-Path $build 'test_integration.exe') @()
        Write-Host 'all tests passed' -ForegroundColor Green
    }
}

if ($Run -and $canRun) { Start-Process (Join-Path $build 'cpulytics.exe') }
