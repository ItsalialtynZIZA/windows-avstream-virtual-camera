# AvCam C270-compatible experimental host-driver update.
# DEFAULT is read-only preflight. Installation requires explicit -Install flag
# and an existing locally trusted TEST-signing certificate/private key.
[CmdletBinding()]
param(
    [switch]$Install,
    [string]$CertificateSubject = 'CN=AvCam Local Kernel Test'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Find-Tool {
    param([string]$FileName)
    $cmd = Get-Command $FileName -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    # SignTool is usually x64; Inf2Cat may be x86 in some WDK installs.
    # Inspect both architectures and Windows Kits installation roots.
    $roots = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'),
        (Join-Path $env:ProgramFiles 'Windows Kits\10\bin')
    ) | Select-Object -Unique
    $found = @(
        foreach ($root in $roots) {
            if (Test-Path -LiteralPath $root -PathType Container) {
                Get-ChildItem -LiteralPath $root -Recurse -Filter $FileName -File -ErrorAction SilentlyContinue |
                    Where-Object { $_.FullName -match '(?i)\\(x64|x86)\\' } |
                    Select-Object -ExpandProperty FullName
            }
        }
    )
    if ($found.Count -eq 0) { return $null }
    if ($FileName -ieq 'inf2cat.exe') {
        return ($found | Sort-Object -Descending | Select-Object -First 1)
    }
    return ($found | Sort-Object -Descending | Select-Object -First 1)
}
function Invoke-External {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Program failed with exit code $LASTEXITCODE"
    }
}

$requiredFiles = @('AvCamUde.sys', 'AvCamUde.inf', 'avcamude.cat',
    'AvCamUdeHostRootInstall.exe', 'AvCamPreviewC270.exe')
foreach ($name in $requiredFiles) {
    if (-not (Test-Path (Join-Path $PSScriptRoot $name) -PathType Leaf)) {
        throw "Missing C270 package file: $name"
    }
}
$os = Get-CimInstance Win32_OperatingSystem
if ($os.BuildNumber -lt 22000 -or -not [Environment]::Is64BitOperatingSystem) {
    throw 'C270 UdeCx test is intended for Windows 11 x64 only'
}
$certs = @(Get-ChildItem Cert:\LocalMachine\My |
    Where-Object { $_.Subject -eq $CertificateSubject -and $_.HasPrivateKey -and $_.NotAfter -gt (Get-Date) })
if ($certs.Count -ne 1) {
    throw 'Exactly one valid local machine test certificate with private key is required; refusing update'
}
$cert = $certs[0]
$signTool = Find-Tool 'signtool.exe'
$inf2cat = Find-Tool 'inf2cat.exe'
if (-not $signTool -or -not $inf2cat) {
    Write-Host "SignTool detected: $([bool]$signTool)"
    Write-Host "Inf2Cat detected: $([bool]$inf2cat)"
    Write-Host 'Search includes Windows Kits 10 bin x64 and x86. If a tool is missing, install official Windows SDK/WDK components.'
    throw 'Windows Kit tools incomplete; existing working AvCam driver left untouched'
}
Write-Host "Windows: $($os.Caption) build $($os.BuildNumber)"
Write-Host "Signer: $($cert.Subject) (private key available)"
Write-Host "SignTool: $signTool"
Write-Host "Inf2Cat: $inf2cat"
Write-Host 'Profile: UdeCx 1280x720 MJPEG / separate AvCam VID_1234 PID_5679'

