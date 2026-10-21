# AvCam C270 DShow Bridge research. Inspect is the default and is READ-ONLY.
# Enable requires an explicit confirmation and records a rollback snapshot.
# Restore requires that snapshot; never guesses an old registry value.
[CmdletBinding(DefaultParameterSetName='Inspect')]
param(
    [Parameter(ParameterSetName='Inspect')][switch]$Inspect,
    [Parameter(Mandatory,ParameterSetName='Enable')][switch]$Enable,
    [Parameter(Mandatory,ParameterSetName='Restore')][switch]$Restore,
    [Parameter(ParameterSetName='Enable')][Parameter(ParameterSetName='Restore')]
    [string]$BackupFile = (Join-Path $env:LOCALAPPDATA 'AvCam\DshowBridgeC270-backup.json')
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$property = 'EnableDshowRedirection'
$expectedDevice = 'USB\VID_046D&PID_0825&MI_00'
function Find-C270Key {
    $found = @(Get-PnpDevice -PresentOnly | Where-Object {
        $_.Class -eq 'Camera' -and
        $_.InstanceId.StartsWith($expectedDevice,[StringComparison]::OrdinalIgnoreCase)
    })
    if ($found.Count -ne 1) { throw "Expected exactly one PRESENT C270 camera interface; found $($found.Count)" }
    $id = [string]$found[0].InstanceId
    $match = [regex]::Match($id,'^USB\\([^\\]+)\\(.+)$',[Text.RegularExpressions.RegexOptions]::IgnoreCase)
    if (-not $match.Success) { throw "Unexpected C270 instance ID" }
    $reg = 'SYSTEM\CurrentControlSet\Enum\USB\' + $match.Groups[1].Value + '\' + $match.Groups[2].Value + '\Device Parameters'
    $key = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($reg, $false)
    if ($null -eq $key) { throw "Device Parameters not present for this C270 interface. No change made." }
    $key.Dispose()
    return @{ InstanceId=$id; Path=$reg }
}
function Read-Value($path) {
    $key = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($path,$false)
    if ($null -eq $key) { throw 'Device registry key disappeared' }
    try {
        $exists = ($key.GetValueNames() -contains $property)
        if (-not $exists) { return @{ Exists=$false; Value=0; Kind='Absent' } }
        return @{ Exists=$true; Value=$key.GetValue($property); Kind=([string]$key.GetValueKind($property)) }
    } finally { $key.Dispose() }
}
# Restore first: a camera can become absent after a failed trial.
# Never require Get-PnpDevice -PresentOnly or a running camera service to
# restore the original USB interface registry value.
if ($Restore) {
    if (-not (Test-Path -LiteralPath $BackupFile -PathType Leaf)) { throw 'Required backup missing; no change made' }
    $snapshot = Get-Content -LiteralPath $BackupFile -Raw | ConvertFrom-Json
    $id = [string]$snapshot.InstanceId
    $reg = [string]$snapshot.RegistryPath
    if (-not $id.StartsWith($expectedDevice,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Backup does not describe the Logitech C270 camera interface'
    }
    $expectedRegistryPath = 'SYSTEM\CurrentControlSet\Enum\' + $id + '\Device Parameters'
    if ($reg -cne $expectedRegistryPath) { throw 'Backup device registry path does not match its instance ID' }
    if ($snapshot.Kind -notin @('Absent','DWord')) { throw 'Unsupported original value type' }
    $key = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($reg,$true)
    if ($null -eq $key) { throw 'Device registry key unavailable; no changes made. Keep the backup.' }
    try {
        if ($snapshot.Kind -eq 'Absent') { $key.DeleteValue($property,$false) }
        else { $key.SetValue($property,[int]$snapshot.Value,[Microsoft.Win32.RegistryValueKind]::DWord) }
    } finally { $key.Dispose() }
    Write-Host 'RESTORED_ORIGINAL_DSHOW_VALUE'
    Write-Host ('BACKUP_RETAINED=' + $BackupFile)
    Write-Host 'Close camera apps and reconnect C270 if possible; do not delete backup yet.'
    exit 0
}
$target = Find-C270Key
$before = Read-Value $target.Path
Write-Host ('C270_INSTANCE=' + $target.InstanceId)
Write-Host ('DSHOW_REGISTRY_PATH=HKLM\' + $target.Path)
Write-Host ('DSHOW_VALUE_EXISTS=' + $before.Exists)
Write-Host ('DSHOW_VALUE_KIND=' + $before.Kind)
if ($before.Exists) { Write-Host ('DSHOW_VALUE=' + $before.Value) }
if ($PSCmdlet.ParameterSetName -eq 'Inspect') {
    Write-Host 'INSPECT_ONLY_NO_CHANGES'
    exit 0
}
# Do not modify a system with another device-MFT experiment registered.
$cameraClass = '{E5323777-F976-4F5B-9B55-B94699C46E44}'
$ifaceRoot = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey(
    ('SYSTEM\CurrentControlSet\Control\DeviceClasses\' + $cameraClass),$false)
if ($null -eq $ifaceRoot) { throw 'Camera interface registry not found' }
try {
    $encoded = $target.InstanceId.Replace('\','#')
    $iface = @($ifaceRoot.GetSubKeyNames() | Where-Object { $_.IndexOf($encoded,[StringComparison]::OrdinalIgnoreCase) -ge 0 })
    if ($iface.Count -ne 1) { throw 'Camera interface is not uniquely identified' }
    $k = $ifaceRoot.OpenSubKey(($iface[0] + '\#GLOBAL\Device Parameters'),$false)
    if ($null -eq $k) { throw 'Camera interface device parameters not found; trial refused' }
    try {
        $mft = $k.GetValue('CameraDeviceMftClsid',$null)
        if ($null -ne $mft -and [string]$mft -ne '') { throw 'CameraDeviceMftClsid is registered; do not perform bridge trial' }
        $chain = $k.GetValue('CameraDeviceMftClsidChain',$null)
        if ($null -ne $chain) { throw 'CameraDeviceMftClsidChain is registered; do not perform bridge trial' }
    } finally { $k.Dispose() }
} finally { $ifaceRoot.Dispose() }
if ($before.Kind -notin @('Absent','DWord')) { throw 'Unexpected value type; no change made' }
if (Test-Path -LiteralPath $BackupFile) { throw 'Existing rollback backup must be resolved before another trial' }
$other = @(Get-Process -ErrorAction SilentlyContinue | Where-Object {
    $_.ProcessName -match '(?i)^DeepLiveCamStudio$|^obs64$'
})
if ($other.Count -gt 0) { throw 'Close Deep Live Cam and OBS before trial. No changes made.' }
Write-Warning 'EXPERIMENTAL DEVICE REGISTRY WRITE. Camera functionality may change.'
$confirmation = Read-Host 'Type ENABLE_C270_DSHOW_BRIDGE to create a backup and enable bit 0x1'
if ($confirmation -cne 'ENABLE_C270_DSHOW_BRIDGE') { throw 'Not confirmed; no changes made' }
$folder = Split-Path -Parent $BackupFile
if (-not (Test-Path -LiteralPath $folder)) { New-Item -ItemType Directory -Path $folder -Force | Out-Null }
$snapshot = @{
    InstanceId=$target.InstanceId; RegistryPath=$target.Path
    Kind=$before.Kind; Value=$before.Value; Created=(Get-Date).ToString('o')
}
# File MUST be created before a write, otherwise there is no safe rollback.
$json = ConvertTo-Json -InputObject $snapshot -Depth 3
[System.IO.File]::WriteAllText($BackupFile,$json,(New-Object System.Text.UTF8Encoding($false)))
$key = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($target.Path,$true)
if ($null -eq $key) { throw "No write access. Backup preserved at $BackupFile" }
try {
    $previous = if ($before.Kind -eq 'Absent') { 0 } else { [int]$before.Value }
    $key.SetValue($property,($previous -bor 1),[Microsoft.Win32.RegistryValueKind]::DWord)
} finally { $key.Dispose() }
Write-Host 'DSHOW_BRIDGE_OPT_IN_WRITTEN'
Write-Host ('BACKUP=' + $BackupFile)
Write-Host 'Close camera apps and reconnect C270 before testing. No driver was installed.'
Write-Host 'Rollback: powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\AvCamDshowBridgeTrial.ps1 -Restore'
