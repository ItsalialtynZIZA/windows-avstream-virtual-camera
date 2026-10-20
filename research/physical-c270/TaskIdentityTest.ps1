# Regression for Task Scheduler normalizing UserId to a short account name.
# Does not create, update, delete, or start any scheduled task.
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'AvCamPhysicalTaskIdentity.ps1')

function Assert-Expected([string]$name,[bool]$actual,[bool]$expected) {
    if ($actual -ne $expected) { throw "$name expected=$expected actual=$actual" }
}
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$full = $identity.Name
$sid = $identity.User.Value
$parts = $full.Split('\')
$leaf = $parts[$parts.Length - 1]
Assert-Expected 'QualifiedName' (Test-AvCamTaskPrincipal -TaskUserId $full -CurrentAccount $full -CurrentSid $sid) $true
Assert-Expected 'CurrentSID' (Test-AvCamTaskPrincipal -TaskUserId $sid -CurrentAccount $full -CurrentSid $sid) $true
Assert-Expected 'ShortCurrentUsername' (Test-AvCamTaskPrincipal -TaskUserId $leaf -CurrentAccount $full -CurrentSid $sid) $true
Assert-Expected 'DifferentSID' (Test-AvCamTaskPrincipal -TaskUserId 'S-1-5-18' -CurrentAccount $full -CurrentSid $sid) $false
Assert-Expected 'OtherKnownAccount' (Test-AvCamTaskPrincipal -TaskUserId 'NT AUTHORITY\SYSTEM' -CurrentAccount $full -CurrentSid $sid) $false
Assert-Expected 'SameLeafOtherMachine' (Test-AvCamTaskPrincipal -TaskUserId ('UnrelatedPC\' + $leaf) -CurrentAccount $full -CurrentSid $sid) $false
Assert-Expected 'DifferentLeaf' (Test-AvCamTaskPrincipal -TaskUserId 'DifferentUnusedAvCamUser012345' -CurrentAccount $full -CurrentSid $sid) $false
# Synthetic case exactly matching the bug report: Task Scheduler returned
# only the unqualified username for a localized Windows account.
Assert-Expected 'LocalizedUnqualifiedTaskPrincipal' (
    Test-AvCamTaskPrincipal -TaskUserId 'Думан Какимжанов' -CurrentAccount 'DESKTOP-AVCAM\Думан Какимжанов' -CurrentSid 'S-1-5-21-987600000-987600000-987600000-1001'
) $true
Assert-Expected 'LocalizedQualifiedOtherPrincipal' (
    Test-AvCamTaskPrincipal -TaskUserId 'OTHER-PC\Думан Какимжанов' -CurrentAccount 'DESKTOP-AVCAM\Думан Какимжанов' -CurrentSid 'S-1-5-21-987600000-987600000-987600000-1001'
) $false

# Test the whole existing-task guard, including refusing a different EXE.
. (Join-Path $PSScriptRoot 'AvCamPhysicalControl.ps1') -Action Status
$goodTask = [pscustomobject]@{
    Principal = [pscustomobject]@{ UserId = $leaf }
    Actions = @([pscustomobject]@{ Execute = $bridgeFile })
}
Assert-TaskOwner $goodTask
$wrongExe = [pscustomobject]@{
    Principal = [pscustomobject]@{ UserId = $leaf }
    Actions = @([pscustomobject]@{ Execute = 'C:\Windows\System32\notepad.exe' })
}
$denied = $false
try { Assert-TaskOwner $wrongExe } catch { $denied = $true }
Assert-Expected 'WrongTaskExecutableRejected' $denied $true
$wrongOwner = [pscustomobject]@{
    Principal = [pscustomobject]@{ UserId = 'NT AUTHORITY\SYSTEM' }
    Actions = @([pscustomobject]@{ Execute = $bridgeFile })
}
$denied = $false
try { Assert-TaskOwner $wrongOwner } catch { $denied = $true }
Assert-Expected 'OtherAccountRejected' $denied $true
Write-Host 'AVCAM_TASK_PRINCIPAL_OWNERSHIP_REGRESSION_PASSED'
