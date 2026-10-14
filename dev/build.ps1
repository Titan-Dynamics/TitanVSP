# Builds TitanVSP on Windows (Visual Studio 2022, x64, Release).
#
# Layout (build output lives next to the repo, not inside it):
#   <parent>\TitanVSP                this repo
#   <parent>\OpenVSP-build\Libraries third-party libraries (built once)
#   <parent>\OpenVSP-build\vsp       TitanVSP build tree
#   <parent>\OpenVSP-build\install   runnable TitanVSP (vsp.exe + resources)
#   <parent>\OpenVSP-build\vsp\venv  Python venv with the openvsp API (editable install)
#
# Usage:  .\dev\build.ps1            incremental build + install
#         .\dev\build.ps1 -Clean     wipe the TitanVSP build tree (keeps Libraries) and rebuild
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

# 2. Python API prerequisites. CMake silently skips the Python API if SWIG, Python
#    or numpy is missing, so check up front and fail loudly instead.
$Swig = (Get-Command swig -ErrorAction SilentlyContinue).Source
if (-not $Swig) {
    # winget's SWIG package doesn't always put swig.exe on PATH.
    $Swig = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Recurse -Filter swig.exe -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName
}
if (-not $Swig) { throw 'SWIG not found. Install it with: winget install SWIG.SWIG' }
$Python = (Get-Command python -ErrorAction SilentlyContinue).Source
if (-not $Python) { throw 'Python not found on PATH.' }
$ErrorActionPreference = 'Continue'   # so the import's stderr doesn't throw
& $Python -c 'import numpy' 2>$null
$NumpyMissing = $LASTEXITCODE -ne 0
$ErrorActionPreference = 'Stop'
if ($NumpyMissing) { throw "numpy not installed for $Python. Run: python -m pip install numpy" }
# The API is built for this one Python version.  The packaged python\environment.yml pins it, so
# the conda environment its setup script creates can load the bindings.
$PyVer = & $Python -c "import sys; print('%d.%d' % sys.version_info[:2])"

# 3. TitanVSP itself, with the Python API. Doxygen and Sphinx docs are disabled
#    (slow, and need Doxygen). Re-configures whenever this script is edited.
$Cache = Join-Path $VspDir 'CMakeCache.txt'
if (-not (Test-Path $Cache) -or (Get-Item $PSCommandPath).LastWriteTime -gt (Get-Item $Cache).LastWriteTime) {
    Invoke-Step 'Configure TitanVSP' {
        cmake -S (Join-Path $RepoRoot 'src') -B $VspDir -G $Generator -A x64 `
            -DCMAKE_BUILD_TYPE=Release `
            "-DVSP_LIBRARY_PATH=$LibsDir" `
            "-DCMAKE_INSTALL_PREFIX=$InstallDir" `
            "-DSWIG_EXECUTABLE=$Swig" `
            "-DPython3_EXECUTABLE=$Python" `
            "-DPYTHON_VERSION_SHORT=$PyVer" `
            -DVSP_NO_API_WRAPPERS=OFF -DVSP_NO_DOC=ON -DVSP_NO_PYDOC=ON
    }
}

# INSTALL builds everything, then copies vsp.exe and its resources into $InstallDir.
Invoke-Step 'Build + install TitanVSP' { cmake --build $VspDir --config Release --target INSTALL --parallel }

# Dev venv with the openvsp package installed in editable mode from the build tree,
# so rebuilt API bindings are picked up without reinstalling.
Invoke-Step 'Build Python venv' { cmake --build $VspDir --config Release --target venv }

Write-Host "Done. TitanVSP is at $InstallDir\vsp.exe" -ForegroundColor Green
Write-Host "Python API venv: $VspDir\venv\Scripts\python.exe" -ForegroundColor Green
