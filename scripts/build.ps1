<#
    Builds cpulytics.exe into build\<arch>\ and, with -Test, the test binaries too.
    Uses MSVC (cl) when it is on PATH, otherwise MinGW g++.

        .\scripts\build.ps1                # x64 release build
        .\scripts\build.ps1 -Arch x86      # 32 bit build
        .\scripts\build.ps1 -Test          # build and run the tests

    With MSVC the architecture comes from the developer prompt the script runs in
    (vcvars64, or vcvarsall x86), so -Arch only has to agree with it. With g++ it is
    passed as -m64 / -m32, and the 32 bit build needs a multilib toolchain.
#>
[CmdletBinding()]
param(
    [ValidateSet('x64', 'x86')]
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

function Invoke-Tool($exe, $argList) {
    & $exe @argList
    if ($LASTEXITCODE -ne 0) { throw "$exe failed with exit code $LASTEXITCODE" }
}

function Build-Exe($outName, $sources, $gui) {
    $out = Join-Path $build $outName
    if ($useMsvc) {
        $a = @('/nologo', '/std:c++17', '/EHsc', '/O2', '/W3', '/DUNICODE', '/D_UNICODE', '/DNOMINMAX',
               "/I$build", "/I$root\src", "/Fo:$build\", "/Fe:$out") + $sources +
             @('/link', 'shell32.lib', 'user32.lib', 'advapi32.lib')
        if ($gui) { $a += '/SUBSYSTEM:WINDOWS' }
        $a += if ($Arch -eq 'x86') { '/MACHINE:X86' } else { '/MACHINE:X64' }
        Invoke-Tool 'cl' $a
    } else {
        $a = @('-std=c++17', '-O2', '-Wall', '-Wextra', '-DNOMINMAX', '-static', '-static-libgcc',
               '-static-libstdc++', '-s', "-I$build", "-I$root\src", '-o', $out) + $sources +
             @('-lshell32', '-luser32', '-ladvapi32')
        $a += if ($Arch -eq 'x86') { '-m32' } else { '-m64' }
        if ($gui) { $a += @('-mwindows', '-municode') }
        Invoke-Tool 'g++' $a
    }
    Write-Host "  -> $out"
}

Build-Exe 'cpulytics.exe' $src $true

if ($Test) {
    Build-Exe 'test_engine.exe' (@("$root\tests\test_engine.cpp") + $engineSrc) $false
    Build-Exe 'test_integration.exe' (@("$root\tests\test_integration.cpp") + $engineSrc) $false
    Invoke-Tool (Join-Path $build 'test_engine.exe') @()
    Invoke-Tool (Join-Path $build 'test_integration.exe') @()
    Write-Host 'all tests passed' -ForegroundColor Green
}

if ($Run) { Start-Process (Join-Path $build 'cpulytics.exe') }
