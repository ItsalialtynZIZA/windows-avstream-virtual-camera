$ErrorActionPreference = 'Stop'

if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run PowerShell as Administrator.'
}

$devcon = Get-ChildItem "$env:ProgramFiles(x86)\Windows Kits\10\Tools" -Recurse -Filter devcon.exe -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match '\\x64\\devcon\.exe$' } |
    Select-Object -First 1 -ExpandProperty FullName

if (-not $devcon) {
    throw 'devcon.exe was not found. Install Windows Driver Kit (WDK) Tools.'
}

& $devcon remove 'Root\AvCamVirtualCamera'
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host 'Root device removed. DriverStore package can be removed separately with pnputil /delete-driver oemXX.inf /uninstall when required.'
