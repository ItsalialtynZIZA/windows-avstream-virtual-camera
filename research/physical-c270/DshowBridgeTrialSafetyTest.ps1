# Static no-hardware safety regression for the experimental C270 DShow Bridge trial.
# Does NOT edit HKLM, enumerate real camera devices, or enable trial mode.
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$path=Join-Path $PSScriptRoot 'AvCamDshowBridgeTrial.ps1'
$s=[IO.File]::ReadAllText($path)
$tokens=$null;$errors=$null
$null=[System.Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$errors)
if ($errors.Count -gt 0) { throw ("TRIAL_SYNTAX_INVALID: " + ($errors -join '; ')) }
function Need([bool]$condition,[string]$label) {
    if (-not $condition) { throw ('DSHOW_TRIAL_SAFETY_FAILED: '+$label) }
    Write-Host ('DSHOW_TRIAL_SAFETY_OK: '+$label)
}
$restore=$s.IndexOf('if ($Restore) {')
$find=$s.IndexOf('$target = Find-C270Key')
Need ($restore -ge 0 -and $find -gt $restore) 'restore-before-present-device-lookup'
$restoreBody=$s.Substring($restore,$find-$restore)
Need (-not ([regex]::IsMatch($restoreBody,'(?m)^\s*(?!#).*Get-PnpDevice'))) 'rollback-independent-of-pnp-presence'
Need ($restoreBody.Contains('Restore') -and $restoreBody.Contains('ConvertFrom-Json')) 'rollback-reads-persisted-snapshot'
Need ($restoreBody.Contains('DeleteValue($property,$false)')) 'rollback-removes-property-if-originally-absent'
Need ($restoreBody.Contains('SetValue($property,[int]$snapshot.Value')) 'rollback-restores-original-dword'
Need ($restoreBody.Contains('$id.StartsWith($expectedDevice')) 'rollback-verifies-camera-hardware-id'
Need ($restoreBody.Contains('$reg -cne $expectedRegistryPath')) 'rollback-verifies-exact-device-registry-path'
Need ($restoreBody.Contains('BACKUP_RETAINED')) 'rollback-preserves-restore-evidence'
$write=$s.LastIndexOf('$key.SetValue($property,($previous -bor 1)')
$save=$s.IndexOf('[System.IO.File]::WriteAllText($BackupFile')
Need ($save -ge 0 -and $write -gt $save) 'snapshot-persisted-before-registry-change'
Need ($s.Contains("ENABLE_C270_DSHOW_BRIDGE")) 'interactive-owner-opt-in'
Need ($s.Contains('CameraDeviceMftClsidChain')) 'reject-existing-mft-chain'
Write-Host 'DSHOW_TRIAL_SAFETY_STATIC_CHECK_PASSED_NO_DEVICE_CHANGES'
