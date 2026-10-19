#include "DirectShowObsSource.h"
#include "VirtualCameraProtocol.h"
#include <dvdmedia.h>
#include <algorithm>
#include <cstring>
#include <thread>
#include <chrono>

using Microsoft::WRL::ComPtr;
namespace {
// The legacy Sample Grabber is not declared by modern Windows SDK headers.
struct __declspec(uuid("6B652FFF-11FE-4fce-92AD-0266B5D7C78F")) ISampleGrabber : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE SetOneShot(BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetMediaType(const AM_MEDIA_TYPE*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetConnectedMediaType(AM_MEDIA_TYPE*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetBufferSamples(BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentBuffer(long*, long*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentSample(IMediaSample**) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetCallback(IUnknown*, long) = 0;
};
const GUID kSampleGrabber = {0xC1F400A0,0x3F08,0x11D3,{0x9F,0x0B,0x00,0x60,0x08,0x03,0x9E,0x37}};
const GUID kNullRenderer = {0xC1F400A4,0x3F08,0x11D3,{0x9F,0x0B,0x00,0x60,0x08,0x03,0x9E,0x37}};

bool LocateObsFilter(IBaseFilter** out) {
    if (!out) return false;
    *out = nullptr;
    ComPtr<ICreateDevEnum> enumerator;
    if (FAILED(CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&enumerator)))) return false;
    ComPtr<IEnumMoniker> list;
    if (enumerator->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &list, 0) != S_OK)
        return false;
    ComPtr<IMoniker> moniker;
    ULONG fetched = 0;
    while (list->Next(1, moniker.ReleaseAndGetAddressOf(), &fetched) == S_OK) {
        ComPtr<IPropertyBag> properties;
        VARIANT name;
        VariantInit(&name);
        if (SUCCEEDED(moniker->BindToStorage(nullptr, nullptr, IID_PPV_ARGS(&properties))) &&
            SUCCEEDED(properties->Read(L"FriendlyName", &name, nullptr)) &&
            name.vt == VT_BSTR && name.bstrVal &&
            std::wstring(name.bstrVal) == L"OBS Virtual Camera") {
            const HRESULT hr = moniker->BindToObject(nullptr, nullptr, IID_IBaseFilter,
                reinterpret_cast<void**>(out));
            VariantClear(&name);
            return SUCCEEDED(hr);
        }
        VariantClear(&name);
    }
    return false;
}
}
bool DirectShowObsSource::Start() {
    Stop();
    errorCode_ = 0;
    failureStage_ = L"";
    if (!LocateObsFilter(sourceFilter_.ReleaseAndGetAddressOf())) {
        failureStage_ = L"OBS filter enumeration/binding"; errorCode_ = HRESULT_FROM_WIN32(ERROR_NOT_FOUND); return false;
    }
    ComPtr<ICaptureGraphBuilder2> builder;
    if (FAILED(CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&graph_))) ||
        FAILED(CoCreateInstance(CLSID_CaptureGraphBuilder2, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&builder))) ||
        FAILED(builder->SetFiltergraph(graph_.Get())) ||
        FAILED(graph_->AddFilter(sourceFilter_.Get(), L"OBS Virtual Camera")) ||
        FAILED(CoCreateInstance(kSampleGrabber, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&grabberFilter_))) ||
        FAILED(graph_->AddFilter(grabberFilter_.Get(), L"AvCam Sample Grabber")) ||
        FAILED(CoCreateInstance(kNullRenderer, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&sinkFilter_))) ||
        FAILED(graph_->AddFilter(sinkFilter_.Get(), L"AvCam Null Renderer"))) {
        failureStage_ = L"DirectShow graph/filter creation"; errorCode_ = HRESULT_FROM_WIN32(ERROR_INVALID_FUNCTION);
        Stop();
        return false;
    }
    ComPtr<ISampleGrabber> grabber;
    if (FAILED(grabberFilter_.As(&grabber))) { failureStage_ = L"SampleGrabber interface"; errorCode_ = static_cast<DWORD>(E_NOINTERFACE); Stop(); return false; }
    AM_MEDIA_TYPE wanted{};
    wanted.majortype = MEDIATYPE_Video;
    wanted.subtype = MEDIASUBTYPE_RGB32;
    wanted.formattype = FORMAT_VideoInfo;
    if (FAILED(grabber->SetMediaType(&wanted)) ||
        FAILED(grabber->SetOneShot(FALSE)) ||
        FAILED(grabber->SetBufferSamples(TRUE)) ||
        FAILED(builder->RenderStream(&PIN_CATEGORY_CAPTURE, &MEDIATYPE_Video,
            sourceFilter_.Get(), grabberFilter_.Get(), sinkFilter_.Get()))) {
        failureStage_ = L"OBS graph RenderStream/RGB32"; errorCode_ = static_cast<DWORD>(VFW_E_CANNOT_CONNECT);
        Stop();
        return false;
    }
    AM_MEDIA_TYPE actual{};
    bool valid = false;
    if (SUCCEEDED(grabber->GetConnectedMediaType(&actual))) {
        if (actual.subtype == MEDIASUBTYPE_RGB32 &&
            actual.formattype == FORMAT_VideoInfo &&
            actual.pbFormat && actual.cbFormat >= sizeof(VIDEOINFOHEADER)) {
            const auto* info = reinterpret_cast<const VIDEOINFOHEADER*>(actual.pbFormat);
            valid = info->bmiHeader.biWidth == static_cast<LONG>(AVCAM_FRAME_WIDTH) &&
                (info->bmiHeader.biHeight == static_cast<LONG>(AVCAM_FRAME_HEIGHT) ||
                 info->bmiHeader.biHeight == -static_cast<LONG>(AVCAM_FRAME_HEIGHT)) &&
                info->bmiHeader.biBitCount == 32;
        }
        if (actual.cbFormat && actual.pbFormat) CoTaskMemFree(actual.pbFormat);
        if (actual.pUnk) actual.pUnk->Release();
    }
    if (!valid) {
        failureStage_ = L"OBS negotiated format is not 1280x720 RGB32"; errorCode_ = static_cast<DWORD>(VFW_E_TYPE_NOT_ACCEPTED);
        Stop(); return false;
    }
    if (FAILED(graph_.As(&control_))) { failureStage_ = L"Graph IMediaControl"; errorCode_ = static_cast<DWORD>(E_NOINTERFACE); Stop(); return false; }
    const HRESULT runResult = control_->Run();
    if (FAILED(runResult)) { failureStage_ = L"DirectShow graph Run"; errorCode_ = static_cast<DWORD>(runResult); Stop(); return false; }
    buffer_.resize(AVCAM_FRAME_BYTES_BGRA32);
    lastFrameTick_ = 0;
    return true;
}
void DirectShowObsSource::Stop() noexcept {
    if (control_) control_->Stop();
    control_.Reset();
    sinkFilter_.Reset();
    grabberFilter_.Reset();
    sourceFilter_.Reset();
    graph_.Reset();
    buffer_.clear();
    lastFrameTick_ = 0;
}
bool DirectShowObsSource::ReadFrame(unsigned char* dst, std::size_t size,
    std::uint64_t& timestamp) {
    if (!dst || size < AVCAM_FRAME_BYTES_BGRA32 || !grabberFilter_) return false;
    ComPtr<ISampleGrabber> grabber;
    if (FAILED(grabberFilter_.As(&grabber))) return false;
    // Wait up to two seconds for OBS to publish its first media sample.
    for (int attempt = 0; attempt < 200; ++attempt) {
        long length = 0;
        HRESULT hr = grabber->GetCurrentBuffer(&length, nullptr);
        if (SUCCEEDED(hr) && length >= static_cast<long>(AVCAM_FRAME_BYTES_BGRA32) &&
            length <= static_cast<long>(AVCAM_FRAME_BYTES_BGRA32 * 2u)) {
            buffer_.resize(static_cast<std::size_t>(length));
            hr = grabber->GetCurrentBuffer(&length,
                reinterpret_cast<long*>(buffer_.data()));
            if (SUCCEEDED(hr) && length >= static_cast<long>(AVCAM_FRAME_BYTES_BGRA32)) {
                // DirectShow RGB32 VIDEOINFOHEADER is bottom-up by default.
                for (unsigned y = 0; y < AVCAM_FRAME_HEIGHT; ++y) {
                    std::memcpy(dst + y * AVCAM_FRAME_STRIDE_BGRA32,
                        buffer_.data() +
                            (AVCAM_FRAME_HEIGHT - 1u - y) * AVCAM_FRAME_STRIDE_BGRA32,
                        AVCAM_FRAME_STRIDE_BGRA32);
                }
                timestamp = static_cast<std::uint64_t>(GetTickCount64()) * 10000ull;
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}
