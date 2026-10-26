# Read-only readiness gate for a TWO-PHYSICAL-CAMERA red-frame experiment.
# Does not modify Logitech, OBS, Windows camera services, COM registration,
# registry, signing policy, drivers or browser settings.
[CmdletBinding()]
param(
    [switch]$SelfTest,
    [switch]$CheckObsIpc,
    [string]$FeederPath = 'C:\AvCam-Second\AvCamFeeder.exe'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$logitechId = 'USB\VID_046D&PID_0825'
$internalId = 'USB\VID_2B7E&PID_B685'
$cameraGuid = '{E5323777-F976-4F5B-9B55-B94699C46E44}'

function Evaluate-CameraPairs {
    param([object[]]$Devices)
    $logitech = @($Devices | Where-Object {
        $_.InstanceId -like "$logitechId*" -and $_.Class -in @('Camera', 'Image')
    })
    $internal = @($Devices | Where-Object {
        $_.InstanceId -like "$internalId*" -and $_.Class -in @('Camera', 'Image')
    })
    $ok = $logitech.Count -eq 1 -and $internal.Count -eq 1 -and
        $logitech[0].Status -eq 'OK' -and $internal[0].Status -eq 'OK' -and
        $logitech[0].InstanceId -ne $internal[0].InstanceId
    return [pscustomobject]@{
        Ready = [bool]$ok
        LogitechCount = $logitech.Count
        InternalCount = $internal.Count
        LogitechStatus = if ($logitech.Count -eq 1) { $logitech[0].Status } else { 'MISSING_OR_AMBIGUOUS' }
        InternalStatus = if ($internal.Count -eq 1) { $internal[0].Status } else { 'MISSING_OR_AMBIGUOUS' }
        LogitechInstanceId = if ($logitech.Count -eq 1) { $logitech[0].InstanceId } else { $null }
    }
}

function Get-LogitechMftStatus {
    param([string]$InstanceId)
    # Fail closed: unknown registry state is NOT evidence of a safe baseline.
    if ([string]::IsNullOrWhiteSpace($InstanceId)) { return 'UNKNOWN' }
    $subpath = "SYSTEM\CurrentControlSet\Control\DeviceClasses\$cameraGuid"
    $root = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($subpath, $false)
    if ($null -eq $root) { return 'UNKNOWN' }
    try {
        $encoded = $InstanceId.Replace('\', '#')
        $matches = @($root.GetSubKeyNames() | Where-Object {
            $_.IndexOf($encoded, [StringComparison]::OrdinalIgnoreCase) -ge 0 -and
            $_.EndsWith(('#' + $cameraGuid), [StringComparison]::OrdinalIgnoreCase)
        })
        if ($matches.Count -ne 1) { return 'UNKNOWN' }
        $parameters = $root.OpenSubKey(($matches[0] + '\#GLOBAL\Device Parameters'), $false)
        if ($null -eq $parameters) { return 'UNKNOWN' }
        try {
            $legacy = $parameters.GetValue('CameraDeviceMftClsid', $null)
            $chain = $parameters.GetValue('CameraDeviceMftClsidChain', $null)
            if ($null -ne $legacy -and [string]$legacy -ne '') { return 'PRESENT' }
            if ($null -ne $chain -and @($chain).Count -gt 0 -and
                ((@($chain) | Where-Object { [string]$_ -ne '' }).Count -gt 0)) {
                return 'PRESENT'
            }
            return 'ABSENT'
        } finally {
            $parameters.Dispose()
        }
    } finally {
        $root.Dispose()
    }
}

if ($SelfTest) {
    function Assert([bool]$Value, [string]$Name) {
        if (-not $Value) { throw "AVCAM_DUAL_CAMERA_TEST_FAILED=$Name" }
    }
    $valid = @(
        [pscustomobject]@{ InstanceId='USB\VID_046D&PID_0825&MI_00\A'; Class='Camera'; Status='OK' },
        [pscustomobject]@{ InstanceId='USB\VID_2B7E&PID_B685&MI_00\B'; Class='Camera'; Status='OK' }
    )
    Assert (Evaluate-CameraPairs $valid).Ready 'two-present-healthy-devices'
    Assert (-not (Evaluate-CameraPairs @($valid[0])).Ready) 'reject-missing-built-in'
    $bad = @(
        $valid[0],
        [pscustomobject]@{ InstanceId=$valid[1].InstanceId; Class='Camera'; Status='Error' }
    )
    Assert (-not (Evaluate-CameraPairs $bad).Ready) 'reject-unhealthy-camera'
    Assert (-not (Evaluate-CameraPairs @($valid[0],$valid[0],$valid[1])).Ready) 'reject-duplicate-logitech'
    Assert (-not (Evaluate-CameraPairs @()).Ready) 'reject-empty'
    Write-Host 'AVCAM_DUAL_CAMERA_PREFLIGHT_SELFTEST=PASS'
    return
}

Write-Host '=== AvCam dual physical camera / read-only preflight ==='
$devices = @(Get-PnpDevice -PresentOnly -ErrorAction Stop |
    Where-Object { $_.Class -in @('Camera','Image') })
$pair = Evaluate-CameraPairs $devices
Write-Host "AVCAM_LOGITECH_C270_COUNT=$($pair.LogitechCount) status=$($pair.LogitechStatus)"
Write-Host "AVCAM_BUILTIN_CAMERA_COUNT=$($pair.InternalCount) status=$($pair.InternalStatus)"
$mft = 'UNKNOWN'
if ($pair.LogitechInstanceId) {
    try {
        $mft = Get-LogitechMftStatus $pair.LogitechInstanceId
    } catch {
        Write-Warning 'Camera interface registry could not be read. Treating MFT state as unknown.'
    }
}
Write-Host "AVCAM_LOGITECH_MFT_STATE=$mft"
Write-Host 'AVCAM_CAMERA_ORIGINAL_PNP_ID_UNCHANGED=NOT_MODIFIED_BY_THIS_SCRIPT'
Write-Host 'AVCAM_DEEPLIVECAM_BUILTIN_SOURCE=NOT_VERIFIABLE_FROM_PNP'
$ready = $pair.Ready -and $mft -eq 'ABSENT'
if ($CheckObsIpc) {
    if (-not (Test-Path -LiteralPath $FeederPath -PathType Leaf)) {
        Write-Warning "Feeder not found: $FeederPath"
        $ready = $false
    } else {
        Write-Host '=== Read-only OBS IPC sample check (does not submit to a camera) ==='
        & $FeederPath --source avcam-ipc-obs --check-source --seconds 3
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "OBS IPC sample check failed (exit $LASTEXITCODE)"
            $ready = $false
        }
    }
}
if ($ready) {
    Write-Host 'AVCAM_DUAL_CAMERA_PREFLIGHT=PASS'
    Write-Host 'Next: test a solid-red OBS scene in AvCam preview; no Logitech frame replacement is enabled.'
    exit 0
}
Write-Host 'AVCAM_DUAL_CAMERA_PREFLIGHT=BLOCKED'
Write-Host 'No camera/driver settings were changed. Investigate before any device-scoped extension.'
exit 6
