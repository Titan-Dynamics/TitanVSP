# Builds OpenVSP on Windows (Visual Studio 2022, x64, Release).
#
# Layout (build output lives next to the repo, not inside it):
#   <parent>\OpenVSP                 this repo
#   <parent>\OpenVSP-build\Libraries third-party libraries (built once)
#   <parent>\OpenVSP-build\vsp       OpenVSP build tree
#   <parent>\OpenVSP-build\install   runnable OpenVSP (vsp.exe + resources)
#
# Usage:  .\dev\build.ps1            incremental build + install
#         .\dev\build.ps1 -Clean     wipe the OpenVSP build tree (keeps Libraries) and rebuild
#         .\dev\build.ps1 -CleanAll  wipe everything, including Libraries, and rebuild

param(
    [switch]$Clean,
    [switch]$CleanAll
)

$ErrorActionPreference = 'Stop'

# Pick up tools installed since this shell started (winget installs of cmake/git).
$env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')

$RepoRoot   = Split-Path $PSScriptRoot -Parent
$BuildRoot  = Join-Path (Split-Path $RepoRoot -Parent) 'OpenVSP-build'
$LibsDir    = Join-Path $BuildRoot 'Libraries'
$VspDir     = Join-Path $BuildRoot 'vsp'
$InstallDir = Join-Path $BuildRoot 'install'
$Generator  = 'Visual Studio 17 2022'

function Invoke-Step([string]$Name, [scriptblock]$Cmd) {
    Write-Host "==> $Name" -ForegroundColor Cyan
    & $Cmd
    if ($LASTEXITCODE -ne 0) { throw "$Name failed (exit $LASTEXITCODE)" }
}

if ($CleanAll -and (Test-Path $BuildRoot)) { Remove-Item -Recurse -Force $BuildRoot }
if ($Clean) {
    foreach ($d in $VspDir, $InstallDir) { if (Test-Path $d) { Remove-Item -Recurse -Force $d } }
}

# 1. Third-party libraries. Only rebuilt when missing; they rarely change.
if (-not (Test-Path (Join-Path $LibsDir 'VSP_Libraries_Config.cmake'))) {
    Invoke-Step 'Configure Libraries' {
        cmake -S (Join-Path $RepoRoot 'Libraries') -B $LibsDir -G $Generator -A x64 -DCMAKE_BUILD_TYPE=Release
    }
    Invoke-Step 'Build Libraries' { cmake --build $LibsDir --config Release --parallel }
}

# 2. OpenVSP itself. Python/MATLAB API wrappers and Doxygen docs are disabled
#    (they need SWIG/Doxygen); remove those flags if you want them.
if (-not (Test-Path (Join-Path $VspDir 'CMakeCache.txt'))) {
    Invoke-Step 'Configure OpenVSP' {
        cmake -S (Join-Path $RepoRoot 'src') -B $VspDir -G $Generator -A x64 `
            -DCMAKE_BUILD_TYPE=Release `
            "-DVSP_LIBRARY_PATH=$LibsDir" `
            "-DCMAKE_INSTALL_PREFIX=$InstallDir" `
            -DVSP_NO_API_WRAPPERS=ON -DVSP_NO_DOC=ON -DVSP_NO_PYDOC=ON
    }
}

# INSTALL builds everything, then copies vsp.exe and its resources into $InstallDir.
Invoke-Step 'Build + install OpenVSP' { cmake --build $VspDir --config Release --target INSTALL --parallel }

Write-Host "Done. OpenVSP is at $InstallDir\vsp.exe" -ForegroundColor Green
