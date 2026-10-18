<#
.SYNOPSIS
    Capture-and-ETW acceptance gate for an already installed AvCam driver.
.DESCRIPTION
    Never installs drivers or removes the root bus. Requires the child camera
    to be absent at start. Plugging, MJPEG upload, capture, and unplug are
    performed in one bounded run, always unplugging in finally.
#>
param(
    [Parameter(Mandatory = $true)] [string]$ControllerPath,
    [Parameter(Mandatory = $true)] [string]$ProbePath,
    [Parameter(Mandatory = $true)] [string]$JpegPath,
    [string]$OutputDirectory = "$env:PUBLIC\AvCamValidation"
)

$ErrorActionPreference = 'Stop'

$principal = [Security.Principal.WindowsPrincipal]::new(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this acceptance check from elevated PowerShell.'
}

$controller = (Resolve-Path -LiteralPath $ControllerPath).Path
$probe = (Resolve-Path -LiteralPath $ProbePath).Path
$jpeg = (Resolve-Path -LiteralPath $JpegPath).Path
$session = "AvCamAcceptance_$PID"
$destination = Join-Path $OutputDirectory (Get-Date -Format 'yyyyMMdd-HHmmss')
New-Item -ItemType Directory -Path $destination -Force | Out-Null

$bus = Get-PnpDevice -PresentOnly |
    Where-Object { $_.InstanceId -like 'ROOT\AVCAMVIRTUALUSBHOST*' } |
    Select-Object -First 1
if (-not $bus -or $bus.Status -ne 'OK') {
    throw 'AvCam root bus is absent or not in PnP OK state.'
}
$child = Get-PnpDevice -PresentOnly |
    Where-Object { $_.InstanceId -like 'USB\VID_1234&PID_5678*' } |
    Select-Object -First 1
if ($child) {
    throw 'AvCam child must be unplugged before acceptance test; no existing camera will be disturbed.'
}

$providers = Join-Path $destination 'providers.txt'
@(
    'Microsoft-Windows-USBVideo',
    'Microsoft-Windows-MF-FrameServer',
    'Microsoft-Windows-MediaFoundation-MFReadWrite',
    'Microsoft-Windows-MediaFoundation-Platform'
) | Set-Content -LiteralPath $providers -Encoding Ascii

$sessionCreated = $false
$sessionStarted = $false
$plugged = $false
try {
    & logman create trace $session -pf $providers -o (Join-Path $destination 'capture.etl') -f bincirc -max 64
    if ($LASTEXITCODE -ne 0) { throw 'Could not create isolated ETW session.' }
    $sessionCreated = $true
    & logman start $session
    if ($LASTEXITCODE -ne 0) { throw 'Could not start ETW capture.' }
    $sessionStarted = $true

    & $controller plug
    if ($LASTEXITCODE -ne 0) { throw 'AvCam plug command failed.' }
    $plugged = $true

    $ready = $false
    for ($attempt = 0; $attempt -lt 20; ++$attempt) {
        Start-Sleep -Milliseconds 500
        $child = Get-PnpDevice -PresentOnly |
            Where-Object { $_.InstanceId -like 'USB\VID_1234&PID_5678*' } |
            Select-Object -First 1
        if ($child -and $child.Status -eq 'OK') { $ready = $true; break }
    }
    if (-not $ready) { throw 'USB child did not reach PnP OK within 10 seconds.' }

    & $controller frame $jpeg
    if ($LASTEXITCODE -ne 0) { throw 'MJPEG feeder upload failed.' }

    $stdout = Join-Path $destination 'mf-probe.txt'
    $stderr = Join-Path $destination 'mf-errors.txt'
    $proc = Start-Process -FilePath $probe -PassThru -NoNewWindow `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    if (-not $proc.WaitForExit(25000)) {
        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
        throw 'Media Foundation probe exceeded the 25-second process timeout.'
    }
    $exitCode = $proc.ExitCode
    $probeText = Get-Content -LiteralPath $stdout -Raw
    Write-Host $probeText
    if ($exitCode -ne 0 -or
        $probeText -notmatch 'MEDIA_FOUNDATION_SAMPLE_RECEIVED') {
        throw "Media Foundation never delivered a nonempty MJPG sample (probe exit $exitCode)."
    }

    Write-Host 'ACCEPTANCE: MJPG sample delivered to Media Foundation.'
}
finally {
    if ($sessionStarted) {
        & logman stop $session | Out-Host
    }
    if ($sessionCreated) {
        & logman delete $session | Out-Host
    }
    if ($plugged) {
        # Capture ISO timing/counters even when the MF probe times out.
        # Never let a diagnostic read prevent the mandatory unplug.
        try {
            & $controller stats | Tee-Object -FilePath (
                Join-Path $destination 'driver-stats.txt')
            if ($LASTEXITCODE -ne 0) {
                Write-Warning 'Could not retrieve driver statistics.'
            }
        }
        catch {
            Write-Warning "Driver stats capture failed: $_"
        }
        & $controller unplug | Out-Host
        if ($LASTEXITCODE -ne 0) {
            Write-Warning 'Automatic unplug failed: inspect PnP child status before retrying.'
        }
    }
    Write-Host "Diagnostic files: $destination"
}
