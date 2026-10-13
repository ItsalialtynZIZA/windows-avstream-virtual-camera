param(
    [ValidateSet('Status', 'EnableOneBoot', 'Reset')]
    [string]$Action = 'Status',

    [switch]$IUnderstandThisCanCrashTheTestMachine
)

$ErrorActionPreference = 'Stop'

function Test-IsAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Invoke-Verifier([string[]]$Arguments) {
    & "$env:WINDIR\System32\verifier.exe" @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "verifier.exe failed with exit code ${LASTEXITCODE}: $($Arguments -join ' ')"
    }
}

if (-not (Test-IsAdministrator)) {
    throw 'Run PowerShell as Administrator.'
}

$driverPath = Join-Path $env:WINDIR 'System32\drivers\AvCam.sys'

switch ($Action) {
    'Status' {
        Write-Host '=== Driver Verifier configured settings ==='
        Invoke-Verifier @('/querysettings')

        Write-Host ''
        Write-Host '=== Driver Verifier current activity ==='
        Invoke-Verifier @('/query')
        break
    }

    'EnableOneBoot' {
        if (-not $IUnderstandThisCanCrashTheTestMachine) {
            throw 'Driver Verifier can intentionally bugcheck Windows. Re-run with -IUnderstandThisCanCrashTheTestMachine only on a dedicated test VM/machine.'
        }

        if (-not (Test-Path $driverPath)) {
            throw "AvCam.sys was not found at '$driverPath'. Install and verify the driver first."
        }

        Write-Warning 'Driver Verifier can deliberately crash Windows when it detects a driver violation.'
        Write-Warning 'Use this only on a disposable/dedicated test VM or test machine, with recovery access available.'
        Write-Warning 'This command targets AvCam.sys only; do not switch it to /all.'

        Invoke-Verifier @('/standard', '/driver', 'AvCam.sys')
        Invoke-Verifier @('/bootmode', 'oneboot')

        Write-Host ''
        Write-Host 'Driver Verifier is configured for AvCam.sys for the next boot.'
        Write-Host 'Review the settings below, then reboot the TEST machine when ready:'
        Invoke-Verifier @('/querysettings')
        Write-Host ''
        Write-Host 'After reboot, exercise Windows Camera, the test feeder, OBS bridge, repeated open/close, and device disable/enable.'
        Write-Host 'If Windows bugchecks, collect the dump and analyze it with WinDbg before changing the driver.'
        break
    }

    'Reset' {
        if (-not $IUnderstandThisCanCrashTheTestMachine) {
            throw 'Reset clears all persistent Driver Verifier settings on this machine. Re-run with -IUnderstandThisCanCrashTheTestMachine on the dedicated test machine.'
        }

        Invoke-Verifier @('/reset')
        Write-Host 'Persistent Driver Verifier settings were reset. Reboot to ensure verification is disabled.'
        break
    }
}
