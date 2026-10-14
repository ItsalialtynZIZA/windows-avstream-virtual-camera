$ErrorActionPreference = 'Stop'

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run PowerShell as Administrator.'
}

& bcdedit.exe /set testsigning off
if ($LASTEXITCODE -ne 0) {
    throw "BCDEdit could not disable test-signing (exit code $LASTEXITCODE)."
}

Write-Host 'Test-signing was disabled in the boot configuration. Reboot Windows to apply the change.'
