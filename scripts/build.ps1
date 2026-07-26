<#
    Builds cpulytics.exe into build\<arch>\ and, with -Test, the test binaries too.
    Uses MSVC (cl) when it is on PATH, otherwise MinGW g++.

        .\scripts\build.ps1                # x64 release build
        .\scripts\build.ps1 -Arch x86      # 32 bit build
        .\scripts\build.ps1 -Test          # build and run the tests

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
    [switch]$Run
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
function Find-Mingw {
    $dirs = @()
    $g = Get-Command g++ -ErrorAction SilentlyContinue
    if ($g) { $dirs += Split-Path $g.Source -Parent }
    $dirs += 'C:\ProgramData\mingw64\mingw64\bin', 'C:\mingw64\bin', 'C:\msys64\mingw64\bin',
             'C:\ProgramData\chocolatey\lib\mingw\tools\install\mingw64\bin'
    foreach ($d in $dirs) {
        if ((Test-Path (Join-Path $d 'g++.exe')) -and (Test-Path (Join-Path $d 'gcc.exe')) -and
            (Test-Path (Join-Path $d 'windres.exe'))) {
            return $d
        }
    }
    return $null
}

$mingw = $null
if ($useMsvc) {
    if (-not (Get-Command rc -ErrorAction SilentlyContinue)) {
        throw ("cl was found but rc (the resource compiler from the windows sdk) was not. " +
               "Run this from a Developer PowerShell / Developer Command Prompt for VS.")
    }
} else {
    $mingw = Find-Mingw
    if (-not $mingw) {
        throw ("No toolchain found. Either open a Developer Command Prompt for VS (msvc), " +
               "or install mingw-w64 so that g++.exe, gcc.exe and windres.exe live in one " +
               "directory - see the Build section of README.md.")
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

# The icon and the manifest, compiled once per build and linked into every binary
# so the tests can check the icon the same way the app loads it.
function Build-Resources {
    $res = Join-Path $build 'cpulytics.res'
    if ($useMsvc) {
        Invoke-Tool 'rc' @('/nologo', "/fo$res", "$root\res\cpulytics.rc")
    } else {
        $target = if ($Arch -eq 'x86') { 'pe-i386' } else { 'pe-x86-64' }
        Invoke-Tool 'windres' @('-I', "$root\res", '-F', $target, '-O', 'coff', "$root\res\cpulytics.rc", $res)
    }
    return $res
}

function Build-Exe($outName, $sources, $gui) {
    $out = Join-Path $build $outName
    if ($useMsvc) {
        $a = @('/nologo', '/std:c++17', '/EHsc', '/O2', '/W3', '/DUNICODE', '/D_UNICODE', '/DNOMINMAX', '/utf-8',
               "/I$build", "/I$root\src", "/Fo:$build\", "/Fe:$out") + $sources +
             @($resources, '/link', 'shell32.lib', 'user32.lib', 'gdi32.lib', 'comctl32.lib', 'advapi32.lib',
               'uxtheme.lib', 'dwmapi.lib', '/MANIFEST:NO')  # the manifest comes from the resource
        if ($gui) { $a += '/SUBSYSTEM:WINDOWS' }
        $a += switch ($Arch) { 'x86' { '/MACHINE:X86' } 'arm64' { '/MACHINE:ARM64' } default { '/MACHINE:X64' } }
        Invoke-Tool 'cl' $a
    } else {
        $a = @('-std=c++17', '-O2', '-Wall', '-Wextra', '-DNOMINMAX', '-DUNICODE', '-D_UNICODE',
               '-static', '-static-libgcc',
               '-static-libstdc++', '-s', "-I$build", "-I$root\src", '-o', $out) + $sources +
             @('-lshell32', '-luser32', '-lgdi32', '-lcomctl32', '-ladvapi32', '-luxtheme', '-ldwmapi')
        $a += $resources
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

$resources = Build-Resources
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
