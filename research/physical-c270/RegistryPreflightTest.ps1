# Regression: Windows PowerShell 5.1 registry interface lookup must not
# recurse into #GLOBAL\Properties and must use the present device instance.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$scriptPath = Join-Path $PSScriptRoot 'AvCamPhysicalInstall.ps1'
$tokens = $null
$errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
    $scriptPath, [ref]$tokens, [ref]$errors)
if ($errors.Count -ne 0) { throw 'Installer parse failed' }
$node = $ast.Find({
    param($n)
    $n -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
    $n.Name -eq 'Get-PhysicalInterface'
}, $true)
if (-not $node) { throw 'Missing physical Logitech lookup function' }
if ($node.Extent.Text -match '(?i)\-Recurse\b') {
    throw 'Physical camera lookup must not enumerate restricted registry subtrees'
}
Invoke-Expression $node.Extent.Text
$cameraClass = '{E5323777-F976-4F5B-9B55-B94699C46E44}'
$real = 'USB\VID_046D&PID_0825&MI_00\7&241E5F6C&0&0000'
$stale = 'USB\VID_046D&PID_0825&MI_00\7&99999999&0&0000'
$registryRoot = 'Registry::HKEY_CURRENT_USER\Software\AvCamPhysicalRegistryCI-' + [guid]::NewGuid().ToString('N')
try {
    New-Item -Path $registryRoot -Force -ErrorAction Stop | Out-Null
    foreach ($instance in @($real,$stale)) {
        $encoded = $instance.Replace('\','#')
        $name = '##?#' + $encoded + '#' + $cameraClass
        $interface = Join-Path $registryRoot $name
        New-Item -Path $interface -Force | Out-Null
        $global = Join-Path $interface '#GLOBAL'
        New-Item -Path $global -Force | Out-Null
        New-Item -Path (Join-Path $global 'Device Parameters') -Force | Out-Null
        New-Item -Path (Join-Path $global 'Properties') -Force | Out-Null
    }
    $key = Get-PhysicalInterface -InstanceId $real -RegistryRoot $registryRoot
    if (-not $key -or $key.Name -notmatch [regex]::Escape('241E5F6C')) {
        throw 'Lookup did not bind to present physical instance'
    }
    $unknown = $real.Replace('241E5F6C','00000000')
    $rejected = $false
    try {
        $null = Get-PhysicalInterface -InstanceId $unknown -RegistryRoot $registryRoot
    } catch {
        if ($_.Exception.Message -like '*Expected exactly one*') { $rejected = $true }
        else { throw }
    }
    if (-not $rejected) { throw 'Lookup did not fail closed for absent camera' }
    Write-Host 'AVCAM_PHYSICAL_C270_REGISTRY_LOOKUP_NONRECURSIVE_PASSED'
} finally {
    Remove-Item -LiteralPath $registryRoot -Recurse -Force -ErrorAction SilentlyContinue
}
