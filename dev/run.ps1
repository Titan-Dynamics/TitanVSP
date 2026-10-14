# One-click entry point: incrementally rebuilds OpenVSP from this repo, then launches it.
# Pass -NoBuild to skip the build and launch the last successful build.

param([switch]$NoBuild)

$InstallDir = Join-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) 'OpenVSP-build\install'

if (-not $NoBuild) {
    try {
        & (Join-Path $PSScriptRoot 'build.ps1')
    } catch {
        Write-Host $_ -ForegroundColor Red
        Read-Host 'Build failed. Press Enter to close'
        exit 1
    }
}

$Exe = Join-Path $InstallDir 'vsp.exe'
if (-not (Test-Path $Exe)) {
    Read-Host "No build found at $Exe. Press Enter to close"
    exit 1
}
Start-Process -FilePath $Exe -WorkingDirectory $InstallDir
