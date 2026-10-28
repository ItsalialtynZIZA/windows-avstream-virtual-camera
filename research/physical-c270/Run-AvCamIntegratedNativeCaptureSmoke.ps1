# Time-bounded MJPEG capture of ORIGINAL Lenovo 13D3:56FF camera via Windows Media Foundation.
# No frame export, USB driver change, COM installation, registry write, or video substitution.
[CmdletBinding()]
param(
    [ValidateRange(5,60)][int]$TimeoutSeconds = 30,
    [switch]$WrapperSelfTest
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Direct .NET Process avoids Windows PowerShell 5.1 Start-Process -PassThru
# returning a blank ExitCode even after the native process has exited.
# Drain redirected streams asynchronously and read the integer exit code.
function Invoke-AvCamBoundedProcess {
    param(
        [Parameter(Mandatory=$true)][string]$Executable,
        [Parameter(Mandatory=$true)][string]$Arguments,
        [Parameter(Mandatory=$true)][int]$Seconds
    )
    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = $Executable
    $startInfo.Arguments = $Arguments
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true

    $p = New-Object System.Diagnostics.Process
    $p.StartInfo = $startInfo
    $started = $false
    try {
        if (-not $p.Start()) {
            throw "AVCAM_PROCESS_START_FAILED: $Executable"
        }
        $started = $true
        $outTask = $p.StandardOutput.ReadToEndAsync()
        $errTask = $p.StandardError.ReadToEndAsync()
        $done = $p.WaitForExit($Seconds * 1000)
        if (-not $done) {
            $p.Kill()
            [void]$p.WaitForExit(3000)
            throw "CAMERA_CAPTURE_TIMEOUT: exceeded $Seconds seconds; stopped only test process."
        }
        $stdout = $outTask.GetAwaiter().GetResult()
        $stderr = $errTask.GetAwaiter().GetResult()
        $p.Refresh()
        return [pscustomobject]@{
            ExitCode = [int]$p.ExitCode
            Stdout = [string]$stdout
            Stderr = [string]$stderr
        }
    }
    finally {
        if ($started -and -not $p.HasExited) {
            try { $p.Kill() } catch {}
        }
        $p.Dispose()
    }
}

if ($WrapperSelfTest) {
    # No camera or AvCam EXE needed; unprivileged tests run in Windows CI.
    $cmd = Join-Path $env:WINDIR 'System32\cmd.exe'
    $ok = Invoke-AvCamBoundedProcess -Executable $cmd -Arguments '/d /c echo AVCAM_MOCK_OK' -Seconds 5
    if ($ok.ExitCode -ne 0 -or $ok.Stdout -notmatch 'AVCAM_MOCK_OK') {
        throw "Wrapper success/redirect contract failed: $($ok.ExitCode)"
    }
    $fail = Invoke-AvCamBoundedProcess -Executable $cmd -Arguments '/d /c exit /b 17' -Seconds 5
    if ($fail.ExitCode -ne 17) {
        throw "Wrapper nonzero exit contract failed: $($fail.ExitCode)"
    }
    Write-Host 'AVCAM_BOUNDED_WRAPPER_SELFTEST_PASS'
    return
}

if (-not [Environment]::Is64BitProcess) { throw '64-bit PowerShell required' }
$exe = Join-Path $PSScriptRoot 'AvCamIntegratedNativeCaptureSmoke.exe'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
    throw "Missing native capture smoke executable: $exe"
}
Write-Warning 'This test briefly opens the ORIGINAL Integrated Camera. Close Chrome camera tabs, OBS camera sources, Deep-Live-Cam, and Windows Camera first.'

& $exe --selftest
if ($LASTEXITCODE -ne 0) {
    throw "Native smoke selftest failed: $LASTEXITCODE"
}

$result = Invoke-AvCamBoundedProcess -Executable $exe -Arguments '--capture-smoke' -Seconds $TimeoutSeconds

if ($result.Stdout) { Write-Host $result.Stdout.TrimEnd() }
if ($result.Stderr) { Write-Warning $result.Stderr.TrimEnd() }
if ($result.ExitCode -ne 0) {
    throw "AVCAM_NATIVE_CAPTURE_FAILED exitcode=$($result.ExitCode)"
}
# Exit status alone is insufficient: confirm actual native-camera PASS marker.
if ($result.Stdout -notmatch '(?m)^AVCAM_INTEGRATED_NATIVE_MJPEG_CAPTURE_PASS\r?$') {
    throw 'AVCAM_NATIVE_CAPTURE_FAILED: camera PASS marker missing'
}
Write-Host 'AVCAM_BOUNDED_NATIVE_CAPTURE_COMPLETE'
