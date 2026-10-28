# Sonix Device MFT chain transaction rehearsal. The ONLY write target is a unique HKCU self-test key.
# This file deliberately has no production install mode. No OEM, camera or HKLM changes.
[CmdletBinding()]
param([switch]$SelfTest)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$OEM='{5A50829A-86DD-4D18-8685-891EEE643C24}'
$AvCam='{1DE4C92F-A801-45BB-9D5A-74723A8EF460}'
$value='CameraDeviceMftClsidChain'

function Exact([string[]]$a,[string[]]$b) {
    if($a.Count -ne $b.Count){ return $false }
    for($i=0;$i -lt $a.Count;$i++){ if($a[$i] -cne $b[$i]){return $false} }
    return $true
}
function Assert-Sandbox([string]$path){
    if(-not $path.StartsWith('Software\AvCam\SonixChainTransactionTest\',[StringComparison]::Ordinal)){
        throw 'Refusing non-sandbox registry path'
    }
}
function Read-Chain([Microsoft.Win32.RegistryKey]$key){
    if($null -eq $key){throw 'Registry key missing'}
    if($key.GetValueNames() -cnotcontains $value){throw 'Chain missing'}
    if($key.GetValueKind($value) -ne [Microsoft.Win32.RegistryValueKind]::MultiString){
        throw 'Chain registry type is not REG_MULTI_SZ'
    }
    return [string[]]@($key.GetValue($value,[string[]]@()))
}
function Prepare([Microsoft.Win32.RegistryKey]$key,[string]$path,[string]$state){
    Assert-Sandbox $path
    if(Test-Path -LiteralPath $state){throw 'Rollback state already exists'}
    $original=[string[]]@(Read-Chain $key)
    if(-not (Exact $original ([string[]]@($OEM)))){throw 'Unexpected OEM baseline'}
    $candidate=[string[]]@($OEM,$AvCam)
    $record=[ordered]@{
        Version=1
        Hive='HKEY_CURRENT_USER'
        Path=$path
        Name=$value
        Kind='MultiString'
        Phase='Prepared'
        Before=@($original)
        After=@($candidate)
    }
    $record | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $state -Encoding UTF8
    $verified=Get-Content -LiteralPath $state -Raw | ConvertFrom-Json
    if($verified.Version -ne 1 -or $verified.Path -cne $path -or
       -not (Exact ([string[]]@($verified.Before)) $original) -or
       -not (Exact ([string[]]@($verified.After)) $candidate)){
        throw 'Persisted rollback state did not verify'
    }
    if(-not (Exact ([string[]]@(Read-Chain $key)) $original)){
        throw 'OEM state changed after snapshot'
    }
    $key.SetValue($value,$candidate,[Microsoft.Win32.RegistryValueKind]::MultiString)
    $key.Flush()
    if(-not (Exact ([string[]]@(Read-Chain $key)) $candidate)){throw 'Candidate verify failed'}
}
function Restore([Microsoft.Win32.RegistryKey]$key,[string]$path,[string]$state){
    Assert-Sandbox $path
    if(-not (Test-Path -LiteralPath $state -PathType Leaf)){throw 'Rollback record missing'}
    $r=Get-Content -LiteralPath $state -Raw | ConvertFrom-Json
    $before=[string[]]@($r.Before)
    $after=[string[]]@($r.After)
    if($r.Version -ne 1 -or $r.Hive -cne 'HKEY_CURRENT_USER' -or
       $r.Path -cne $path -or $r.Name -cne $value -or
       $r.Kind -cne 'MultiString' -or $r.Phase -cne 'Prepared' -or
       -not (Exact $before ([string[]]@($OEM))) -or
       -not (Exact $after ([string[]]@($OEM,$AvCam)))){
        throw 'Rollback record tampered or unexpected'
    }
    $current=[string[]]@(Read-Chain $key)
    if(Exact $current $before){return 'ALREADY_ORIGINAL'}
    if(-not (Exact $current $after)){throw 'CONCURRENT_CHAIN_CHANGE'}
    $key.SetValue($value,$before,[Microsoft.Win32.RegistryValueKind]::MultiString)
    $key.Flush()
    if(-not (Exact ([string[]]@(Read-Chain $key)) $before)){
        throw 'Rollback verification failed; preserve saved state'
    }
    return 'RESTORED'
}
function Run-SandboxSelfTest {
    $id=[guid]::NewGuid().ToString('N')
    $path="Software\AvCam\SonixChainTransactionTest\$id"
    Assert-Sandbox $path
    $dir=Join-Path $env:TEMP "AvCamSonixTransaction-$id"
    New-Item -ItemType Directory -Path $dir -ErrorAction Stop | Out-Null
    $root=[Microsoft.Win32.RegistryKey]::CurrentUser
    $key=$null
    try {
        $key=$root.CreateSubKey($path)
        if($null -eq $key){throw 'Sandbox registry creation failed'}
        $key.SetValue($value,[string[]]@($OEM),[Microsoft.Win32.RegistryValueKind]::MultiString)
        $one=Join-Path $dir 'normal.json'
        Prepare $key $path $one
        if((Restore $key $path $one) -cne 'RESTORED'){throw 'Normal restore failed'}
        if((Restore $key $path $one) -cne 'ALREADY_ORIGINAL'){throw 'Idempotency failed'}
        $two=Join-Path $dir 'fault.json'
        try {
            Prepare $key $path $two
            throw 'INJECTED_FAULT_AFTER_WRITE'
        } catch {
            if($_.Exception.Message -ne 'INJECTED_FAULT_AFTER_WRITE'){throw}
            if((Restore $key $path $two) -cne 'RESTORED'){throw 'Fault recovery failed'}
        }
        $three=Join-Path $dir 'concurrent.json'
        Prepare $key $path $three
        $foreign=[string[]]@($OEM,'{00000000-0000-0000-0000-000000000042}')
        $key.SetValue($value,$foreign,[Microsoft.Win32.RegistryValueKind]::MultiString)
        $refused=$false
        try{$null=Restore $key $path $three}catch{
            if($_.Exception.Message -eq 'CONCURRENT_CHAIN_CHANGE'){$refused=$true}else{throw}
        }
        if(-not $refused -or -not (Exact ([string[]]@(Read-Chain $key)) $foreign)){
            throw 'Concurrent mutation was not preserved'
        }
        $key.SetValue($value,$OEM,[Microsoft.Win32.RegistryValueKind]::String)
        $wrongTypeRejected=$false
        try{$null=Read-Chain $key}catch{$wrongTypeRejected=$true}
        if(-not $wrongTypeRejected){throw 'REG_SZ accepted as REG_MULTI_SZ'}
        Write-Host 'AVCAM_SONIX_TRANSACTION_NORMAL_ROLLBACK_PASS'
        Write-Host 'AVCAM_SONIX_TRANSACTION_FAULT_RECOVERY_PASS'
        Write-Host 'AVCAM_SONIX_TRANSACTION_CONCURRENT_GUARD_PASS'
        Write-Host 'AVCAM_SONIX_TRANSACTION_REGTYPE_GUARD_PASS'
        Write-Host 'AVCAM_SONIX_TRANSACTION_SELFTEST_PASS'
    } finally {
        if($key){$key.Dispose()}
        $root.DeleteSubKeyTree($path,$false)
        if(Test-Path -LiteralPath $dir){Remove-Item -LiteralPath $dir -Recurse -Force}
    }
}
if($SelfTest){Run-SandboxSelfTest;return}
Write-Host 'AVCAM_SONIX_TRANSACTION_INSTALL=BLOCKED'
Write-Host 'AVCAM_SONIX_TRANSACTION_ONLY_HKCU_SELFTEST_SUPPORTED'
