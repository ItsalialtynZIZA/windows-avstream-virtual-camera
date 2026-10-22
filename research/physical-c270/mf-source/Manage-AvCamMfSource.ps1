# Owner-only reversible registration of experimental Windows 11 COM source.
# No virtual camera creation, driver installation, Device MFT, or Logitech edits.
[CmdletBinding()]
param(
    [ValidateSet('Status', 'Register', 'Unregister')]
    [string]$Action = 'Status',
    [switch]$AcceptExperimental
)
$ErrorActionPreference = 'Stop'
$clsid = '{8AF84A8D-96ED-4F61-B1C3-5EDC62F2D591}'
$keyPath = 'HKLM:\SOFTWARE\Classes\CLSID\' + $clsid
$inproc = Join-Path $keyPath 'InprocServer32'
$marker = 'AvCam-MF-OBS-IPC-V1'
$installDir = Join-Path $env:ProgramFiles 'AvCam Experimental MF'
$dllPath = Join-Path $installDir 'AvCamMfSource.dll'
$manifestPath = Join-Path $installDir 'AvCamMfSource.install.json'
$bundleDll = Join-Path $PSScriptRoot 'AvCamMfSource.dll'

function Read-Registration {
    $value = @{ Exists = $false; Owned = $false; Dll = $null }
    if (Test-Path -LiteralPath $keyPath) {
        $value.Exists = $true
        $value.Owned = (Get-Item -LiteralPath $keyPath).GetValue('AvCamOwner') -ceq $marker
        if (Test-Path -LiteralPath $inproc) {
            $value.Dll = (Get-Item -LiteralPath $inproc).GetValue('')
        }
    }
    return $value
}
function Assert-Approved {
    if (-not $AcceptExperimental) {
        throw 'Specify -AcceptExperimental to modify HKLM; no unattended installation is allowed.'
    }
    if ([IntPtr]::Size -ne 8) {
        throw 'Use elevated 64-bit PowerShell (not WOW64).'
    }
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Administrator access is required for opt-in COM registration.'
    }
    if ([Environment]::OSVersion.Version.Build -lt 22000) {
        throw 'Windows 11 build 22000 or later is required.'
    }
}

if ($env:OS -ne 'Windows_NT' -or -not [Environment]::Is64BitOperatingSystem) {
    throw 'AvCam experimental MF source requires x64 Windows.'
}
$current = Read-Registration

if ($Action -eq 'Status') {
    Write-Host ('AVCAM_MF_REGISTRATION=' + $(if ($current.Exists) { 'PRESENT' } else { 'ABSENT' }))
    Write-Host ('AVCAM_MF_REGISTRATION_OWNED=' + [int]$current.Owned)
    Write-Host ('AVCAM_MF_DLL_PRESENT=' + [int](Test-Path -LiteralPath $dllPath -PathType Leaf))
    Write-Host 'AVCAM_MF_REGISTRATION_STATUS_PASS'
    return
}
Assert-Approved

