param(
    [Parameter(Mandatory=$true)]
    [string]$PackageDirectory,

    [switch]$TrustTestCertificate,
    [switch]$AcceptTestSignedDriver
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

# Fail closed before touching the certificate store or creating any PnP device.
if (-not $AcceptTestSignedDriver) {
    throw 'Explicit approval required: -AcceptTestSignedDriver. No changes made.'
}
Write-Host ''
Write-Host 'Boot test-signing state:'
$bcdLines = @(& bcdedit.exe /enum '{current}' 2>&1)
$bcdExit = $LASTEXITCODE
if ($bcdExit -ne 0) {
    throw "Cannot read BCD test-signing status (exit=$bcdExit). No changes made."
}
$bcd = $bcdLines -join "`n"
# On a Russian locale, the BCD value may be 'Да'.
$testSigningEnabled = $bcd -match '(?im)^\s*testsigning\s+(?:Yes|On|Да|1)\s*$'
if (-not $testSigningEnabled) {
    throw 'TESTSIGNING is not confirmed active for the current boot. No driver/certificate changes made.'
}
Write-Host 'AVCAM_TESTSIGNING_CONFIRMED=1'

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

# The bundled native SetupAPI tool replaces WDK DevCon for fresh installs.
$rootTool = Get-ChildItem $package -Recurse -Filter 'AvCamRootInstall.exe' -File |
    Select-Object -First 1
if ($rootTool) {
    Write-Host "Using AvCamRootInstall: $($rootTool.FullName)"
    & $rootTool.FullName install $inf.FullName
    if ($LASTEXITCODE -ne 0) {
        throw "AvCamRootInstall failed with exit code $LASTEXITCODE. Inspect setupapi.dev.log."
    }
} else {
    # Legacy package: use a pre-existing WDK DevCon if available.
    $programFilesX86 = ${env:ProgramFiles(x86)}
    $toolsRoot = Join-Path $programFilesX86 'Windows Kits\10\Tools'
    $devcon = if (Test-Path -LiteralPath $toolsRoot) {
        Get-ChildItem $toolsRoot -Recurse -Filter devcon.exe -File -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match '\\\\x64\\\\devcon\\.exe$' } |
            Sort-Object FullName -Descending |
            Select-Object -First 1 -ExpandProperty FullName
    }
    if (-not $devcon) {
        throw 'Neither AvCamRootInstall.exe nor DevCon.exe found. Download the complete current runtime package.'
    }
    Write-Host "Using legacy DevCon: $devcon"
    & $devcon install $inf.FullName 'Root\AvCamVirtualCamera'
    if ($LASTEXITCODE -ne 0) {
        throw "DevCon install failed with exit code $LASTEXITCODE."
    }
}
Write-Host ''
Write-Host 'Driver installation requested. Current AvCam device state:'
Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
    Where-Object {
        $_.InstanceId -like 'ROOT\AVCAMVIRTUALCAMERA*' -or
        $_.FriendlyName -eq 'AvCam Virtual Camera'
    } |
    Format-List Status, Class, FriendlyName, InstanceId, Problem, ConfigManagerErrorCode

Write-Host 'AVCAM_TEST_SIGNED_DRIVER_INSTALL_ATTEMPT_COMPLETE'