if (-not $Install) {
    Write-Host 'PREFLIGHT_ONLY: no driver, service, certificate or device was modified.'
    Write-Host 'Review README_C270_COMPATIBILITY.txt and your recovery/backup first.'
    Write-Host 'Install requires: .\AvCamC270Install.ps1 -Install'
    return
}
$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Install must run in Administrator PowerShell; no changes made'
}
$root = @(Get-PnpDevice -ErrorAction Stop | Where-Object {
    $_.InstanceId -match '(?i)^ROOT\\AVCAMUDECONTROLLER(?:\\|$)'
})
if ($root.Count -ne 1 -or $root[0].Status -ne 'OK') {
    throw 'Exactly one healthy existing UdeCx controller required; no changes made'
}
# Back up current driver before modifying a kernel package or invoking SetupAPI.
$backup = Join-Path $env:USERPROFILE ('Documents\AvCam-C270-Backup-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $backup -Force | Out-Null
$property = Get-PnpDeviceProperty -InstanceId $root[0].InstanceId -KeyName 'DEVPKEY_Device_DriverInfPath'
$publishedInf = [string]$property.Data
if ($publishedInf -notmatch '^oem[0-9]+\.inf$') {
    throw "Cannot safely export installed driver: $publishedInf"
}
Invoke-External 'pnputil.exe' @('/export-driver', $publishedInf, $backup)
if (@(Get-ChildItem $backup -Filter '*.sys' -Recurse -File).Count -eq 0) {
    throw 'PnP driver export did not contain SYS. Stopping without installation.'
}
Write-Host "Original driver backup: $backup"

# Inf2Cat.exe on Windows PowerShell 5.1 can misparse an embedded /driver:
# argument when the directory contains spaces (e.g. a user profile name).
# Sign in a dedicated, short, space-free temporary folder, not in Documents.
$work = Join-Path $env:SystemDrive ('AvCamC270Stage-' + [guid]::NewGuid().ToString('N'))
$candidateBackup = Join-Path $backup 'candidate-signed'
New-Item -ItemType Directory -Path $work -Force | Out-Null
try {
    foreach ($name in @('AvCamUde.sys', 'AvCamUde.inf', 'avcamude.cat')) {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $work
    }
    if ($work -match '\s') {
        throw "Signing staging path unexpectedly contains spaces: $work"
    }
    $sys = Join-Path $work 'AvCamUde.sys'
    $inf = Join-Path $work 'AvCamUde.inf'
    $cat = Join-Path $work 'avcamude.cat'
    $thumb = $cert.Thumbprint
    Write-Host "Signing stage: $work"
    Invoke-External $signTool @('sign', '/as', '/sm', '/s', 'My', '/sha1',
        $thumb, '/fd', 'SHA256', $sys)

    # Signed SYS bytes differ from the original catalog manifest.
    # Regenerate the catalog from the exact signed SYS and matching INF.
    Remove-Item -LiteralPath $cat -Force
    $inf2catArgs = @("/driver:$work", '/os:10_X64')
    Write-Host "Inf2Cat arguments: $($inf2catArgs -join ' ')"
    Invoke-External $inf2cat $inf2catArgs
    if (-not (Test-Path -LiteralPath $cat -PathType Leaf)) {
        throw 'Inf2Cat did not generate matching catalog; existing controller untouched'
    }
    Invoke-External $signTool @('sign', '/sm', '/s', 'My', '/sha1',
        $thumb, '/fd', 'SHA256', $cat)
    Invoke-External $signTool @('verify', '/pa', $sys)
    Invoke-External $signTool @('verify', '/pa', $cat)
    Write-Host 'Locally signed driver and catalog verified.'

    # Preserve the fully signed candidate for repeatable rollback diagnostics.
    New-Item -ItemType Directory -Path $candidateBackup -Force | Out-Null
    Copy-Item -LiteralPath $sys -Destination $candidateBackup
    Copy-Item -LiteralPath $inf -Destination $candidateBackup
    Copy-Item -LiteralPath $cat -Destination $candidateBackup

    Write-Host 'Installing only ROOT\AvCamUdeController via guarded SetupAPI tool.'
    & (Join-Path $PSScriptRoot 'AvCamUdeHostRootInstall.exe') 'install' $inf 'I_ACCEPT_KERNEL_TEST_ON_PRIMARY_WINDOWS'
    if ($LASTEXITCODE -ne 0) {
        throw "Installer failed, original driver export remains at $backup"
    }
} finally {
    # Do not leave staged kernel binaries in the filesystem root.
    if (Test-Path -LiteralPath $work -PathType Container) {
        Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    }
}
Write-Host 'C270_PROFILE_INSTALL_ATTEMPT_COMPLETE'
Write-Host "Backup for rollback: $backup"
Write-Host 'Restart Windows if SetupAPI requests it; then use AvCamPreviewC270.exe with OBS.'
