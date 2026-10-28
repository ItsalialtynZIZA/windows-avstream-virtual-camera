# Bounded MJPEG capture from the ORIGINAL 13D3:56FF Windows Media Foundation source.
# No OBS, frame injection, registry edits, frame export, COM registration or driver installation.
[CmdletBinding()]
param([ValidateRange(5,60)][int]$TimeoutSeconds = 30)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$exe = Join-Path $PSScriptRoot 'AvCamIntegratedNativeCaptureSmoke.exe'
if (-not [Environment]::Is64BitProcess) { throw '64-bit PowerShell required' }
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
    throw "Missing native capture smoke executable: $exe"
}
Write-Warning 'This test briefly opens the ORIGINAL Integrated Camera. Close Chrome camera tabs, OBS camera sources, Deep-Live-Cam, and Windows Camera first.'
# Only diagnostics metadata is emitted. Keep stdout and stderr separately.
$runId = [guid]::NewGuid().ToString('N')
$out = Join-Path $env:TEMP "avcam-integrated-capture-$runId.out.txt"
$err = Join-Path $env:TEMP "avcam-integrated-capture-$runId.err.txt"
$p = $null
try {
    & $exe --selftest
    if ($LASTEXITCODE -ne 0) {
        throw "Native smoke selftest failed: $LASTEXITCODE"
    }
    $startArgs = @{
        FilePath = $exe
        ArgumentList = '--capture-smoke'
        RedirectStandardOutput = $out
        RedirectStandardError = $err
        PassThru = $true
        ErrorAction = 'Stop'
    }
    $p = Start-Process @startArgs
    $completed = $p.WaitForExit($TimeoutSeconds * 1000)
    if (-not $completed) {
        Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
        try { $p.WaitForExit(3000) | Out-Null } catch {}
        throw "CAMERA_CAPTURE_TIMEOUT: exceeded $TimeoutSeconds seconds; stopped only AvCam test process."
    }
    if (Test-Path -LiteralPath $out) {
        Get-Content -LiteralPath $out | ForEach-Object { Write-Host $_ }
    }
    if (Test-Path -LiteralPath $err) {
        Get-Content -LiteralPath $err | ForEach-Object { Write-Warning $_ }
    }
    if ($p.ExitCode -ne 0) {
        throw "AVCAM_NATIVE_CAPTURE_FAILED exitcode=$($p.ExitCode)"
    }
    Write-Host 'AVCAM_BOUNDED_NATIVE_CAPTURE_COMPLETE'
}
finally {
    if ($p) {
        if (-not $p.HasExited) {
            Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
        }
        $p.Dispose()
    }
    foreach ($file in @($out, $err)) {
        if (Test-Path -LiteralPath $file) {
            Remove-Item -LiteralPath $file -Force -ErrorAction SilentlyContinue
        }
    }
}
