# No-install AvCam preview launcher. Does not change drivers or camera endpoints.
# Starts only user-mode RAW/OBS router and a separate IPC video viewer.
[CmdletBinding()]
param(
    [ValidateSet('compare','output','raw','obs')]
    [string]$Mode = 'compare',
    [switch]$ValidateOnly
)
$ErrorActionPreference = 'Stop'
$folder = Split-Path -Parent $MyInvocation.MyCommand.Path
$routerPath = Join-Path $folder 'AvCamRouterPhysicalRawObs.exe'
$viewerPath = Join-Path $folder 'AvCamRouterPreview.exe'

foreach ($binary in @($routerPath, $viewerPath)) {
    if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) {
        throw "AvCam executable missing: $binary. Extract the complete no-install diagnostic archive."
    }
}
if ($ValidateOnly) {
    Write-Output 'AVCAM_ONE_CLICK_PACKAGE_VALIDATION_PASS'
    return
}

$router = $null
$viewer = $null
try {
    Write-Host 'AvCam: starting independent Logitech RAW + OBS IPC router...'
    $router = Start-Process -FilePath $routerPath -ArgumentList @('--continuous', '--independent-obs', '--publish-ipc') -WorkingDirectory $folder -PassThru -NoNewWindow
    Start-Sleep -Milliseconds 950
    $router.Refresh()
    if ($router.HasExited) {
        throw "AvCam Router exited with code $($router.ExitCode). Close other AvCam Router instances and check OBS/Logitech availability."
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
