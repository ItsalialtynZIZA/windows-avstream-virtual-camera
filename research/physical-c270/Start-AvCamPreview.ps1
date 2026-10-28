# No-install AvCam preview launcher. Does not change drivers or camera endpoints.
# Starts only user-mode RAW/OBS router and a separate IPC video viewer.
[CmdletBinding()]
param(
    [ValidateSet('compare','output','raw','obs')]
    [string]$Mode = 'compare',
    [switch]$VirtualOutput,
    [switch]$MfPipe,
    [switch]$ValidateOnly
)
$ErrorActionPreference = 'Stop'
$folder = Split-Path -Parent $MyInvocation.MyCommand.Path
$routerPath = Join-Path $folder 'AvCamRouterPhysicalRawObs.exe'
$viewerPath = Join-Path $folder 'AvCamRouterPreview.exe'
$feederPath = Join-Path $folder 'AvCamFeeder.exe'
$mfPipePath = Join-Path $folder 'AvCamMfObsPipeBridge.exe'

foreach ($binary in @($routerPath, $viewerPath)) {
    if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) {
        throw "AvCam executable missing: $binary. Extract the complete no-install diagnostic archive."
    }
}
if ($VirtualOutput) {
    if (-not (Test-Path -LiteralPath $feederPath -PathType Leaf)) {
        throw 'AvCamFeeder.exe is missing from the diagnostic package.'
    }
}
if ($MfPipe -and -not (Test-Path -LiteralPath $mfPipePath -PathType Leaf)) {
    throw 'AvCamMfObsPipeBridge.exe is missing from the diagnostic package.'
}
if ($ValidateOnly) {
    Write-Output 'AVCAM_ONE_CLICK_PACKAGE_VALIDATION_PASS'
    return
}

# The virtual camera is an OPT-IN *separate* AVStream endpoint.
# Never install or register test-signed drivers automatically.
if ($VirtualOutput) {
    $sinkReport = @(& $feederPath --check-virtual-camera)
    if ($LASTEXITCODE -ne 0) {
        throw 'A unique AvCam Virtual Camera was not enumerated by both KS and Media Foundation. No drivers were changed. Use preview-only mode.'
    }
    $sinkReport | ForEach-Object { Write-Host $_ }
    # Enumeration alone can succeed for a broken or incompatible device.
    # Ask only for the existing private driver's GET_STATS method; do not
    # submit synthetic frames or open physical/OBS capture sources.
    $transportReport = @(& $feederPath --check-transport)
    # PowerShell's -notmatch returns EVERY nonmatching element for arrays.
    # That incorrectly rejects a successful probe if it prints any extra
    # diagnostic lines. Require exactly one positive readiness marker.
    $transportExit = $LASTEXITCODE
    $readyLines = @($transportReport | Where-Object {
        $_ -match '^AVCAM_TRANSPORT_READINESS=RESPONSIVE(?:\s|$)'
    })
    if ($transportExit -ne 0 -or $readyLines.Count -ne 1) {
        $transportReport | ForEach-Object { Write-Host $_ }
        throw 'AvCam private driver transport failed read-only handshake. No frames were submitted and no drivers were changed.'
    }
    $transportReport | ForEach-Object { Write-Host $_ }
}

$router = $null
$viewer = $null
$feeder = $null
$mfBroker = $null
try {
    Write-Host 'AvCam: starting independent Logitech RAW + OBS IPC router...'
    $router = Start-Process -FilePath $routerPath -ArgumentList @('--continuous', '--independent-obs', '--publish-ipc') -WorkingDirectory $folder -PassThru -NoNewWindow
    Start-Sleep -Milliseconds 950
    $router.Refresh()
    if ($router.HasExited) {
        throw "AvCam Router exited with code $($router.ExitCode). Close other AvCam Router instances and check OBS/Logitech availability."
    }

    if ($MfPipe) {
        Write-Host 'AvCam: opening experimental OBS-only cross-session pipe (no COM registration).'
        $mfBroker = Start-Process -FilePath $mfPipePath -ArgumentList @('--continuous') -WorkingDirectory $folder -PassThru -NoNewWindow
        Start-Sleep -Milliseconds 350
        $mfBroker.Refresh()
        if ($mfBroker.HasExited) {
            throw "AvCam MF pipe broker exited with code $($mfBroker.ExitCode). Another pipe broker may already own the name."
        }
    }

    if ($VirtualOutput) {
        Write-Host 'AvCam: starting processed OBS output to the separate installed AvCam Virtual Camera.'
        $feeder = Start-Process -FilePath $feederPath -ArgumentList @('--source', 'avcam-ipc-obs') -WorkingDirectory $folder -PassThru -NoNewWindow
        Start-Sleep -Milliseconds 450
        $feeder.Refresh()
        if ($feeder.HasExited) {
            throw "AvCamFeeder failed to start: $($feeder.ExitCode)"
        }
    }

    Write-Host "AvCam: starting video preview (mode=$Mode)."
    Write-Host 'Close the preview window to stop the router started by this launcher.'
    $viewer = Start-Process -FilePath $viewerPath -ArgumentList @('--mode', $Mode) -WorkingDirectory $folder -PassThru
    $viewer.WaitForExit()
    if ($viewer.ExitCode -ne 0) {
        throw "AvCam Preview exited with code $($viewer.ExitCode)."
    }
}
finally {
    # Never kill all processes by name. Stop ONLY children we created.
    if ($viewer) {
        try {
            $viewer.Refresh()
            if (-not $viewer.HasExited) {
                Stop-Process -Id $viewer.Id -Force -ErrorAction SilentlyContinue
            }
        } catch {}
        $viewer.Dispose()
    }
    if ($mfBroker) {
        try {
            $mfBroker.Refresh()
            if (-not $mfBroker.HasExited) {
                Stop-Process -Id $mfBroker.Id -Force -ErrorAction SilentlyContinue
            }
        } catch {}
        $mfBroker.Dispose()
    }
    if ($feeder) {
        try {
            $feeder.Refresh()
            if (-not $feeder.HasExited) {
                Stop-Process -Id $feeder.Id -Force -ErrorAction SilentlyContinue
            }
        } catch {}
        $feeder.Dispose()
    }
    if ($router) {
        try {
            $router.Refresh()
            if (-not $router.HasExited) {
                Stop-Process -Id $router.Id -Force -ErrorAction SilentlyContinue
                $router.WaitForExit(5000) | Out-Null
            }
        } catch {}
        $router.Dispose()
    }
    Write-Host 'AvCam: launcher session closed.'
}
