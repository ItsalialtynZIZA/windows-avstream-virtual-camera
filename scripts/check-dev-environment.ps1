$ErrorActionPreference = "Continue"

Write-Host "=== AvCam development environment ==="

Write-Host "`nWindows:"
Get-ComputerInfo | Select-Object WindowsProductName, WindowsVersion, OsBuildNumber

Write-Host "`nVisual Studio / MSBuild:"
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) {
    & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
} else {
    Write-Warning "vswhere.exe not found."
}

Write-Host "`nWindows Kits:"
Get-ChildItem "C:\Program Files (x86)\Windows Kits\10" -ErrorAction SilentlyContinue |
    Select-Object -First 20 FullName

Write-Host "`nCMake:"
cmake --version 2>$null
