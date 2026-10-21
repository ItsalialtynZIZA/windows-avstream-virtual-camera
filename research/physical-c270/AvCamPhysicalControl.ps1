# AvCam Physical C270 local manager. Windows PowerShell 5.1.
# Modifies only the named AvCam scheduled task and AvCam installation.
[CmdletBinding()]
param([ValidateSet('Status','Setup','Start','Stop','RemoveAutostart','Uninstall')][string]$Action = 'Status')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$taskName = 'AvCamPhysicalC270Bridge'
$installDir = Join-Path $env:ProgramFiles 'AvCamPhysicalC270'
$stateFile = Join-Path $installDir 'install-state.json'
$bridgeFile = Join-Path $installDir 'AvCamPhysicalBridge.exe'
$controlFile = Join-Path $installDir 'AvCamPhysicalControl.ps1'
$installFile = Join-Path $installDir 'AvCamPhysicalInstall.ps1'
$expectedClsid = '{1DE4C92F-A801-45BB-9D5A-74723A8EF460}'
. (Join-Path $PSScriptRoot 'AvCamPhysicalTaskIdentity.ps1')

function Test-Admin {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}
function Get-Task { return Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue }
function Assert-PhysicalRegistration {
    if (-not (Test-Path -LiteralPath $stateFile -PathType Leaf)) {
        throw 'AvCam Physical Device MFT is not installed. Run AvCamSetup.cmd first.'
    }
    $saved = Get-Content -LiteralPath $stateFile -Raw | ConvertFrom-Json
    if ($saved.Clsid -ine $expectedClsid -or
        $saved.RegistryPath -notmatch '(?i)^HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\DeviceClasses\\\{E5323777-F976-4F5B-9B55-B94699C46E44\}\\' -or
        $saved.RegistryPath -notmatch '(?i)VID_046D&PID_0825') {
        throw 'Install-state does not belong to the supported physical Logitech C270.'
    }
    $binding = Get-Item -LiteralPath ('Registry::' + $saved.RegistryPath) -ErrorAction Stop
    if ([string]$binding.GetValue('CameraDeviceMftClsid', '') -ine $expectedClsid) {
        throw 'AvCam Device MFT is not bound to the physical Logitech camera.'
    }
    $comPath = "Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\$expectedClsid\InprocServer32"
    $com = Get-Item -LiteralPath $comPath -ErrorAction Stop
    if (-not (Test-Path -LiteralPath ([string]$com.GetValue('', '')) -PathType Leaf)) {
        throw 'Registered AvCam Device MFT DLL is missing.'
    }
}
function Assert-TaskOwner($task) {
    if ($null -eq $task) { return }
    # Validate the executable before changing the task. A short user
    # name alone is not proof of ownership.
    $actions = @($task.Actions)
    if ($actions.Count -ne 1 -or -not $actions[0].Execute) {
        throw 'An unexpected scheduled task uses the AvCam task name. No changes made.'
    }
    $actualExe = [Environment]::ExpandEnvironmentVariables([string]$actions[0].Execute)
    if (-not [IO.Path]::IsPathRooted($actualExe) -or
        [IO.Path]::GetFullPath($actualExe) -ine [IO.Path]::GetFullPath($bridgeFile)) {
        throw 'An unrelated scheduled task already uses AvCam task name. No changes made.'
    }
    $current = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principalName = [string]$task.Principal.UserId
    if (-not $principalName -or
        -not (Test-AvCamTaskPrincipal -TaskUserId $principalName -CurrentAccount $current.Name -CurrentSid $current.User.Value)) {
        throw "AvCam task principal does not match current Windows account: $principalName. No changes made."
    }
}
function Elevate-IfNeeded {
    if (Test-Admin) { return $false }
    $ps = Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $args = '-NoProfile -ExecutionPolicy Bypass -File "{0}" -Action {1}' -f $PSCommandPath,$Action
    $proc = Start-Process -FilePath $ps -ArgumentList $args -Verb RunAs -Wait -PassThru
    if ($proc.ExitCode -ne 0) { throw "Elevated AvCam manager exited with $($proc.ExitCode)." }
    return $true
}
function Install-Shortcuts {
    $startMenu = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\AvCam Physical C270'
    New-Item -ItemType Directory -Path $startMenu -Force | Out-Null
    $shell = New-Object -ComObject WScript.Shell
    $ps = Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
    foreach ($entry in @(
        @{ Label = 'AvCam - Start'; Command = 'Start' },
        @{ Label = 'AvCam - Stop'; Command = 'Stop' },
        @{ Label = 'AvCam - Status'; Command = 'Status' },
        @{ Label = 'AvCam - Remove Autostart'; Command = 'RemoveAutostart' },
        @{ Label = 'AvCam - Uninstall'; Command = 'Uninstall' }
    )) {
        $shortcut = $shell.CreateShortcut((Join-Path $startMenu ($entry.Label + '.lnk')))
        $shortcut.TargetPath = $ps
        $shortcut.Arguments = '-NoProfile -ExecutionPolicy Bypass -NoExit -File "{0}" -Action {1}' -f $controlFile,$entry.Command
        $shortcut.WorkingDirectory = $installDir
        $shortcut.Description = "AvCam Physical C270: $($entry.Command)"
        $shortcut.Save()
    }
    Write-Host "Start Menu shortcuts: $startMenu"
}
function Show-Status {
    Write-Host '=== AVCAM PHYSICAL C270 STATUS ==='
    Write-Host ('Device MFT installed: ' + (Test-Path -LiteralPath $stateFile -PathType Leaf))
    $task = Get-Task
    if ($null -eq $task) { Write-Host 'Autostart task: absent' }
    else {
        Write-Host "Autostart task: $($task.State); user=$($task.Principal.UserId)"
        $info = Get-ScheduledTaskInfo -TaskName $taskName -ErrorAction SilentlyContinue
        if ($null -ne $info) { Write-Host "Task last result: $($info.LastTaskResult)" }
    }
    $running = @(Get-CimInstance Win32_Process -Filter "Name='AvCamPhysicalBridge.exe'" -ErrorAction SilentlyContinue)
    Write-Host "Bridge process count: $($running.Count)"
    foreach ($p in $running) { Write-Host ("PID={0}; Path={1}" -f $p.ProcessId,$p.ExecutablePath) }
    if (Test-Path -LiteralPath $stateFile) {
        try { Assert-PhysicalRegistration; Write-Host 'Physical Device MFT registration: OK' }
        catch { Write-Warning $_.Exception.Message }
    }
    Write-Host 'OBS active: bridge publishes. OBS absent: original Logitech video passes through.'
}
if ($Action -eq 'Status') { Show-Status; return }
if ($Action -in @('Setup','Start')) {
    throw 'AvCam physical MFT bridge startup is disabled until Chrome-only OBS routing is proven. Status, Stop, RemoveAutostart and Uninstall remain available.'
}
if (Elevate-IfNeeded) { return }
$existingTask = Get-Task
Assert-TaskOwner $existingTask
switch ($Action) {
    'Setup' {
        Assert-PhysicalRegistration
        foreach ($name in @('AvCamPhysicalBridge.exe','AvCamPhysicalControl.ps1','AvCamPhysicalInstall.ps1','AvCamPhysicalTaskIdentity.ps1')) {
            if (-not (Test-Path -LiteralPath (Join-Path $PSScriptRoot $name) -PathType Leaf)) {
                throw "Missing package file: $name"
            }
        }
        New-Item -ItemType Directory -Path $installDir -Force | Out-Null
        # Updating only the already-owned scheduled task is safe. Stop
        # its process before replacing the EXE; no webcam process,
        # OBS, or Deep Live Cam is terminated or suspended here.
        if ($null -ne $existingTask) {
            Stop-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
        }
        $running = @()
        for ($attempt = 0; $attempt -lt 12; $attempt++) {
            $running = @(Get-CimInstance Win32_Process -Filter "Name='AvCamPhysicalBridge.exe'" -ErrorAction SilentlyContinue)
            $installedRunning = @($running | Where-Object {
                $_.ExecutablePath -ieq $bridgeFile
            })
            if ($installedRunning.Count -eq 0) { break }
            Start-Sleep -Seconds 1
        }
        if ($installedRunning.Count -gt 0) {
            throw 'AvCam scheduled task stopped but its EXE is still in use. Retry after it exits.'
        }
        # Do not replace the Device MFT DLL currently loaded by camera services.
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'AvCamPhysicalBridge.exe') -Destination $bridgeFile -Force
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'AvCamPhysicalControl.ps1') -Destination $controlFile -Force
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'AvCamPhysicalInstall.ps1') -Destination $installFile -Force
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'AvCamPhysicalTaskIdentity.ps1') -Destination (Join-Path $installDir 'AvCamPhysicalTaskIdentity.ps1') -Force
        $account = [Security.Principal.WindowsIdentity]::GetCurrent().Name
        $principal = New-ScheduledTaskPrincipal -UserId $account -LogonType Interactive -RunLevel Highest
        $trigger = New-ScheduledTaskTrigger -AtLogOn -User $account
        $taskAction = New-ScheduledTaskAction -Execute $bridgeFile -WorkingDirectory $installDir
        $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit ([TimeSpan]::Zero) -MultipleInstances IgnoreNew -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 1) -StartWhenAvailable
        $null = Register-ScheduledTask -TaskName $taskName -Action $taskAction -Trigger $trigger -Principal $principal -Settings $settings -Description 'AvCam OBS camera watcher (Logitech C270 Media Foundation)' -Force
        Install-Shortcuts
        $external = @($running | Where-Object {
            $_.ExecutablePath -and $_.ExecutablePath -ine $bridgeFile
        })
        if ($external.Count -gt 0) {
            Write-Warning 'Another manually started AvCam Bridge is active. Close that window with Ctrl+C, then choose AvCam - Start. No process was forcibly killed.'
        } else {
            Start-ScheduledTask -TaskName $taskName
        }
        Write-Host 'AVCAM_C270_SETUP_COMPLETE'
        Show-Status
    }
    'Start' {
        Assert-PhysicalRegistration
        if ($null -eq $existingTask) { throw 'Autostart task missing. Run AvCamSetup.cmd.' }
        Start-ScheduledTask -TaskName $taskName
        Write-Host 'AVCAM_C270_STARTED_OR_ALREADY_RUNNING'
        Show-Status
    }
    'Stop' {
        if ($null -eq $existingTask) { Write-Host 'AVCAM_C270_ALREADY_STOPPED'; return }
        Stop-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
        Write-Host 'AVCAM_C270_STOPPED; camera falls back to original video.'
        Show-Status
    }
    'RemoveAutostart' {
        if ($null -ne $existingTask) {
            Stop-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
            Unregister-ScheduledTask -TaskName $taskName -Confirm:$false
        }
        Write-Host 'AVCAM_C270_AUTOSTART_REMOVED'
    }
    'Uninstall' {
        if ($null -ne $existingTask) {
            Stop-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
            Unregister-ScheduledTask -TaskName $taskName -Confirm:$false
        }
        if (Test-Path -LiteralPath $stateFile -PathType Leaf) {
            if (-not (Test-Path -LiteralPath $installFile)) {
                throw 'Rollback installer is missing. Do not delete AvCamPhysicalC270 folder.'
            }
            & $installFile -Disable
        }
        Write-Host 'AVCAM_C270_UNINSTALLED; reopen Windows Camera or reboot if needed.'
    }
}
