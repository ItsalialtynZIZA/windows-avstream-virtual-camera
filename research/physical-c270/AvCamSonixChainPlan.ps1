# Read-only chain-aware planning for Lenovo 13D3:56FF with Sonix OEM DMFT.
# No COM registration, registry writes, camera activation, or installation flags.
[CmdletBinding()]
param([switch]$SelfTest)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$sonix = '{5A50829A-86DD-4D18-8685-891EEE643C24}'
$avcam = '{1DE4C92F-A801-45BB-9D5A-74723A8EF460}'
$category = '{E5323777-F976-4F5B-9B55-B94699C46E44}'

function New-AvCamChainPlan {
    param([string[]]$Chain, [int]$Build, [string]$OEM, [string]$Extension)
    if ($Build -lt 22621) { throw 'Windows 11 22H2 or newer required' }
    if ($Chain.Count -ne 1) {
        throw "Unexpected OEM chain length $($Chain.Count); changes forbidden"
    }
    $o = [guid]::Empty
    $e = [guid]::Empty
    $current = [guid]::Empty
    if (-not [guid]::TryParseExact($OEM, 'B', [ref]$o)) { throw 'Bad OEM CLSID' }
    if (-not [guid]::TryParseExact($Extension, 'B', [ref]$e)) { throw 'Bad AvCam CLSID' }
    if (-not [guid]::TryParseExact($Chain[0], 'B', [ref]$current)) {
        throw 'Bad existing CLSID'
    }
    if ($current -ne $o -or $current -eq $e) {
        throw 'Unexpected OEM MFT; refusing chain plan'
    }
    # First CLSID is closest to DevProxy; keep OEM transform first.
    $original = [string[]]@($Chain[0])
    $candidate = [string[]]@($Chain[0], $Extension)
    if ($candidate.Count -gt 4) { throw 'DMFT chain exceeds Windows 11 limit' }
    return [pscustomobject]@{
        Original = $original
        Candidate = $candidate
        Rollback = $original
        MayModifyRegistry = $false
        PinInteropVerified = $false
    }
}
if ($SelfTest) {
    $ok = New-AvCamChainPlan -Chain ([string[]]@($sonix)) -Build 26300 -OEM $sonix -Extension $avcam
    if ($ok.Candidate.Count -ne 2 -or $ok.Candidate[0] -cne $sonix -or
        $ok.Candidate[1] -cne $avcam -or $ok.Rollback[0] -cne $sonix -or
        $ok.MayModifyRegistry -or $ok.PinInteropVerified) {
        throw 'Valid plan invariant failed'
    }
    $cases = [System.Collections.Generic.List[string[]]]::new()
    $cases.Add([string[]]@())
    $cases.Add([string[]]@($avcam))
    $cases.Add([string[]]@($sonix, $avcam))
    $cases.Add([string[]]@('not-a-guid'))
    $cases.Add([string[]]@('{00000000-0000-0000-0000-000000000001}'))
    foreach ($case in $cases) {
        $rejected = $false
        try {
            $null = New-AvCamChainPlan -Chain $case -Build 26300 -OEM $sonix -Extension $avcam
        }
        catch { $rejected = $true }
        if (-not $rejected) { throw 'Unsafe OEM chain case accepted' }
    }
    $rejectedOld = $false
    try {
        $null = New-AvCamChainPlan -Chain ([string[]]@($sonix)) -Build 19045 -OEM $sonix -Extension $avcam
    }
    catch { $rejectedOld = $true }
    if (-not $rejectedOld) { throw 'Unsupported Windows build accepted' }
    Write-Host 'AVCAM_SONIX_CHAIN_PLAN_SELFTEST_PASS'
    return
}

