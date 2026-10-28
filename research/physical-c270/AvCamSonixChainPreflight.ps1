# Read-only inventory for Lenovo 82K1 Integrated Camera (13D3:56FF) and its OEM Device MFT chain.
# Never changes USB drivers, COM registration, camera interface values, ACLs or the video stream.
[CmdletBinding()]
param([switch]$SelfTest)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$targetVidPid = 'VID_13D3&PID_56FF'
$cameraCategory = '{E5323777-F976-4F5B-9B55-B94699C46E44}'
$expectedSonix = '{5A50829A-86DD-4D18-8685-891EEE643C24}'
$untrustedSids = @('S-1-5-32-545', 'S-1-5-11', 'S-1-1-0')

function Test-WriteMask {
    param([int64]$Rights)
    # KEY_SET_VALUE, KEY_CREATE_SUB_KEY, KEY_CREATE_LINK, DELETE,
    # WRITE_DAC, WRITE_OWNER, GENERIC_WRITE, GENERIC_ALL.
    $writeBits = [int64]0x2 -bor 0x4 -bor 0x20 -bor 0x10000 -bor
                 0x40000 -bor 0x80000 -bor 0x40000000 -bor 0x10000000
    return (($Rights -band $writeBits) -ne 0)
}
function Test-GuidChain {
    param([string[]]$Items, [int]$Build)
    if ($Items.Count -eq 0) { return $false }
    $max = if ($Build -ge 22621) { 4 } else { 2 }
    if ($Items.Count -gt $max) { return $false }
    $seen = @{}
    foreach ($item in $Items) {
        $guid = [guid]::Empty
        if (-not [guid]::TryParseExact($item, 'B', [ref]$guid)) {
            return $false
        }
        if ($seen.ContainsKey($guid.ToString())) { return $false }
        $seen.Add($guid.ToString(), $true)
    }
    return $true
}
function Invoke-SelfTest {
    $a = '{5A50829A-86DD-4D18-8685-891EEE643C24}'
    $b = '{1DE4C92F-A801-45BB-9D5A-74723A8EF460}'
    if (-not (Test-GuidChain @($a) 26200)) { throw 'One-element chain failed' }
    if (-not (Test-GuidChain @($a, $b) 26200)) { throw 'Two-element chain failed' }
    if (Test-GuidChain @($a, $a) 26200) { throw 'Duplicate chain accepted' }
    if (Test-GuidChain @('not-a-guid') 26200) { throw 'Malformed chain accepted' }
    if (Test-GuidChain @($a, $b, $a, $b, $a) 26200) {
        throw 'Overlong Windows 11 chain accepted'
    }
    if (Test-GuidChain @($a, $b, '{F0000000-0000-0000-0000-000000000001}') 19045) {
        throw 'Overlong Windows 10 chain accepted'
    }
    if (Test-WriteMask ([int64][int32]::MinValue)) {
        throw 'GENERIC_READ was classified as write'
    }
    if (Test-WriteMask 0x20019) { throw 'ReadKey was classified as write' }
    if (-not (Test-WriteMask 0xF003F)) { throw 'FullControl was not detected' }
    if (-not (Test-WriteMask 0x40000000)) { throw 'GENERIC_WRITE was not detected' }
    Write-Host 'AVCAM_SONIX_CHAIN_PREFLIGHT_SELFTEST_PASS'
}

if ($SelfTest) {
    Invoke-SelfTest
    return
}

if (-not [Environment]::Is64BitProcess) { throw '64-bit PowerShell required' }
if (-not (Get-Command Get-PnpDevice -ErrorAction SilentlyContinue)) {
    throw 'Windows PnP module not available'
}
$os = Get-CimInstance Win32_OperatingSystem
$build = [int]$os.BuildNumber
if ($build -lt 22000) { throw 'Windows 11 required for this host-specific audit' }

$physical = @(Get-PnpDevice -PresentOnly | Where-Object {
    $_.InstanceId -match '(?i)^USB\\VID_13D3&PID_56FF&MI_00\\' -and
    $_.Class -eq 'Camera' -and $_.Status -eq 'OK' -and
    $_.FriendlyName -eq 'Integrated Camera'
})
Write-Host "Integrated Camera matches: $($physical.Count)"
if ($physical.Count -ne 1) {
    throw 'Expected exactly one healthy 13D3:56FF Integrated Camera. No changes made.'
}

$root = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
    [Microsoft.Win32.RegistryHive]::LocalMachine,
    [Microsoft.Win32.RegistryView]::Registry64
)

