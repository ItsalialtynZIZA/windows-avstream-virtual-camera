# No-install AvCam preview launcher. Does not change drivers or camera endpoints.
# Starts only user-mode RAW/OBS router and a separate IPC video viewer.
[CmdletBinding()]
param(
    [ValidateSet('compare','output','raw','obs')]
    [string]$Mode = 'compare',
    [switch]$VirtualOutput,
    [switch]$ValidateOnly
)
$ErrorActionPreference = 'Stop'
$folder = Split-Path -Parent $MyInvocation.MyCommand.Path
$routerPath = Join-Path $folder 'AvCamRouterPhysicalRawObs.exe'
$viewerPath = Join-Path $folder 'AvCamRouterPreview.exe'
$feederPath = Join-Path $folder 'AvCamFeeder.exe'

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
if ($ValidateOnly) {
    Write-Output 'AVCAM_ONE_CLICK_PACKAGE_VALIDATION_PASS'
    return
}

# The virtual camera is an OPT-IN *separate* AVStream endpoint.
# Never install or register test-signed drivers automatically.
if ($VirtualOutput) {
    $sinkReport = @(& $feederPath --check-sink)
    if ($LASTEXITCODE -ne 0) {
        throw 'Separate AvCam Virtual Camera sink not found. No driver changes were made. Use preview-only mode or an independently approved installed AvCam driver.'
    }
    $sinkReport | ForEach-Object { Write-Host $_ }
}

$router = $null
$viewer = $null
$feeder = $null
try {
    Write-Host 'AvCam: starting independent Logitech RAW + OBS IPC router...'
    $router = Start-Process -FilePath $routerPath -ArgumentList @('--continuous', '--independent-obs', '--publish-ipc') -WorkingDirectory $folder -PassThru -NoNewWindow
    Start-Sleep -Milliseconds 950
    $router.Refresh()
    if ($router.HasExited) {
        throw "AvCam Router exited with code $($router.ExitCode). Close other AvCam Router instances and check OBS/Logitech availability."
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
