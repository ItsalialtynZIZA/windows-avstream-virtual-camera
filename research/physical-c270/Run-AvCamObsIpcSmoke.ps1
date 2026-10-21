#Requires -Version 5.1
[CmdletBinding()]
param(
    [string]$Executable = '',
    [string]$ReportDirectory = ''
)
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($Executable)) {
    $Executable = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) 'AvCamMockFrameIpc.exe'
}
if ([string]::IsNullOrWhiteSpace($ReportDirectory)) {
    $ReportDirectory = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'AvCam\ObsIpcSmoke'
}
$exe = (Resolve-Path -LiteralPath $Executable -ErrorAction Stop).Path
New-Item -ItemType Directory -Force -Path $ReportDirectory | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$report = Join-Path $ReportDirectory "obs-ipc-$stamp.txt"
$errFile = Join-Path $ReportDirectory "obs-ipc-$stamp.stderr.txt"
Write-Host "AVCAM_OBS_IPC_EXE=$exe"
Write-Host 'This test does not register drivers or open the physical Logitech camera.'
$proc = Start-Process -FilePath $exe -ArgumentList '--obs' -PassThru -Wait -NoNewWindow -RedirectStandardOutput $report -RedirectStandardError $errFile
if (Test-Path -LiteralPath $errFile) {
    Get-Content -LiteralPath $errFile | Tee-Object -FilePath $report -Append
}
Write-Host "AVCAM_OBS_IPC_EXIT=$($proc.ExitCode)"
Write-Host "AVCAM_OBS_IPC_REPORT=$report"
Get-Content -LiteralPath $report
if ($proc.ExitCode -eq 8) {
    Write-Warning 'OBS shared memory was not available. Start OBS Virtual Camera and retry.'
} elseif ($proc.ExitCode -ne 0) {
    Write-Warning 'Live IPC failed. Inspect the report for OBS_SOURCE_FRAME_FAILED or IPC_FRAME_CHILD_EXIT.'
} else {
    Write-Host 'AVCAM_OBS_IPC_RUN_COMPLETED'
}
exit $proc.ExitCode