if (-not [Environment]::Is64BitProcess) { throw '64-bit PowerShell required' }
if (-not (Get-Command Get-PnpDevice -ErrorAction SilentlyContinue)) {
    throw 'PnP module unavailable'
}
$build = [int](Get-CimInstance Win32_OperatingSystem).BuildNumber
$camera = @(Get-PnpDevice -PresentOnly | Where-Object {
    $_.Class -eq 'Camera' -and $_.Status -eq 'OK' -and
    $_.FriendlyName -eq 'Integrated Camera' -and
    $_.InstanceId -match '(?i)^USB\\VID_13D3&PID_56FF&MI_00\\'
})
if ($camera.Count -ne 1) { throw 'Expected exactly one healthy Integrated Camera 13D3:56FF' }

$root = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
    [Microsoft.Win32.RegistryHive]::LocalMachine,
    [Microsoft.Win32.RegistryView]::Registry64
)
try {
    $base = "SYSTEM\CurrentControlSet\Control\DeviceClasses\$category"
    $classes = $root.OpenSubKey($base, $false)
    if ($null -eq $classes) { throw 'VIDEO_CAMERA registry root unavailable' }
    try {
        $encoded = $camera[0].InstanceId.Replace('\', '#')
        $matches = @(
            foreach ($name in $classes.GetSubKeyNames()) {
                if ($name.IndexOf($encoded, [StringComparison]::OrdinalIgnoreCase) -lt 0 -or
                    -not $name.EndsWith("#$category", [StringComparison]::OrdinalIgnoreCase)) {
                    continue
                }
                $relative = "$name\#GLOBAL\Device Parameters"
                $candidate = $classes.OpenSubKey($relative, $false)
                if ($null -ne $candidate) {
                    $candidate.Dispose()
                    "$base\$relative"
                }
            }
        )
    }
    finally { $classes.Dispose() }
    if ($matches.Count -ne 1) { throw 'Camera interface is not unique' }
    $key = $root.OpenSubKey($matches[0], $false)
    if ($null -eq $key) { throw 'Camera interface registry key missing' }
    try {
        $names = @($key.GetValueNames())
        if (($names -contains 'CameraDeviceMftClsid') -and
            $key.GetValue('CameraDeviceMftClsid', $null)) {
            throw 'Legacy Device MFT present; OEM baseline changed'
        }
        if ($names -notcontains 'CameraDeviceMftClsidChain') {
            throw 'OEM DMFT chain not found'
        }
        if ($key.GetValueKind('CameraDeviceMftClsidChain') -ne
            [Microsoft.Win32.RegistryValueKind]::MultiString) {
            throw 'OEM DMFT chain is not REG_MULTI_SZ'
        }
        $originalChain = [string[]]$key.GetValue('CameraDeviceMftClsidChain', [string[]]@())
    }
    finally { $key.Dispose() }

    $plan = New-AvCamChainPlan -Chain $originalChain -Build $build -OEM $sonix -Extension $avcam
    Write-Host "AVCAM_SONIX_PLAN_OS_BUILD=$build"
    Write-Host "AVCAM_SONIX_PLAN_ORIGINAL_COUNT=$($plan.Original.Count)"
    Write-Host "AVCAM_SONIX_PLAN_ORIGINAL_0=$($plan.Original[0])"
    Write-Host "AVCAM_SONIX_PLAN_CANDIDATE_COUNT=$($plan.Candidate.Count)"
    Write-Host "AVCAM_SONIX_PLAN_CANDIDATE_0=$($plan.Candidate[0])"
    Write-Host "AVCAM_SONIX_PLAN_CANDIDATE_1=$($plan.Candidate[1])"
    Write-Host 'AVCAM_SONIX_PLAN_ROLLBACK=RESTORE_EXACT_ORIGINAL_CHAIN'
    Write-Host 'AVCAM_SONIX_PIN_INTEROP=NOT_VERIFIED'
    Write-Host 'AVCAM_SONIX_CHAIN_INSTALL=BLOCKED'
    Write-Host 'AVCAM_SONIX_CHAIN_PLAN_READ_ONLY_COMPLETE'
}
finally { $root.Dispose() }
