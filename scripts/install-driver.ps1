param(
    [Parameter(Mandatory=$true)]
    [string]$InfPath
)

$ErrorActionPreference = 'Stop'

if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run PowerShell as Administrator.'
}

$InfPath = (Resolve-Path $InfPath).Path
$devconCandidates = @(
    "$env:ProgramFiles(x86)\Windows Kits\10\Tools\x64\devcon.exe",
    "$env:ProgramFiles(x86)\Windows Kits\10\Tools\10.0.26100.0\x64\devcon.exe"
)
$devcon = $devconCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $devcon) {
    $devcon = Get-ChildItem "$env:ProgramFiles(x86)\Windows Kits\10\Tools" -Recurse -Filter devcon.exe -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\x64\\devcon\.exe$' } |
        Select-Object -First 1 -ExpandProperty FullName
}

if (-not $devcon) {
    throw 'devcon.exe was not found. Install Windows Driver Kit (WDK) Tools.'
}

Write-Host "Using: $devcon"
Write-Host "INF:   $InfPath"

& $devcon install $InfPath 'Root\AvCamVirtualCamera'
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host 'AvCam root device installation requested.'
pnputil.exe /enum-devices /class Camera
