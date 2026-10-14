# Creates desktop + Start Menu shortcuts that rebuild and launch this OpenVSP checkout.

$RunScript = Join-Path $PSScriptRoot 'run.ps1'
$Icon = Join-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) 'OpenVSP-build\install\vsp.exe'

$Shell = New-Object -ComObject WScript.Shell
$Targets = @(
    [Environment]::GetFolderPath('Desktop'),
    (Join-Path ([Environment]::GetFolderPath('Programs')) '')
)
foreach ($dir in $Targets) {
    $lnk = $Shell.CreateShortcut((Join-Path $dir 'OpenVSP (Dev).lnk'))
    $lnk.TargetPath = 'powershell.exe'
    $lnk.Arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$RunScript`""
    $lnk.WorkingDirectory = $PSScriptRoot
    $lnk.Description = 'Rebuild and launch TitanVSP from this checkout'
    if (Test-Path $Icon) { $lnk.IconLocation = "$Icon,0" }
    $lnk.Save()
    Write-Host "Created $($lnk.FullName)"
}
