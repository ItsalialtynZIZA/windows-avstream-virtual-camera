param(
    [switch]$IUnderstandThisChangesBootTrust
)

$ErrorActionPreference = 'Stop'

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run PowerShell as Administrator.'
}

if (-not $IUnderstandThisChangesBootTrust) {
    throw 'Test-signing changes Windows boot/driver trust policy. Re-run with -IUnderstandThisChangesBootTrust only on a dedicated test machine.'
}

Write-Warning 'This changes Windows boot configuration to permit test-signed kernel drivers.'
Write-Warning 'Use it only on a dedicated development/test machine. A reboot is required.'
Write-Warning 'This helper will NOT disable Secure Boot or change firmware settings.'

& bcdedit.exe /set testsigning on
if ($LASTEXITCODE -ne 0) {
    throw "BCDEdit could not enable test-signing (exit code $LASTEXITCODE). If Secure Boot policy blocks the change, do not weaken firmware security automatically; use a dedicated driver-development test environment instead."
}

Write-Host 'Test-signing was enabled in the boot configuration. Reboot the test machine before installing/running the test driver.'
