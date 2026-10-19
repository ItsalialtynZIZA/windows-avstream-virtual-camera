# Run this STATIC, read-only acceptance checker in GitHub Actions.
# The actual VM PnP probe is deliberately never executed in CI.
$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$target = Join-Path $PSScriptRoot '../tools/AvCamUdeVmAcceptance.ps1'
$tokens = $null
$parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
    (Resolve-Path -LiteralPath $target).ProviderPath,
    [ref]$tokens, [ref]$parseErrors
)
if ($parseErrors.Count -gt 0) {
    foreach ($err in $parseErrors) {
        Write-Error "VM acceptance PowerShell syntax: $($err.Message) at $($err.Extent.StartLineNumber):$($err.Extent.StartColumnNumber)"
    }
    throw "VM acceptance script has $($parseErrors.Count) PowerShell parser error(s)"
}
if ($null -eq $ast) { throw "PowerShell parser did not return an AST" }
$content = Get-Content -LiteralPath $target -Raw

# Never mistake a duplicated partial footer for success: the tool must
# emit precisely one unique success marker, only after actual sample read.
$markers = [regex]::Matches(
    $content, 'Write-Host\s+"VM_ACCEPTANCE_FIRST_MEDIA_FOUNDATION_FRAME_RECEIVED"')
if ($markers.Count -ne 1) {
    throw "VM acceptance success marker must occur exactly once"
}
$requirements = @{
    "VM identity restriction" = 'virtual machine\|vmware\|virtualbox'
    "Hardware enumeration" = 'Win32_PnPEntity'
    "Isolated research controller" = 'AVCAMUDECONTROLLER'
    "Actual emulated USB hardware ID" = 'VID_1234&PID_5679'
    "Inbox Windows USB Video Class driver" = 'usbvideo'
    "Controller PnP error checked" = 'controllers\[0\]\.ConfigManagerErrorCode'
    "Camera PnP error checked" = 'camera\.ConfigManagerErrorCode'
    "Enumeration-only mode is explicit" = 'VM_ENUMERATION_ONLY: passed'
    "Actual MF sample is required" = 'MEDIA_FOUNDATION_SAMPLE_RECEIVED'
    "MF process exit code is required" = 'exitCode -ne 0'
}
foreach ($entry in $requirements.GetEnumerator()) {
    if ($content -notmatch $entry.Value) {
        throw "VM acceptance missing $($entry.Key)"
    }
}

# A static AST inspection catches accidental destructive calls, even
# when a comment legitimately mentions examples such as driver installs.
$badCommandNames = @(
    'pnputil', 'devcon', 'verifier', 'bcdedit', 'reg',
    'New-PnpDevice', 'Disable-PnpDevice', 'Enable-PnpDevice',
    'Set-ItemProperty', 'Remove-ItemProperty', 'New-ItemProperty',
    'Start-Process', 'Invoke-Expression'
)
$commands = @($ast.FindAll({
    param($node)
    $node -is [System.Management.Automation.Language.CommandAst]
}, $true))
foreach ($command in $commands) {
    $name = $command.GetCommandName()
    if ($null -ne $name -and
        $badCommandNames -contains $name) {
        throw "VM acceptance script cannot perform destructive action: $name"
    }
}

# Sanity-check parser negative control: reject a truncated, unclosed script.
$badTokens = $null
$badErrors = $null
[void][System.Management.Automation.Language.Parser]::ParseInput(
    'if ($true) {', [ref]$badTokens, [ref]$badErrors
)
if ($badErrors.Count -eq 0) {
    throw "PowerShell parser negative control failed"
}

# The guest-only prerequisite checker is just as safety critical as the
# acceptance script. Parse its AST and deny any mutation/installer commands.
$preflightPath = Join-Path $PSScriptRoot '../tools/AvCamUdeVmPreflight.ps1'
$preflightTokens = $null
$preflightParseErrors = $null
$preflightAst = [System.Management.Automation.Language.Parser]::ParseFile(
    (Resolve-Path -LiteralPath $preflightPath).ProviderPath,
    [ref]$preflightTokens, [ref]$preflightParseErrors
)
if ($preflightParseErrors.Count -ne 0 -or $null -eq $preflightAst) {
    throw 'VM guest preflight has invalid PowerShell syntax.'
}
$preflightText = Get-Content -LiteralPath $preflightPath -Raw
$preflightChecks = @{
    'VM-only manufacturer and model check' = 'VM_PREFLIGHT_REFUSED'
    'Windows 11 guest build check' = 'Build -lt 22000'
    '64-bit guest gate' = 'Is64BitOperatingSystem'
    'Minimum memory safety gate' = 'memoryGiB -lt 4'
    'Minimum free disk space gate' = 'freeGiB -lt 8'
    'Expected research USB identity check' = 'VID_1234&PID_5679'
    'Guest-only success marker' = 'VM_PREFLIGHT_GUEST_READY'
    'Host-side checkpoint reminder' = 'snapshot on the HOST'
}
foreach ($check in $preflightChecks.GetEnumerator()) {
    if ($preflightText -notmatch [regex]::Escape($check.Value)) {
        throw "Guest preflight missing $($check.Key)"
    }
}
$preflightCommands = @($preflightAst.FindAll({
    param($node)
    $node -is [System.Management.Automation.Language.CommandAst]
}, $true))
foreach ($command in $preflightCommands) {
    $name = $command.GetCommandName()
    if ($null -ne $name -and $badCommandNames -contains $name) {
        throw "VM guest preflight cannot modify the machine: $name"
    }
}
Write-Host "VM_PREFLIGHT_STATIC_CONTRACT_PASSED"
Write-Host "VM_ACCEPTANCE_STATIC_CONTRACT_PASSED"
