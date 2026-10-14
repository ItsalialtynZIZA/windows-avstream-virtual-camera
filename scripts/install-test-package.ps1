param(
    [Parameter(Mandatory=$true)]
    [string]$PackageDirectory,

    [switch]$TrustTestCertificate
)

$ErrorActionPreference = 'Stop'

function Test-IsAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not (Test-IsAdministrator)) {
    throw 'Run PowerShell as Administrator.'
}

$package = (Resolve-Path $PackageDirectory).Path
$inf = Get-ChildItem $package -Recurse -Filter 'AvCam.inf' -File | Select-Object -First 1
$cer = Get-ChildItem $package -Recurse -Filter 'AvCam.cer' -File | Select-Object -First 1
$sys = Get-ChildItem $package -Recurse -Filter 'AvCam.sys' -File | Select-Object -First 1
$cat = Get-ChildItem $package -Recurse -Filter 'AvCam.cat' -File | Select-Object -First 1

if (-not $inf) { throw 'AvCam.inf was not found in the package directory.' }
if (-not $sys) { throw 'AvCam.sys was not found in the package directory.' }
if (-not $cat) { throw 'AvCam.cat was not found in the package directory.' }

Write-Host '=== AvCam test package ==='
Write-Host "INF: $($inf.FullName)"
Write-Host "SYS: $($sys.FullName)"
Write-Host "CAT: $($cat.FullName)"
if ($cer) { Write-Host "CER: $($cer.FullName)" }

Write-Host ''
Write-Host 'Boot test-signing state:'
$bcd = bcdedit.exe /enum '{current}' 2>&1 | Out-String
$testSigningEnabled = $bcd -match '(?im)^testsigning\s+Yes\s*$'
if ($testSigningEnabled) {
    Write-Host 'TESTSIGNING appears to be enabled.'
} else {
    Write-Warning 'TESTSIGNING does not appear to be enabled. This script will NOT enable it automatically.'
}

if ($TrustTestCertificate) {
    if (-not $cer) {
        throw '-TrustTestCertificate was specified but AvCam.cer was not found.'
    }

    Write-Warning 'Importing the CI test certificate into LocalMachine Root and TrustedPublisher. Use only on a dedicated test machine.'
    Import-Certificate -FilePath $cer.FullName -CertStoreLocation 'Cert:\LocalMachine\Root' | Out-Null
    Import-Certificate -FilePath $cer.FullName -CertStoreLocation 'Cert:\LocalMachine\TrustedPublisher' | Out-Null
    Write-Host 'Test certificate imported.'
} elseif ($cer) {
    Write-Host 'Test certificate was found but was not trusted automatically.'
    Write-Host 'Re-run with -TrustTestCertificate only on a dedicated development/test machine if required.'
}

$programFilesX86 = ${env:ProgramFiles(x86)}
$toolsRoot = Join-Path $programFilesX86 'Windows Kits\10\Tools'
$devconCandidates = @(
    (Join-Path $toolsRoot 'x64\devcon.exe'),
    (Join-Path $toolsRoot '10.0.26100.0\x64\devcon.exe')
)

$devcon = $devconCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $devcon -and (Test-Path $toolsRoot)) {
    $devcon = Get-ChildItem $toolsRoot -Recurse -Filter devcon.exe -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\x64\\devcon\.exe$' } |
        Sort-Object FullName -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}

if (-not $devcon) {
    throw 'devcon.exe was not found. Install the Windows Driver Kit (WDK) Tools component.'
}

Write-Host ''
Write-Host "Using devcon: $devcon"
& $devcon install $inf.FullName 'Root\AvCamVirtualCamera'
if ($LASTEXITCODE -ne 0) {
    throw "devcon install failed with exit code $LASTEXITCODE."
}

Write-Host ''
Write-Host 'Driver installation requested. Current AvCam device state:'
Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
    Where-Object {
        $_.InstanceId -like 'ROOT\AVCAMVIRTUALCAMERA*' -or
        $_.FriendlyName -eq 'AvCam Virtual Camera'
    } |
    Format-List Status, Class, FriendlyName, InstanceId, Problem, ConfigManagerErrorCode

if (-not $testSigningEnabled) {
    Write-Warning 'If Windows reports a signature/start failure, use a dedicated test machine and explicitly enable test-signing before retrying.'
}
