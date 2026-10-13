param(
    [string]$FeederPath = '.\AvCamFeeder.exe',
    [int]$Seconds = 8
)

$ErrorActionPreference = 'Stop'

if ($Seconds -lt 3 -or $Seconds -gt 60) {
    throw 'Seconds must be between 3 and 60.'
}

Write-Host '=== AvCam runtime smoke check ==='

$camera = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
    Where-Object {
        $_.FriendlyName -eq 'AvCam Virtual Camera' -or
        $_.InstanceId -like 'ROOT\AVCAMVIRTUALCAMERA*'
    } |
    Select-Object -First 1

if (-not $camera) {
    throw 'AvCam Virtual Camera is not currently enumerated. Install/start the driver first.'
}

Write-Host "PnP device: $($camera.FriendlyName)"
Write-Host "Status: $($camera.Status)"
Write-Host "InstanceId: $($camera.InstanceId)"

if ($camera.Status -ne 'OK') {
    throw "AvCam PnP status is '$($camera.Status)', expected 'OK'."
}

if (-not (Test-Path $FeederPath)) {
    throw "Feeder executable was not found: $FeederPath"
}

$resolvedFeeder = (Resolve-Path $FeederPath).Path

Write-Host ''
Write-Host 'Enumerating Media Foundation camera sources...'
& $resolvedFeeder --list-sources
if ($LASTEXITCODE -ne 0) {
    throw "AvCamFeeder --list-sources failed with exit code $LASTEXITCODE."
}

$stdout = Join-Path $env:TEMP "avcam-smoke-$PID-out.log"
$stderr = Join-Path $env:TEMP "avcam-smoke-$PID-err.log"

Remove-Item $stdout, $stderr -Force -ErrorAction SilentlyContinue

Write-Host ''
Write-Host "Starting deterministic feeder source for $Seconds second(s)..."
$process = Start-Process -FilePath $resolvedFeeder `
    -ArgumentList @('--source', 'test') `
    -PassThru `
    -WindowStyle Hidden `
    -RedirectStandardOutput $stdout `
    -RedirectStandardError $stderr

try {
    Start-Sleep -Seconds $Seconds

    if ($process.HasExited) {
        $outText = if (Test-Path $stdout) { Get-Content $stdout -Raw } else { '' }
        $errText = if (Test-Path $stderr) { Get-Content $stderr -Raw } else { '' }
        Write-Host $outText
        if ($errText) { Write-Error $errText }
        throw "AvCamFeeder exited early with code $($process.ExitCode)."
    }
}
finally {
    if (-not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
        $process.WaitForExit()
    }
}

$outText = if (Test-Path $stdout) { Get-Content $stdout -Raw } else { '' }
$errText = if (Test-Path $stderr) { Get-Content $stderr -Raw } else { '' }

Write-Host ''
Write-Host '=== Feeder output ==='
Write-Host $outText

if ($errText) {
    Write-Host '=== Feeder stderr ==='
    Write-Host $errText
}

$requiredSignals = @(
    'Frame source started:',
    'Streaming 1280x720 BGRA32 frames.'
)

foreach ($signal in $requiredSignals) {
    if ($outText -notmatch [regex]::Escape($signal)) {
        throw "Smoke check did not observe expected feeder signal: $signal"
    }
}

if ($outText -notmatch 'stats:\s+submitted=') {
    Write-Warning 'No transport stats line was observed. The run may have been too short or the filter transport may not yet be active.'
}

Write-Host ''
Write-Host 'Smoke check passed: AvCam enumerated and the test feeder remained active.'
Write-Host 'For full validation, keep Windows Camera open and confirm capture_reads increases in feeder stats.'
