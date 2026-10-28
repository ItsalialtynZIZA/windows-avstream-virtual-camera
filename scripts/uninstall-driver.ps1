# Remove ONLY the separate Root\AvCamVirtualCamera test endpoint.
# Never touches physical USB cameras or the Sonix OEM Device MFT chain.
$ErrorActionPreference = 'Stop'

if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run PowerShell as Administrator.'
}

$package = Split-Path -Parent $PSScriptRoot
$inf = Get-ChildItem -LiteralPath $package -Recurse -File -Filter 'AvCam.inf' |
    Select-Object -First 1
$native = Get-ChildItem -LiteralPath $package -Recurse -File -Filter 'AvCamRootInstall.exe' |
    Select-Object -First 1

if ($native -and $inf) {
    & $native.FullName remove $inf.FullName
    if ($LASTEXITCODE -ne 0) {
        throw "AvCamRootInstall remove failed ($LASTEXITCODE). No other devices were targeted."
    }
    Write-Host 'AVCAM_SEPARATE_ROOT_REMOVAL_REQUESTED'
    Write-Host 'Restart Windows if the AvCam root device remains present.'
    return
}

# Compatibility path for older packages: DevCon must already be installed.
$toolsRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Tools'
$devcon = if (Test-Path -LiteralPath $toolsRoot) {
    Get-ChildItem -LiteralPath $toolsRoot -Recurse -Filter devcon.exe -File -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -like '*\x64\devcon.exe' } |
        Select-Object -First 1 -ExpandProperty FullName
}
if (-not $devcon) {
    throw 'Neither the packaged AvCam root removal tool nor WDK DevCon is present.'
}
& $devcon remove 'Root\AvCamVirtualCamera'
if ($LASTEXITCODE -ne 0) {
    throw "DevCon removal failed with exit code $LASTEXITCODE."
}
Write-Host 'AVCAM_SEPARATE_ROOT_REMOVAL_REQUESTED'
Write-Host 'The driver-store package can be removed separately if needed.'
