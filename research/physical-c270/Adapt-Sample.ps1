# Adapts a pinned checkout of Microsoft's sample Device MFT.
# Microsoft sample source stays in its own checkout and retains its license.
param([Parameter(Mandatory=$true)][string]$SampleDir)
$ErrorActionPreference='Stop'
$base = (Resolve-Path $SampleDir).Path
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
foreach ($name in @('AvCamPhysicalWire.h','AvCamPhysicalHook.h','AvCamMftBuildSafety.h','AvCamPerClientRoutingPolicy.h','AvCamDmftMediaContract.h','AvCamVgaFrameConvert.h','AvCamPhysicalHook.cpp')) {
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
    // Never allow AvCam allocation/metadata exceptions to escape the
    // Microsoft sample's COM method before its own ExceptionBoundary.
    ComPtr<IMFSample> avcamSample;
    IMFSample* const originalSample = pSample;
    bool useReplacement = false;
    try {
        useReplacement = AvCamReplaceMjpegSample(
            this, originalSample, avcamSample.GetAddressOf());
    } catch (...) {
        // Do not change pSample inside the try block: Reset() might otherwise
        // free the replacement while pSample still points to it.
        AvCamTrace(L"SampleSkipped: replacement exception; original frame retained");
        avcamSample.Reset();
        useReplacement = false;
    }
    if (useReplacement && avcamSample)
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
# On modern Windows a sample queue can use the allocator-mode path, not
# RecreateTee(). Both paths must record the negotiated media format.
Replace-Once $helpers @'
    Ctee::ReleaseTee(m_spTeer);// Should release the reference

    ComPtr<CNullTee> spNulltee = new (std::nothrow) CNullTee(this);
'@ @'
    Ctee::ReleaseTee(m_spTeer);// Should release the reference
    AvCamSetQueueFormat(this, inMediatype, outMediatype);

    ComPtr<CNullTee> spNulltee = new (std::nothrow) CNullTee(this);
'@

# Make it possible to distinguish COM registration/activation from an
# actual camera pipeline calling IMFDeviceTransform::InitializeTransform.
$mftImpl = Join-Path $base 'multipinmft.cpp'
$mftContent = [IO.File]::ReadAllText($mftImpl)
$mftContent = '#include "AvCamDmftMediaContract.h"' + [Environment]::NewLine +
              '#include "AvCamPhysicalHook.h"' + [Environment]::NewLine + $mftContent
[IO.File]::WriteAllText($mftImpl,$mftContent,[Text.UTF8Encoding]::new($false))
Replace-Once $mftImpl @'
STDMETHODIMP CMultipinMft::InitializeTransform ( 
    _In_ IMFAttributes *pAttributes
    )
{
'@ @'
STDMETHODIMP CMultipinMft::InitializeTransform ( 
    _In_ IMFAttributes *pAttributes
    )
{
    // This identifies the Device MFT instance, not the consuming application.
    wchar_t avcamInitEvent[192]{};
    (void)swprintf_s(avcamInitEvent,
        L"AvCamDeviceMftInitialize instance=%p host_pid=%lu client_identity=unknown",
        static_cast<void*>(this), GetCurrentProcessId());
    AvCamTrace(avcamInitEvent);
'@


# Guard the connected predecessor's reported counts BEFORE the Microsoft
# sample allocates arrays or creates pins. Zero input pins is valid for DevProxy,
# but no output pins or more than 32 of either kind is not a supported test.
# The guard is independent of whether the predecessor is Sonix.
Replace-Once $mftImpl @'
    DMFTCHECKHR_GOTO( m_spSourceTransform->GetStreamCount( &inputStreams, &outputStreams ), done );

    spFilterUnk = nullptr;
'@ @'
    DMFTCHECKHR_GOTO( m_spSourceTransform->GetStreamCount( &inputStreams, &outputStreams ), done );

    // Fail closed on malformed/extreme topology before allocating arrays.
    if (avcam::dmft::CheckUpstreamPinCounts(inputStreams, outputStreams) !=
        avcam::dmft::UpstreamCountGate::BoundedForEnumeration) {
        AvCamTrace(L"AvCamUpstreamTopologyRejected invalid_pin_count=1 upstream_identity=unknown");
        hr = E_INVALIDARG;
        goto done;
    }

    spFilterUnk = nullptr;
'@

# Capture media-type evidence from the ACTUAL predecessor's output stream IDs.
# This is an observation only: it cannot determine whether that predecessor
# is Sonix, or certify device-wide installation compatibility.
Replace-Once $mftImpl @'
        DMFTCHECKHR_GOTO( m_spSourceTransform->GetStreamIDs( inputStreams, pcInputStreams,
            outputStreams,
            pcOutputStreams ),done );
'@ @'
        DMFTCHECKHR_GOTO( m_spSourceTransform->GetStreamIDs( inputStreams, pcInputStreams,
            outputStreams,
            pcOutputStreams ),done );

        // The predecessor is untrusted OEM code. Duplicate IDs would alias
        // different camera pins and could corrupt an active connection.
        // Count limits were already enforced above, before allocation.
        if (avcam::dmft::AuditStreamIds(pcOutputStreams, outputStreams) !=
                avcam::dmft::StreamIdAudit::Unique ||
            (inputStreams > 0 &&
             avcam::dmft::AuditStreamIds(pcInputStreams, inputStreams) !=
                avcam::dmft::StreamIdAudit::Unique)) {
            AvCamTrace(L"AvCamUpstreamTopologyRejected duplicate_stream_ids=1 chain_interop=NOT_VERIFIED");
            hr = MF_E_INVALIDSTREAMNUMBER;
            goto done;
        }

        AvCamTraceUpstreamMediaContract(m_spSourceTransform.Get(),
                                        pcOutputStreams, outputStreams);
'@

# The upstream reference MFT gets the previous transform's output stream
# count from the actual MF_DEVICEMFT_CONNECTED_FILTER_KSCONTROL object inside
# InitializeTransform. Observe that count AND our resulting pin topology.
# We cannot identify the previous transform as Sonix from this callback alone;
# never convert the event into a production installation permission.
Replace-Once $mftImpl @'
    m_InputPinCount =  ULONG ( m_InPins.size() );
    m_OutputPinCount = ULONG ( m_OutPins.size() );
'@ @'
    m_InputPinCount =  ULONG ( m_InPins.size() );
    m_OutputPinCount = ULONG ( m_OutPins.size() );

    // The Microsoft sample must build exactly one successor input pin
    // for each connected predecessor output. IDs are opaque, not indices.
    if (m_InputPinCount != outputStreams || m_OutputPinCount == 0) {
        AvCamTrace(L"AvCamConnectedPinsRejected unexpected_pin_count=1");
        hr = MF_E_INVALIDSTREAMNUMBER;
        goto done;
    }
    for (DWORD i = 0; i < outputStreams; ++i) {
        if (!m_InPins[i] || m_InPins[i]->streamId() != pcOutputStreams[i]) {
            AvCamTrace(L"AvCamConnectedPinsRejected successor_stream_id_mismatch=1");
            hr = MF_E_INVALIDSTREAMNUMBER;
            goto done;
        }
    }
    AvCamTrace(L"AvCamConnectedPinsValidated predecessor_outputs_match_successor_inputs=1");

    // Observational event only: upstream identity is unverified.
    // For a valid initialized pipeline the upstream output stream count
    // should agree with this MFT input pin count.
    wchar_t avcamTopology[320]{};
    (void)swprintf_s(
        avcamTopology,
        L"AvCamDeviceMftPinTopology host_pid=%lu upstream_input=%lu upstream_output=%lu avcam_input=%lu avcam_output=%lu identity=unknown",
        GetCurrentProcessId(),
        static_cast<unsigned long>(inputStreams),
        static_cast<unsigned long>(outputStreams),
        static_cast<unsigned long>(m_InputPinCount),
        static_cast<unsigned long>(m_OutputPinCount));
    AvCamTrace(avcamTopology);
'@


# Capture initialized transform topology without modifying stream routing.
# The inherited Microsoft sample reads its predecessor's GetStreamCount
# through the actual MF_DEVICEMFT_CONNECTED_FILTER_KSCONTROL pointer.
# This telemetry cannot prove the predecessor is Sonix or validate media types.
Replace-Once $mftImpl @'
    if ( pcInputStreams )
    {
        delete[ ] ( pcInputStreams );
    }
'@ @'
    AvCamTraceDeviceMftTopology(
        static_cast<long>(hr),
        static_cast<unsigned long>(inputStreams),
        static_cast<unsigned long>(outputStreams),
        static_cast<unsigned long>(m_InputPinCount),
        static_cast<unsigned long>(m_OutputPinCount));
    if ( pcInputStreams )
    {
        delete[ ] ( pcInputStreams );
    }
'@

# Microsoft sample's input pin enumerates GetOutputAvailableType() with
# an unbounded for-loop. A misbehaving OEM predecessor could keep returning
# S_OK indefinitely and stall the Windows Camera Frame Server. Fail closed.
$basePin=Join-Path $base 'basepin.cpp'
$basePinText=[IO.File]::ReadAllText($basePin)
$basePinText='#include "AvCamDmftMediaContract.h"' + [Environment]::NewLine +
             '#include "AvCamPhysicalHook.h"' + [Environment]::NewLine + $basePinText
[IO.File]::WriteAllText($basePin,$basePinText,[Text.UTF8Encoding]::new($false))
Replace-Once $basePin @'
    for (UINT iMediaType = 0; SUCCEEDED(hr) ; iMediaType++)
'@ @'
    for (UINT iMediaType = 0;
         iMediaType < avcam::dmft::kMaxObservedTypesPerStream && SUCCEEDED(hr);
         ++iMediaType)
'@
Replace-Once $basePin @'
    if (hr == MF_E_NO_MORE_TYPES) {
        hr = S_OK;
    }
    return hr;
'@ @'
    if (hr == MF_E_NO_MORE_TYPES) {
        hr = S_OK;
    } else if (hr == S_OK) {
        // 64 successful types without a terminal MF_E_NO_MORE_TYPES is
        // incomplete enumeration, not authorization to expose a pin.
        AvCamTrace(L"AvCamUpstreamMediaTypeEnumerationRejected cap=64");
        hr = MF_E_INVALIDMEDIATYPE;
    }
    return hr;
'@

$common=Join-Path $base 'common.h'
# Microsoft's reference relies on WPP-generated check macros. The CMake
# standalone build runs without WPP, so enable upstream's documented
# fallback semantics, preserving error propagation and goto labels.
$commonText=[IO.File]::ReadAllText($common)
$macros=@'
#ifndef WPP_LEVEL_ENABLED
#define WPP_LEVEL_ENABLED(flag) false
#endif
#ifndef DMFT_GENERAL
#define DMFT_GENERAL 0
#endif
#ifndef DMFTCHECKHR_GOTO
#define DMFTCHECKHR_GOTO(a,b,...) do { hr=(a); if(FAILED(hr)) { goto b; } } while(0)
#endif
#ifndef DMFTCHECKNULL_GOTO
#define DMFTCHECKNULL_GOTO(a,b,c,...) do { if(!(a)) { hr=(c); goto b; } } while(0)
#endif
'@
$commonText=$commonText.Replace('#pragma once', "#pragma once" + [Environment]::NewLine + $macros)
[IO.File]::WriteAllText($common,$commonText,[Text.UTF8Encoding]::new($false))

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
