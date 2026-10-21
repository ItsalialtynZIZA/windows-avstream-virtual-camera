# Read-only camera-related Windows event correlation. Never changes Windows settings.
[CmdletBinding()]
param([ValidateRange(1,60)][int]$Minutes = 10)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$since = (Get-Date).AddMinutes(-$Minutes)
Write-Output 'AVCAM_C270_READONLY_EVENT_CORRELATION'
Write-Output ("SINCE_LOCAL={0:o}" -f $since)
$logs = @(Get-WinEvent -ListLog * -ErrorAction SilentlyContinue | Where-Object {
    $_.LogName -match '(?i)Camera|FrameServer|MediaFoundation'
} | Sort-Object LogName)
Write-Output ("CAMERA_RELATED_LOG_COUNT={0}" -f $logs.Count)
foreach ($log in $logs) {
    Write-Output ("LOG={0};ENABLED={1}" -f $log.LogName,$log.IsEnabled)
    if (-not $log.IsEnabled) { continue }
    try {
        $events = @(Get-WinEvent -FilterHashtable @{LogName=$log.LogName;StartTime=$since} -MaxEvents 40 -ErrorAction Stop)
        foreach ($event in $events) {
            Write-Output ("EVENT_LOG={0};UTC={1:o};ID={2};LEVEL={3};PROVIDER={4};RECORD={5}" -f $event.LogName,$event.TimeCreated.ToUniversalTime(),$event.Id,$event.LevelDisplayName,$event.ProviderName,$event.RecordId)
        }
    } catch {
        Write-Output ("LOG_READ_UNAVAILABLE={0};ERROR_TYPE={1}" -f $log.LogName,$_.Exception.GetType().Name)
    }
}
foreach ($name in @('FrameServer','FrameServerMonitor')) {
    $service = Get-CimInstance Win32_Service -Filter ("Name='{0}'" -f $name) -ErrorAction SilentlyContinue
    if ($null -ne $service) { Write-Output ("SERVICE={0};STATE={1};PID={2}" -f $service.Name,$service.State,$service.ProcessId) }
}
$proc = @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object {
    $_.Name -match '(?i)DeepLiveCam|obs64|chrome'
})
foreach ($p in $proc) { Write-Output ("APP_PROCESS={0};PID={1};PPID={2}" -f $p.Name,$p.ProcessId,$p.ParentProcessId) }
Write-Output 'NOTE: Disabled or empty logs are inconclusive and do not establish capture ownership.'
Write-Output 'NOTE: Events and loaded modules cannot by themselves identify the active OpenCV backend.'
Write-Output 'AVCAM_READONLY_COMPLETE_NO_SETTINGS_CHANGED'
