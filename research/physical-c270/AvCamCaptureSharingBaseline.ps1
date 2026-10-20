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
# Query the actual physical camera interface registration, not just the
# installer state file (which may be absent after a manual rollback).
Write-Host '=== PHYSICAL C270 DEVICE MFT REGISTRATION (READ ONLY) ==='
$cameraClass = '{E5323777-F976-4F5B-9B55-B94699C46E44}'
$expectedMft = '{1DE4C92F-A801-45BB-9D5A-74723A8EF460}'
$rootKey = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey(
    ("SYSTEM\CurrentControlSet\Control\DeviceClasses\{0}" -f $cameraClass), $false)
if ($null -eq $rootKey) {
    Write-Host 'C270_CAMERA_INTERFACE_REGISTRY=NOT_FOUND'
} else {
    try {
        foreach ($device in $dev) {
            $encoded = $device.InstanceId.Replace('\','#')
            $names = @($rootKey.GetSubKeyNames() | Where-Object {
                $_.IndexOf($encoded,[StringComparison]::OrdinalIgnoreCase) -ge 0
            })
            Write-Host ("C270_MATCHED_CAMERA_INTERFACES={0}" -f $names.Count)
            foreach ($name in $names) {
                $settings = $rootKey.OpenSubKey(($name + '\#GLOBAL\Device Parameters'), $false)
                if ($null -eq $settings) { continue }
                try {
                    $actual = $settings.GetValue('CameraDeviceMftClsid',$null)
                    $chain = $settings.GetValue('CameraDeviceMftClsidChain',$null)
                    Write-Host ("C270_DEVICE_MFT_CLSID={0}" -f $(if($null -eq $actual){'<absent>'}else{$actual}))
                    Write-Host ("C270_DEVICE_MFT_CHAIN={0}" -f $(if($null -eq $chain){'<absent>'}else{($chain -join ',')}))
                    Write-Host ("C270_AVCAM_MFT_ACTIVE={0}" -f ($actual -ieq $expectedMft))
                } finally { $settings.Dispose() }
            }
        }
    } finally { $rootKey.Dispose() }
}
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
Write-Host '=== DEEP LIVE CAM LOADED CAMERA MODULES (BEST EFFORT) ==='
$deep = @(Get-Process -ErrorAction SilentlyContinue | Where-Object {
    $_.ProcessName -match '(?i)DeepLiveCam'
})
foreach ($process in $deep) {
    Write-Host ("DEEP_PROCESS={0};PID={1}" -f $process.ProcessName, $process.Id)
    try {
        $loaded = @($process.Modules | Where-Object {
            $_.ModuleName -match '(?i)opencv_videoio|opencv_world|mfplat|mfreadwrite|mf.dll|quartz|ksproxy|devenum|qedit|vidcap|dshow|ksuser'
        } | Select-Object ModuleName,FileName)
        if ($loaded.Count -eq 0) { Write-Host 'DEEP_CAMERA_MODULES=NONE_VISIBLE' }
        else { $loaded | Format-Table -AutoSize | Out-Host }
    } catch { Write-Host ("DEEP_MODULE_SCAN_ERROR={0}" -f $_.Exception.Message) }
}
Write-Host 'NOTE: Loaded modules indicate available backends, not necessarily the active capture backend.'
Write-Host '=== DSHOW BRIDGE AND CAMERA FRAME SERVER MODULES (BEST EFFORT) ==='
# A process loading a bridge DLL does not prove that it routes camera frames,
# but comparing both hosts can rule out some proposed configurations.
$all = @(Get-Process -ErrorAction SilentlyContinue | Where-Object {
    $_.ProcessName -match '(?i)DeepLiveCam|svchost|FrameServer'
})
foreach ($proc in $all) {
    try {
        $matches = @($proc.Modules | Where-Object {
            $_.ModuleName -match '(?i)dshowbridge|ksproxy|mfsensorgroup|mfcaptureengine|mfreadwrite|quartz'
        } | Select-Object -ExpandProperty ModuleName -Unique)
        if ($matches.Count -gt 0) {
            Write-Host ("CAMERA_HOST_MODULES_PID={0};NAME={1};DLLS={2}" -f $proc.Id,$proc.ProcessName,($matches -join ','))
        }
    } catch {
        # Some protected Windows service processes deny module enumeration.
    }
}
Write-Host 'NOTE: This scan is informational; a DLL loaded into a process does not prove capture ownership.'
Write-Host 'NOTE: Service processes can be protected and inaccessible; missing module results are inconclusive.'
Write-Host 'NEXT: For conclusive backend selection capture the actual OpenCV backend name or trace during live operation.'
Write-Host 'BASELINE_COMPLETE_NO_SETTINGS_CHANGED'
