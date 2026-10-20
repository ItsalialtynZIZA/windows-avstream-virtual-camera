# AvCam Physical C270 MFT ETW capture. Captures diagnostic text only.
# Run elevated with physical Logitech C270 connected. Close Windows Camera
# BEFORE starting this script, then open it while tracing is active.
[CmdletBinding()]
param(
    [ValidateRange(10,120)][int]$Seconds = 30,
    [ValidateSet('WindowsCamera','DeepLiveCam')][string]$Target = 'WindowsCamera'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$p = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run PowerShell as administrator.'
}
$provider = '{A56F32CA-31E5-4C6D-95F2-74DB9DA141E9}'
$session = 'AvCamPhysicalMftTrace'
$folder = Join-Path $env:TEMP ('AvCamPhysicalETW-' + (Get-Date -Format yyyyMMdd-HHmmss))
New-Item -ItemType Directory -Path $folder -Force | Out-Null
$etl = Join-Path $folder 'AvCamPhysicalMft.etl'
$logman = Join-Path $env:WINDIR 'System32\logman.exe'
Write-Host "Capturing AvCam MFT ETW for $Seconds seconds; target=$Target."
try {
    & $logman start $session -p $provider 0xFFFFFFFF 5 -o $etl -ets | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "logman could not start session, code $LASTEXITCODE" }
    if ($Target -eq 'DeepLiveCam') {
        Write-Host 'TRACE_STARTED: Launch Deep Live Cam NOW; attempt to start the physical Logitech C270 once. Note the exact error.'
        Write-Host 'Do not open Chrome or Windows Camera during the reproduction.'
    } else {
        Write-Host 'TRACE_STARTED: Open Windows Camera NOW; keep physical C270 preview active.'
    }
    Start-Sleep -Seconds $Seconds
} finally {
    & $logman stop $session -ets 2>&1 | Out-Host
}
Write-Host "ETL_FILE=$etl"
if (Test-Path -LiteralPath $etl) {
    try {
        $records = @(Get-WinEvent -Path $etl -Oldest -ErrorAction Stop |
            Where-Object { $_.ProviderId -eq [guid]$provider })
        Write-Host ('ETW_PROVIDER_RECORDS=' + $records.Count)
        foreach ($record in $records | Select-Object -First 80) {
            $payload = @($record.Properties | ForEach-Object { [string]$_.Value }) -join ' '
            Write-Host ('ETW {0} {1}' -f $record.TimeCreated.ToString('HH:mm:ss'),$payload)
        }
        if ($records.Count -eq 0) {
            Write-Warning 'No provider records decoded. Keep the ETL file for further analysis.'
        }
    } catch {
        Write-Warning ('Windows could not decode ETW payloads: ' + $_.Exception.Message)
        Write-Host 'The ETL was still saved for inspection.'
    }
}
Write-Host ('ETW_TARGET=' + $Target)
Write-Host 'ETW_TRACE_FINISHED_CAMERA_UNCHANGED'
