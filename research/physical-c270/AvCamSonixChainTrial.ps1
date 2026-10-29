# Experimental pass-through-first Device MFT chain trial for Lenovo 82K1.
# No OBS substitution is enabled: validate actual Sonix->AvCam chaining first.
# Install requires elevation and typed confirmation. Rollback can work even if
# the camera PnP/FrameServer fails. Never touches the USB driver.
# Never changes USB drivers, Sonix COM registration, or camera-facing names.
[CmdletBinding()]
param(
    [ValidateSet('Status','Install','Rollback','SelfTest')]
    [string]$Action = 'Status',
    [switch]$AcceptHardwareRisk
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Sonix = '{5A50829A-86DD-4D18-8685-891EEE643C24}'
$AvCam = '{1DE4C92F-A801-45BB-9D5A-74723A8EF460}'
$Category = '{E5323777-F976-4F5B-9B55-B94699C46E44}'
$ChainValue = 'CameraDeviceMftClsidChain'
$RootPath = "SYSTEM\CurrentControlSet\Control\DeviceClasses\$Category"
$Folder = Join-Path $env:ProgramFiles 'AvCamSonixChainTrial'
$Dll = Join-Path $Folder 'AvCamSonixPassThroughDmft.dll'
$StateFile = Join-Path $Folder 'oem-chain-rollback.json'
$Original = [string[]]@($Sonix)
$Candidate = [string[]]@($Sonix,$AvCam)

function Same([string[]]$Left,[string[]]$Right) {
    if ($Left.Count -ne $Right.Count) { return $false }
    for ($i=0;$i -lt $Left.Count;$i++) {
        if ($Left[$i] -cne $Right[$i]) { return $false }
    }
    return $true
}
function Assert-InterfacePath([string]$Path) {
    if (-not $Path.StartsWith("$RootPath\",[StringComparison]::OrdinalIgnoreCase) -or
        $Path.IndexOf('VID_13D3&PID_56FF&MI_00',[StringComparison]::OrdinalIgnoreCase) -lt 0 -or
        -not $Path.EndsWith('\#GLOBAL\Device Parameters',[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refusing an unrelated camera interface key'
    }
}
function Open-Hklm64 {
    if (-not [Environment]::Is64BitProcess) { throw '64-bit PowerShell required' }
    return [Microsoft.Win32.RegistryKey]::OpenBaseKey(
        [Microsoft.Win32.RegistryHive]::LocalMachine,
        [Microsoft.Win32.RegistryView]::Registry64)
}
function Assert-Admin {
    $principal=[Security.Principal.WindowsPrincipal]::new(
        [Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Run an elevated 64-bit PowerShell for this operation'
    }
}
function Get-Chain([Microsoft.Win32.RegistryKey]$Key) {
    if ($null -eq $Key) { throw 'Camera interface key missing' }
    # Windows registry value names are case-insensitive; a case-sensitive
    # comparison incorrectly reported that the OEM chain was absent.
    $names = @($Key.GetValueNames())
    if ($names -notcontains $ChainValue) {
        Write-Host "AVCAM_SONIX_INTERFACE_KEY=$($Key.Name)"
        Write-Host "AVCAM_SONIX_VISIBLE_VALUES=$($names -join ';')"
        throw 'Original OEM chain value missing in selected interface'
    }
    if ($Key.GetValueKind($ChainValue) -ne [Microsoft.Win32.RegistryValueKind]::MultiString) {
        throw 'Refusing chain registry type other than REG_MULTI_SZ'
    }
    return [string[]]@($Key.GetValue($ChainValue,[string[]]@()))
}
function Find-Interface([Microsoft.Win32.RegistryKey]$Root,[string]$DeviceId) {
    $deviceRoot=$Root.OpenSubKey($RootPath,$false)
    if ($null -eq $deviceRoot) { throw 'Camera VIDEO_CAMERA interface root unavailable' }
    try {
        $encoded=$DeviceId.Replace('\','#')
        $matches=@(
            foreach ($name in $deviceRoot.GetSubKeyNames()) {
                if ($name.IndexOf($encoded,[StringComparison]::OrdinalIgnoreCase) -lt 0 -or
                    -not $name.EndsWith("#$Category",[StringComparison]::OrdinalIgnoreCase)) {
                    continue
                }
                $relative="$name\#GLOBAL\Device Parameters"
                $sub=$deviceRoot.OpenSubKey($relative,$false)
                if ($null -ne $sub) {
                    $sub.Dispose()
                    "$RootPath\$relative"
                }
            }
        )
    } finally { $deviceRoot.Dispose() }
    if ($matches.Count -ne 1) {
        throw "Expected one exact USB camera interface, found $($matches.Count)"
    }
    return [string]$matches[0]
}
function Assert-OemBaseline([Microsoft.Win32.RegistryKey]$Key) {
    if (-not (Same ([string[]]@(Get-Chain $Key)) $Original)) {
        throw 'OEM baseline is no longer Sonix-only'
    }
    foreach ($name in @('CameraDeviceMftClsid','CameraPostProcessingPluginCLSID')) {
        if ($Key.GetValue($name,$null)) {
            throw "Unexpected existing camera extension: $name"
        }
    }
    $sonixDll=Join-Path $env:WINDIR 'System32\SONiX\SonixDeviceMFT.dll'
    if (-not (Test-Path -LiteralPath $sonixDll -PathType Leaf)) {
        throw 'Signed Sonix OEM module is missing'
    }
    if ((Get-AuthenticodeSignature -LiteralPath $sonixDll).Status -ne 'Valid') {
        throw 'Sonix OEM signature verification failed'
    }
}
function Get-ComDll([Microsoft.Win32.RegistryKey]$Root) {
    $key=$Root.OpenSubKey("SOFTWARE\Classes\CLSID\$AvCam\InprocServer32",$false)
    if ($null -eq $key) { return '' }
    try { return [string]$key.GetValue('','') }
    finally { $key.Dispose() }
}
function Invoke-Regsvr([switch]$Unregister) {
    if (-not (Test-Path -LiteralPath $Dll -PathType Leaf)) {
        throw "COM DLL missing: $Dll"
    }
    $args=if ($Unregister) { '/u /s' } else { '/s' }
    $args+=' "'+$Dll+'"'
    $exe=Join-Path $env:WINDIR 'System32\regsvr32.exe'
    $process=Start-Process -FilePath $exe -ArgumentList $args -Wait -PassThru -ErrorAction Stop
    if ($null -eq $process -or [int]$process.ExitCode -ne 0) {
        throw "regsvr32 failed (unregister=$Unregister)"
    }
}
function Test-SourceC270([Microsoft.Win32.RegistryKey]$Root) {
    if (-not (Get-Command Get-PnpDevice -ErrorAction SilentlyContinue)) {
        throw 'Get-PnpDevice unavailable'
    }
    $logi=@(Get-PnpDevice -PresentOnly | Where-Object {
        $_.Status -eq 'OK' -and $_.Class -eq 'Camera' -and
        $_.InstanceId -match '(?i)^USB\\VID_046D&PID_0825(&MI_00)?\\'
    })
    if ($logi.Count -ne 1) { throw 'Expected exactly one healthy Logitech C270 RAW source' }
    $sourcePath=Find-Interface $Root $logi[0].InstanceId
    $source=$Root.OpenSubKey($sourcePath,$false)
    if ($null -eq $source) { throw 'Logitech camera registry interface unavailable' }
    try {
        foreach ($name in @('CameraDeviceMftClsid','CameraDeviceMftClsidChain',
                             'CameraPostProcessingPluginCLSID')) {
            if ($source.GetValue($name,$null)) {
                throw "Logitech RAW source already has a camera MFT: $name"
            }
        }
    } finally { $source.Dispose() }
    if (Test-Path -LiteralPath (Join-Path $env:ProgramFiles 'AvCamDualCameraC270Lab\lab-install-state.json')) {
        throw 'Previous Logitech lab rollback state remains present'
    }
}
function Find-Lenovo([Microsoft.Win32.RegistryKey]$Root) {
    $build=[int](Get-CimInstance Win32_OperatingSystem).BuildNumber
    if ($build -lt 22621) { throw 'Requires Windows 11 22H2 or later' }
    $camera=@(Get-PnpDevice -PresentOnly | Where-Object {
        $_.Class -eq 'Camera' -and $_.Status -eq 'OK' -and
        $_.FriendlyName -eq 'Integrated Camera' -and
        $_.InstanceId -match '(?i)^USB\\VID_13D3&PID_56FF&MI_00\\'
    })
    if ($camera.Count -ne 1) { throw 'Expected exactly one healthy Lenovo Integrated Camera' }
    $path=Find-Interface $Root $camera[0].InstanceId
    Assert-InterfacePath $path
    return $path
}
function Verify-Package {
    $source=Join-Path $PSScriptRoot 'AvCamPhysicalDeviceMft.dll'
    $manifest=Join-Path $PSScriptRoot 'PASS_THROUGH_SHA256SUMS.txt'
    if (-not (Test-Path -LiteralPath $source -PathType Leaf) -or
        -not (Test-Path -LiteralPath $manifest -PathType Leaf)) {
        throw 'Extract the full 13D3 lab including the signed pass-through DMFT and manifest first'
    }
    $line=(Get-Content -LiteralPath $manifest -TotalCount 1)
    $expected=($line -split '\s+')[0]
    if ($expected -notmatch '^[0-9a-fA-F]{64}$') { throw 'Invalid SHA256 manifest' }
    $actual=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
    if ($actual -ine $expected) { throw 'Device MFT DLL SHA256 mismatch' }
    # A class factory test is not a pin-compatibility test, but it catches
    # broken builds before any COM registration or OEM chain writes.
    $probe=Join-Path $PSScriptRoot 'AvCamDmftComContract.exe'
    if (-not (Test-Path -LiteralPath $probe -PathType Leaf)) {
        throw 'AvCamDmftComContract.exe missing; refuse installing unchecked DLL'
    }
    $result=@(& $probe --inspect-dll $source)
    if ($LASTEXITCODE -ne 0 -or
        $result -notcontains 'AVCAM_DMFT_REQUIRED_COM_INTERFACES=PASS') {
        $result | ForEach-Object { Write-Host $_ }
        throw 'Pass-through Device MFT COM interface contract failed'
    }
    Write-Host 'AVCAM_PASS_THROUGH_DLL_CONTRACT_PASS'
    return $actual
}
function Save-State([string]$Path,[string]$Hash) {
    if (Test-Path -LiteralPath $StateFile) { throw 'Rollback record already exists' }
    $state=[ordered]@{
        Version=1
        RegistryPath=$Path
        Original=@($Original)
        Candidate=@($Candidate)
        DllPath=$Dll
        DllSha256=$Hash
        PreparedUtc=(Get-Date).ToUniversalTime().ToString('o')
    }
    $json=$state | ConvertTo-Json -Depth 4
    $temporary="$StateFile.$([guid]::NewGuid().ToString('N')).tmp"
    try {
        $bytes=[Text.Encoding]::UTF8.GetBytes($json)
        $stream=[IO.FileStream]::new($temporary,[IO.FileMode]::CreateNew,
                                     [IO.FileAccess]::Write,[IO.FileShare]::None)
        try {
            $stream.Write($bytes,0,$bytes.Length)
            $stream.Flush($true)
        } finally { $stream.Dispose() }
        [IO.File]::Move($temporary,$StateFile)
    } finally {
        if (Test-Path -LiteralPath $temporary) {
            Remove-Item -LiteralPath $temporary -Force
        }
    }
}
function Load-State {
    if (-not (Test-Path -LiteralPath $StateFile -PathType Leaf)) {
        throw "Original chain rollback record not found: $StateFile"
    }
    $state=Get-Content -LiteralPath $StateFile -Raw | ConvertFrom-Json
    if ($state.Version -ne 1 -or $state.DllPath -cne $Dll -or
        $state.DllSha256 -notmatch '^[0-9a-fA-F]{64}$' -or
        -not (Same ([string[]]@($state.Original)) $Original) -or
        -not (Same ([string[]]@($state.Candidate)) $Candidate)) {
        throw 'Rollback state invalid: refusing changes'
    }
    Assert-InterfacePath ([string]$state.RegistryPath)
    return $state
}
function Restore-Original {
    $state=Load-State
    $root=Open-Hklm64
    try {
        $key=$root.OpenSubKey([string]$state.RegistryPath,$true)
        if ($null -eq $key) {
            throw 'Saved Lenovo interface missing: preserve rollback state'
        }
        try {
            $current=[string[]]@(Get-Chain $key)
            if (Same $current $Candidate) {
                $key.SetValue($ChainValue,$Original,
                    [Microsoft.Win32.RegistryValueKind]::MultiString)
                $key.Flush()
                if (-not (Same ([string[]]@(Get-Chain $key)) $Original)) {
                    throw 'Restore verification failed'
                }
            } elseif (-not (Same $current $Original)) {
                throw 'OEM chain changed externally: refusing destructive rollback'
            }
        } finally { $key.Dispose() }
        Write-Host 'AVCAM_SONIX_OEM_CHAIN_RESTORED'
        $registered=Get-ComDll $root
        if ($registered) {
            if (-not [string]::Equals($registered.Trim('"'),$Dll,
                                     [StringComparison]::OrdinalIgnoreCase)) {
                throw 'AvCam CLSID points to a foreign DLL: preserve record'
            }
            Invoke-Regsvr -Unregister
            if (Get-ComDll $root) {
                throw 'AvCam COM class still registered: preserve record'
            }
        }
    } finally { $root.Dispose() }
    $archived="$StateFile.restored-$(Get-Date -Format yyyyMMdd-HHmmss).json"
    Move-Item -LiteralPath $StateFile -Destination $archived -ErrorAction Stop
    Write-Host "AVCAM_SONIX_ROLLBACK_BACKUP=$archived"
    Write-Host 'AVCAM_SONIX_CHAIN_ROLLBACK_PASS'
}
function Invoke-SelfTest {
    if (-not (Same $Original ([string[]]@($Sonix))) -or
        -not (Same $Candidate ([string[]]@($Sonix,$AvCam))) -or
        (Same $Candidate $Original) -or
        (Same ([string[]]@($AvCam,$Sonix)) $Candidate)) {
        throw 'Invalid OEM chain order or equality test'
    }
    Assert-InterfacePath "$RootPath\##?#USB#VID_13D3&PID_56FF&MI_00#SAMPLE#$Category\#GLOBAL\Device Parameters"
    $rejected=$false
    try {
        Assert-InterfacePath "$RootPath\##?#USB#VID_046D&PID_0825#SAMPLE#$Category\#GLOBAL\Device Parameters"
    } catch { $rejected=$true }
    if (-not $rejected) { throw 'Unrelated camera interface accepted' }
    if ($Dll -notlike '*AvCamSonixPassThroughDmft.dll') { throw 'Expected the isolated pass-through trial DLL name' }
    Write-Host 'AVCAM_SONIX_CHAIN_TRIAL_SELFTEST_PASS'
    Write-Host 'AVCAM_SONIX_CHAIN_TRIAL_OEM_REGISTRY_UNTOUCHED'
}

if ($Action -eq 'SelfTest') {
    Invoke-SelfTest
    return
}
if ($Action -eq 'Rollback') {
    Assert-Admin
    Restore-Original
    Write-Warning 'Restart Windows to clear FrameServer camera transforms.'
    return
}
if ($Action -eq 'Status') {
    $root=Open-Hklm64
    try {
        $path=if (Test-Path -LiteralPath $StateFile) {
            [string](Load-State).RegistryPath
        } else { Find-Lenovo $root }
        $key=$root.OpenSubKey($path,$false)
        if ($null -eq $key) { throw 'Camera interface missing' }
        try {
            $chain=[string[]]@(Get-Chain $key)
            Write-Host "AVCAM_SONIX_CHAIN_COUNT=$($chain.Count)"
            for ($i=0;$i -lt $chain.Count;$i++) {
                Write-Host "AVCAM_SONIX_CHAIN_$i=$($chain[$i])"
            }
        } finally { $key.Dispose() }
        Write-Host "AVCAM_SONIX_ROLLBACK_RECORD_PRESENT=$(Test-Path -LiteralPath $StateFile)"
        Write-Host 'AVCAM_SONIX_TRIAL_MODE=PASSTHROUGH_ONLY'
        Write-Host 'AVCAM_SONIX_STATUS_READ_ONLY_COMPLETE'
    } finally { $root.Dispose() }
    return
}

if ($Action -ne 'Install' -or -not $AcceptHardwareRisk) {
    throw 'Installation requires -Action Install -AcceptHardwareRisk'
}
Assert-Admin
$hash=Verify-Package
if (Test-Path -LiteralPath $StateFile) {
    throw 'Rollback record already exists. Restore original camera first.'
}
if (Test-Path -LiteralPath $Dll) {
    throw 'Lab DLL already exists without a rollback record: review first'
}
$root=Open-Hklm64
$stateSaved=$false
try {
    if (Get-ComDll $root) { throw 'AvCam CLSID already registered: refuse overwrite' }
    $path=Find-Lenovo $root
    Test-SourceC270 $root
    $key=$root.OpenSubKey($path,$false)
    if ($null -eq $key) { throw 'Lenovo camera interface missing' }
    try { Assert-OemBaseline $key } finally { $key.Dispose() }

    Write-Host "AVCAM_SONIX_ORIGINAL=$Sonix"
    Write-Host "AVCAM_SONIX_CANDIDATE=$Sonix,$AvCam"
    Write-Warning 'EXPERIMENTAL: appends an AvCam PASS-THROUGH MFT to the OEM chain.'
    Write-Warning 'OBS replacement remains DISABLED until Sonix pin interoperability is proven.'
    Write-Warning 'A failed MFT may prevent Windows Camera and Chrome from opening Integrated Camera.'
    Write-Warning 'Close all camera clients before installation; reboot is required afterward.'
    Write-Warning 'To recover, use this script elevated with -Action Rollback, then reboot.'
    $answer=Read-Host 'Type INSTALL_AVCAM_PASSTHROUGH_ONLY to accept this hardware trial'
    if ($answer -cne 'INSTALL_AVCAM_PASSTHROUGH_ONLY') {
        Write-Host 'AVCAM_SONIX_CHAIN_INSTALL_CANCELLED'
        return
    }
    New-Item -ItemType Directory -Force -Path $Folder | Out-Null
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'AvCamPhysicalDeviceMft.dll') -Destination $Dll
    if ((Get-FileHash -LiteralPath $Dll -Algorithm SHA256).Hash -ine $hash) {
        throw 'Copied Device MFT hash mismatch'
    }
    Save-State $path $hash
    $stateSaved=$true
    Invoke-Regsvr
    $registered=Get-ComDll $root
    if (-not [string]::Equals($registered.Trim('"'),$Dll,
                             [StringComparison]::OrdinalIgnoreCase)) {
        throw 'AvCam COM server registration not verified'
    }
    $key=$root.OpenSubKey($path,$true)
    if ($null -eq $key) { throw 'Lenovo interface is not writable' }
    try {
        Assert-OemBaseline $key
        $key.SetValue($ChainValue,$Candidate,
                      [Microsoft.Win32.RegistryValueKind]::MultiString)
        $key.Flush()
        if (-not (Same ([string[]]@(Get-Chain $key)) $Candidate)) {
            throw 'Chain verification failed'
        }
    } finally { $key.Dispose() }
    Write-Host 'AVCAM_SONIX_PASSTHROUGH_TRIAL_INSTALLED'
    Write-Host 'AVCAM_OBS_FRAME_REPLACEMENT=DISABLED'
    Write-Host 'AVCAM_SONIX_PIN_INTEROP=NOT_YET_VERIFIED'
    Write-Warning 'Restart Windows before the first camera test.'
} catch {
    $errorMessage=$_.Exception.Message
    Write-Warning "Experimental installation failed: $errorMessage"
    if ($stateSaved) {
        try { Restore-Original }
        catch {
            Write-Warning "Automatic rollback was incomplete: $($_.Exception.Message)"
            Write-Warning "Preserve $StateFile and use -Action Rollback after resolving permissions."
        }
    }
    throw
} finally { $root.Dispose() }