try {
    $base = "SYSTEM\CurrentControlSet\Control\DeviceClasses\$cameraCategory"
    $deviceRoot = $root.OpenSubKey($base, $false)
    if ($null -eq $deviceRoot) {
        throw 'VIDEO_CAMERA device interface registry root is unavailable'
    }
    try {
        $encoded = $physical[0].InstanceId.Replace('\', '#')
        $suffix = "#$cameraCategory"
        $matches = @(
            foreach ($name in $deviceRoot.GetSubKeyNames()) {
                if ($name.IndexOf($encoded, [StringComparison]::OrdinalIgnoreCase) -lt 0 -or
                    -not $name.EndsWith($suffix, [StringComparison]::OrdinalIgnoreCase)) {
                    continue
                }
                $relative = "$name\#GLOBAL\Device Parameters"
                $trial = $deviceRoot.OpenSubKey($relative, $false)
                if ($null -ne $trial) {
                    $trial.Dispose()
                    "$base\$relative"
                }
            }
        )
    }
    finally {
        $deviceRoot.Dispose()
    }

    if ($matches.Count -ne 1) {
        throw "Expected exactly one VIDEO_CAMERA Device Parameters key; found $($matches.Count)"
    }
    $interface = $root.OpenSubKey($matches[0], $false)
    if ($null -eq $interface) { throw 'Camera registry interface disappeared' }
    try {
        $names = @($interface.GetValueNames())
        $legacy = if ($names -contains 'CameraDeviceMftClsid') {
            [string]$interface.GetValue('CameraDeviceMftClsid', '')
        } else { '' }
        $present = ($names -contains 'CameraDeviceMftClsidChain')
        $kind = if ($present) {
            [string]$interface.GetValueKind('CameraDeviceMftClsidChain')
        } else { 'Absent' }
        $chain = @()
        if ($present -and $kind -eq 'MultiString') {
            $chain = @([string[]]$interface.GetValue(
                'CameraDeviceMftClsidChain', [string[]]@(),
                [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames
            ))
        }
    }
    finally {
        $interface.Dispose()
    }

    Write-Host "Windows build: $build"
    Write-Host "Camera endpoint: Integrated Camera ($targetVidPid)"
    Write-Host "Legacy Device MFT: $legacy"
    Write-Host "OEM chain registry type: $kind"
    Write-Host "OEM chain entries: $($chain.Count)"
    if ($kind -ne 'MultiString') {
        Write-Warning 'OEM chain is missing or not REG_MULTI_SZ. No registration should be attempted.'
        Write-Host 'AVCAM_CHAIN_INTEROP=NOT_VERIFIED'
        return
    }
    if (-not (Test-GuidChain -Items $chain -Build $build)) {
        Write-Warning 'Malformed, duplicate or overlong MFT chain. No registration should be attempted.'
        Write-Host 'AVCAM_CHAIN_INTEROP=NOT_VERIFIED'
        return
    }
    $sonixFound = @($chain | Where-Object { $_ -ieq $expectedSonix }).Count -eq 1
    if (-not $sonixFound) {
        Write-Warning 'Expected Sonix CLSID was not found in OEM chain.'
    }
    $aclUnsafe = $false
    for ($i = 0; $i -lt $chain.Count; $i++) {
        $clsid = $chain[$i]
        $classPath = "SOFTWARE\Classes\CLSID\$clsid"
        $comKey = $root.OpenSubKey("$classPath\InprocServer32", $false)
        Write-Host "Chain[$i]: $clsid"
        if ($null -eq $comKey) {
            Write-Warning "COM registration missing for chain[$i]"
            continue
        }
        try {
            $dll = [string]$comKey.GetValue('', '')
            $dll = [Environment]::ExpandEnvironmentVariables($dll.Trim('"'))
            Write-Host "  COM DLL: $dll"
            if (Test-Path -LiteralPath $dll -PathType Leaf) {
                $sig = Get-AuthenticodeSignature -LiteralPath $dll
                Write-Host "  Signature: $($sig.Status)"
            } else {
                Write-Warning '  Registered COM DLL was not found'
            }
        }
        finally {
            $comKey.Dispose()
        }

        if ($clsid -ieq $expectedSonix) {
            foreach ($path in @($classPath, "$classPath\InprocServer32")) {
                $inspect = $root.OpenSubKey($path, $false)
                if ($null -eq $inspect) {
                    Write-Warning "Missing Sonix ACL audit key: $path"
                    continue
                }
                try {
                    $rules = $inspect.GetAccessControl().GetAccessRules(
                        $true, $true,
                        [System.Security.Principal.SecurityIdentifier]
                    )
                    foreach ($rule in $rules) {
                        if ($rule.AccessControlType -ne 'Allow' -or
                            $untrustedSids -notcontains $rule.IdentityReference.Value) {
                            continue
                        }
                        $isWrite = Test-WriteMask ([int64]$rule.RegistryRights)
                        if ($isWrite) {
                            $aclUnsafe = $true
                            Write-Warning "Possible untrusted write ACE: $path SID=$($rule.IdentityReference.Value) Inherited=$($rule.IsInherited)"
                        }
                    }
                }
                finally {
                    $inspect.Dispose()
                }
            }
        }
    }
    if ($aclUnsafe) {
        Write-Host 'AVCAM_SONIX_ACL=REVIEW'
    } elseif ($sonixFound) {
        Write-Host 'AVCAM_SONIX_ACL=NO_UNTRUSTED_ALLOW_WRITE_ACE_FOUND'
    } else {
        Write-Host 'AVCAM_SONIX_ACL=NOT_CHECKED'
    }
    Write-Host 'AVCAM_CHAIN_INTEROP=NOT_VERIFIED'
    Write-Host 'AVCAM_CHAIN_PREFLIGHT_READ_ONLY_COMPLETE'
}
finally {
    $root.Dispose()
}