if ($Action -eq 'Register') {
    if ($current.Exists) {
        throw 'CLSID already exists. Refusing to overwrite any COM registration.'
    }
    if (Test-Path -LiteralPath $installDir) {
        throw 'Experimental install directory already exists; refusing to overwrite any files.'
    }
    if (-not (Test-Path -LiteralPath $bundleDll -PathType Leaf)) {
        throw 'AvCamMfSource.dll missing: extract the matching full package first.'
    }
    if ((Get-Item -LiteralPath $bundleDll).Length -lt 10240) {
        throw 'Source DLL unexpectedly small. Registration refused.'
    }
    $hash = (Get-FileHash -LiteralPath $bundleDll -Algorithm SHA256).Hash
    $createdKey = $false
    try {
        # This dedicated directory inherits protected Program Files ACLs.
        New-Item -ItemType Directory -Path $installDir -ErrorAction Stop | Out-Null
        Copy-Item -LiteralPath $bundleDll -Destination $dllPath -ErrorAction Stop
        $data = [ordered]@{ Owner = $marker; CLSID = $clsid; Dll = $dllPath; SHA256 = $hash }
        $data | ConvertTo-Json | Set-Content -LiteralPath $manifestPath -Encoding UTF8
        New-Item -Path $keyPath -Force | Out-Null
        $createdKey = $true
        New-ItemProperty -Path $keyPath -Name 'AvCamOwner' -Value $marker -PropertyType String | Out-Null
        New-Item -Path $inproc -Force | Out-Null
        Set-Item -Path $inproc -Value $dllPath
        New-ItemProperty -Path $inproc -Name 'ThreadingModel' -Value 'Both' -PropertyType String | Out-Null
        Write-Host 'AVCAM_MF_EXPERIMENTAL_COM_REGISTERED'
        Write-Host 'COM registration only: no webcam, driver or service was installed.'
    } catch {
        if ($createdKey) {
            Remove-Item -LiteralPath $keyPath -Recurse -Force -ErrorAction SilentlyContinue
        }
        if (Test-Path -LiteralPath $installDir) {
            Remove-Item -LiteralPath $dllPath -Force -ErrorAction SilentlyContinue
            Remove-Item -LiteralPath $manifestPath -Force -ErrorAction SilentlyContinue
            Remove-Item -LiteralPath $installDir -ErrorAction SilentlyContinue
        }
        throw
    }
    return
}

if ($Action -eq 'Unregister') {
    if (-not $current.Exists) {
        Write-Host 'AVCAM_MF_COM_ALREADY_ABSENT'
        return
    }
    if (-not $current.Owned -or
        -not [string]::Equals([string]$current.Dll, $dllPath,
                             [StringComparison]::OrdinalIgnoreCase)) {
        throw 'CLSID path or owner differs: refusing to delete COM registration.'
    }
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw 'AvCam manifest absent: refusing to unregister.'
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($manifest.Owner -cne $marker -or $manifest.CLSID -cne $clsid -or
        -not [string]::Equals([string]$manifest.Dll, $dllPath,
                             [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Manifest does not match the AvCam-owned COM class.'
    }
    Remove-Item -LiteralPath $keyPath -Recurse -Force -ErrorAction Stop
    Write-Host 'AVCAM_MF_EXPERIMENTAL_COM_UNREGISTERED'
    if (Test-Path -LiteralPath $dllPath -PathType Leaf) {
        if ((Get-FileHash -LiteralPath $dllPath -Algorithm SHA256).Hash -cne $manifest.SHA256) {
            Write-Warning 'Installed DLL hash changed; retained file for inspection.'
            return
        }
    }
    try {
        # Only remove the two files we created. Never recursively delete an
        # existing folder containing other contents. Do not hide failed DLL
        # deletion: an in-use COM DLL can remain loaded by Frame Server.
        if (Test-Path -LiteralPath $dllPath -PathType Leaf) {
            Remove-Item -LiteralPath $dllPath -Force -ErrorAction Stop
        }
        if (Test-Path -LiteralPath $manifestPath -PathType Leaf) {
            Remove-Item -LiteralPath $manifestPath -Force -ErrorAction Stop
        }
        if (Test-Path -LiteralPath $installDir -PathType Container) {
            $remaining = @(Get-ChildItem -LiteralPath $installDir -Force -ErrorAction Stop)
            if ($remaining.Count -gt 0) {
                throw "AvCam installation folder still contains $($remaining.Count) object(s). No recursive deletion performed."
            }
            Remove-Item -LiteralPath $installDir -ErrorAction Stop
        }
        if ((Test-Path -LiteralPath $dllPath) -or (Test-Path -LiteralPath $installDir)) {
            throw 'Installed AvCam files still exist after cleanup.'
        }
        Write-Host 'AVCAM_MF_EXPERIMENTAL_FILES_CLEANED'
    } catch {
        Write-Warning ('AvCam COM registration was removed but file cleanup is incomplete: ' + $_.Exception.Message)
        Write-Host 'AVCAM_MF_EXPERIMENTAL_FILES_LEFT'
    }
}
