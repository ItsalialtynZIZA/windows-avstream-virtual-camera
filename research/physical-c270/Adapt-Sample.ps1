# Adapts a pinned checkout of Microsoft's sample Device MFT.
# Microsoft sample source stays in its own checkout and retains its license.
param([Parameter(Mandatory=$true)][string]$SampleDir)
$ErrorActionPreference='Stop'
$base = (Resolve-Path $SampleDir).Path
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
foreach ($name in @('AvCamPhysicalWire.h','AvCamPhysicalHook.h','AvCamPhysicalHook.cpp')) {
    Copy-Item -LiteralPath (Join-Path $here $name) -Destination (Join-Path $base $name) -Force
}
function Replace-Once([string]$path,[string]$before,[string]$after) {
    $text = [IO.File]::ReadAllText($path)
    $count = ([regex]::Matches($text, [regex]::Escape($before))).Count
    if ($count -ne 1) { throw "Expected one occurrence of patch anchor in $path; got $count" }
    [IO.File]::WriteAllText($path, $text.Replace($before,$after),
       [Text.UTF8Encoding]::new($false))
}
$helpers = Join-Path $base 'multipinmfthelpers.cpp'
Replace-Once $helpers 'CPinQueue::~CPinQueue( )' 'CPinQueue::~CPinQueue( )'
# Inject includes after the sample's own headers, before method definitions.
$text=[IO.File]::ReadAllText($helpers)
$text='#include "AvCamPhysicalHook.h"' + [Environment]::NewLine + $text
[IO.File]::WriteAllText($helpers,$text,[Text.UTF8Encoding]::new($false))
Replace-Once $helpers @'
CPinQueue::~CPinQueue( )
{
    Ctee::ReleaseTee(m_spTeer);
}
'@ @'
CPinQueue::~CPinQueue( )
{
    AvCamRemoveQueue(this);
    Ctee::ReleaseTee(m_spTeer);
}
'@
Replace-Once $helpers @'
STDMETHODIMP_(VOID) CPinQueue::InsertInternal( _In_ IMFSample *pSample )
{
    HRESULT hr = ExceptionBoundary([&]()
'@ @'
STDMETHODIMP_(VOID) CPinQueue::InsertInternal( _In_ IMFSample *pSample )
{
    ComPtr<IMFSample> avcamSample;
    if (AvCamReplaceMjpegSample(this, pSample, avcamSample.GetAddressOf()))
        pSample = avcamSample.Get();
    HRESULT hr = ExceptionBoundary([&]()
'@
Replace-Once $helpers @'
    Ctee::ReleaseTee(m_spTeer);

    ComPtr<CNullTee> spNulltee = new (std::nothrow) CNullTee(this);
'@ @'
    Ctee::ReleaseTee(m_spTeer);
    AvCamSetQueueFormat(this, inMediatype, outMediatype);

    ComPtr<CNullTee> spNulltee = new (std::nothrow) CNullTee(this);
'@
$common=Join-Path $base 'common.h'
Replace-Once $common @'
DEFINE_GUID(CLSID_MultiPinMFT,
    0xe313280, 0x3169, 0x4f41, 0xa3, 0x29, 0x9e, 0x85, 0x41, 0x69, 0x63, 0x4f);
'@ @'
DEFINE_GUID(CLSID_MultiPinMFT,
0x1de4c92f, 0xa801, 0x45bb, 0x9d, 0x5a, 0x74, 0x72, 0x3a, 0x8e, 0xf4, 0x60);
'@
$project=Join-Path $base 'multipinmft.vcxproj'
Replace-Once $project '</Project>' @'
  <ItemGroup>
    <ClCompile Include="AvCamPhysicalHook.cpp">
      <PrecompiledHeader>NotUsing</PrecompiledHeader>
      <WppEnabled>false</WppEnabled>
    </ClCompile>
  </ItemGroup>
</Project>
'@
Write-Host 'AVCAM_PHYSICAL_MFT_SAMPLE_PATCH_APPLIED'
