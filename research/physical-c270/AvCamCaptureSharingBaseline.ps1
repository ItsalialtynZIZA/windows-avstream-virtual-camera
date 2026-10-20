# Read-only baseline for physical C270 camera sharing.
# Run WHILE Deep Live Cam is displaying live physical Logitech frames,
# WITHOUT AvCam Device MFT enabled or the physical Bridge running.
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Write-Host '=== AVCAM PHYSICAL C270 SHARING BASELINE (READ ONLY) ==='
$dev = @(Get-PnpDevice -PresentOnly | Where-Object {
    $_.InstanceId -match '(?i)^USB\\VID_046D&PID_0825' -and
    $_.Class -match '(?i)Camera|Image'
})
Write-Host ('C270_PRESENT=' + $dev.Count)
$dev | Format-Table Status,Class,FriendlyName,InstanceId -AutoSize | Out-Host
$task = Join-Path $env:ProgramFiles 'AvCamPhysicalC270\install-state.json'
Write-Host ('AVCAM_INSTALL_STATE_EXISTS=' + (Test-Path -LiteralPath $task))
foreach ($name in @('FrameServer','FrameServerMonitor')) {
    $svc = Get-Service -Name $name -ErrorAction SilentlyContinue
    if ($null -eq $svc) { Write-Host ("SERVICE_{0}=MISSING" -f $name) }
    else { Write-Host ("SERVICE_{0}={1};START={2}" -f $name,$svc.Status,$svc.StartType) }
}
foreach ($view in @('Registry64','Registry32')) {
    $hive = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
        [Microsoft.Win32.RegistryHive]::LocalMachine,
        [Enum]::Parse([Microsoft.Win32.RegistryView], $view))
    try {
        $key = $hive.OpenSubKey('SOFTWARE\Microsoft\Windows Media Foundation\Platform',$false)
        $value = if ($null -ne $key) { $key.GetValue('EnableFrameServerMode',$null) } else { $null }
        Write-Host ("FRAME_SERVER_OVERRIDE_{0}={1}" -f $view, $(if($null -eq $value){'<absent>'}else{$value}))
        if ($null -ne $key) { $key.Dispose() }
    } finally { $hive.Dispose() }
}
Write-Host '=== CAMERA RELATED PROCESSES ==='
Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
    Where-Object {
        $_.Name -match '(?i)camera|obs|python|deep|chrome|frameserver'
    } |
    Select-Object ProcessId,Name,ExecutablePath |
    Format-Table -AutoSize | Out-Host
Write-Host 'NOTE: Process and service listings cannot alone prove who owns the KS capture pin.'
Write-Host 'BASELINE_COMPLETE_NO_SETTINGS_CHANGED'
