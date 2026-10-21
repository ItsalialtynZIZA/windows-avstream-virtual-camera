# Read-only two-phase camera contention snapshot.
# Run with Deep Live Cam showing the physical Logitech C270.
# No registry changes, camera driver installation, or tracing activation.
[CmdletBinding()]
param(
    [ValidateSet('Baseline','ChromeBusy')][string]$Phase,
    [ValidateRange(1,30)][int]$Minutes = 3
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$script = Join-Path $PSScriptRoot 'AvCamC270EventCorrelation.ps1'
if (-not (Test-Path -LiteralPath $script -PathType Leaf)) {
    throw "Missing companion script: $script"
}
$dir = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'AvCam\C270SharingEvidence'
New-Item -ItemType Directory -Path $dir -Force | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$report = Join-Path $dir ("{0}-{1}.txt" -f $Phase,$stamp)
$header = @(
    "AVCAM_CAPTURE_PHASE=$Phase",
    ("CAPTURE_LOCAL={0:o}" -f (Get-Date)),
    "NOTE=Read-only metadata; no full event messages or camera images saved"
)
$header | Set-Content -LiteralPath $report -Encoding UTF8
try {
    & $script -Minutes $Minutes | Out-File -LiteralPath $report -Append -Encoding UTF8
} catch {
    ("EVIDENCE_SCRIPT_FAILED={0}" -f $_.Exception.Message) | Add-Content -LiteralPath $report
    throw
}
Write-Host "AVCAM_EVIDENCE_REPORT=$report"
Get-Content -LiteralPath $report
