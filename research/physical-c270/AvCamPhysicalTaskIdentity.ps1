# Identity check for the existing, explicitly named AvCam scheduled task.
# Task Scheduler may serialize the current local account as its short user name.
# Match SID first; never trust an arbitrary account or task executable.
function Test-AvCamTaskPrincipal {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)][string]$TaskUserId,
        [Parameter(Mandatory=$true)][string]$CurrentAccount,
        [Parameter(Mandatory=$true)][string]$CurrentSid
    )
    if ($TaskUserId -ieq $CurrentAccount -or $TaskUserId -ieq $CurrentSid) {
        return $true
    }
    # Prefer SID resolution. If a different account resolves successfully,
    # do not fall back to a matching display name.
    try {
        $ntAccount = New-Object Security.Principal.NTAccount($TaskUserId)
        $resolved = $ntAccount.Translate([Security.Principal.SecurityIdentifier])
        return $resolved.Value -ieq $CurrentSid
    } catch [Security.Principal.IdentityNotMappedException] {
        # Task Scheduler on some Windows installations returns an unqualified
        # account name that cannot be translated from the elevated session.
        # This narrow fallback is allowed ONLY for the exact current leaf name.
        # Assert-TaskOwner additionally verifies the exact AvCam EXE path.
        if ($TaskUserId.Contains('\') -or $TaskUserId.Contains('@') -or
            $TaskUserId.Contains('/')) { return $false }
        $parts = $CurrentAccount.Split('\')
        $leaf = $parts[$parts.Length - 1]
        return $TaskUserId -ieq $leaf
    }
}
