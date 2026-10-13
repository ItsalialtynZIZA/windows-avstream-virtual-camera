param(
    [ValidateSet('Debug','Release')]
    [string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$solution = Join-Path $repo 'AvCam.sln'
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"

if (-not (Test-Path $vswhere)) {
    throw 'Visual Studio Installer/vswhere.exe not found. Install Visual Studio 2022 with Desktop development with C++ and WDK.'
}

$vs = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
if (-not $vs) { throw 'MSBuild installation not found.' }

$msbuild = Join-Path $vs 'MSBuild\Current\Bin\MSBuild.exe'
if (-not (Test-Path $msbuild)) { throw "MSBuild not found at $msbuild" }

Write-Host "Building AvCam ($Configuration|x64)..."
& $msbuild $solution /m /t:Build "/p:Configuration=$Configuration" /p:Platform=x64
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host 'Build completed.'
Get-ChildItem $repo -Recurse -Include AvCam.sys,AvCam.inf,AvCam.cat -ErrorAction SilentlyContinue |
    Select-Object FullName,Length,LastWriteTime
