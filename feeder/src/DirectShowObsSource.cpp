#include "DirectShowObsSource.h"
#include "VirtualCameraProtocol.h"
#include <dvdmedia.h>
#include <algorithm>
#include <cstring>
#include <thread>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <atomic>

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
struct __declspec(uuid("0579154A-2B53-4994-B0D0-E773148EFF85")) ISampleGrabberCB : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE SampleCB(double, IMediaSample*) = 0;
    virtual HRESULT STDMETHODCALLTYPE BufferCB(double, BYTE*, long) = 0;
};
class FrameCallback final : public ISampleGrabberCB {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (riid == IID_IUnknown || riid == __uuidof(ISampleGrabberCB)) {
            *out = static_cast<ISampleGrabberCB*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = --refs_;
        if (!remaining) delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE SampleCB(double, IMediaSample*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE BufferCB(double, BYTE* data, long length) override {
        ++callbackCount_;
        if (!data || length != static_cast<long>(AVCAM_FRAME_BYTES_BGRA32)) {
            rejectedLength_.store(length);
            return S_OK;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            frame_.assign(data, data + length);
            ++sequence_;
        }
        ready_.notify_one();
        return S_OK;
    }
    unsigned CallbackCount() const noexcept { return callbackCount_.load(); }
    long RejectedLength() const noexcept { return rejectedLength_.load(); }
    bool WaitForFrame(std::uint64_t& previous, unsigned char* output, bool bottomUp) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!ready_.wait_for(lock, std::chrono::milliseconds(2500),
                [&] { return sequence_ != previous; })) return false;
        previous = sequence_;
        for (unsigned y = 0; y < AVCAM_FRAME_HEIGHT; ++y) {
            const unsigned srcY = bottomUp ? AVCAM_FRAME_HEIGHT - 1u - y : y;
            std::memcpy(output + y * AVCAM_FRAME_STRIDE_BGRA32,
                        frame_.data() + srcY * AVCAM_FRAME_STRIDE_BGRA32,
                        AVCAM_FRAME_STRIDE_BGRA32);
        }
        return true;
    }
private:
    std::atomic<ULONG> refs_{1};
    std::atomic<unsigned> callbackCount_{0};
    std::atomic<long> rejectedLength_{0};
    std::mutex mutex_;
    std::condition_variable ready_;
    std::vector<unsigned char> frame_;
    std::uint64_t sequence_ = 0;
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
    auto* callback = new FrameCallback();
    callback_.Attach(static_cast<IUnknown*>(callback));
    if (FAILED(grabber->SetMediaType(&wanted)) ||
        FAILED(grabber->SetOneShot(FALSE)) ||
        FAILED(grabber->SetCallback(callback, 1)) ||
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
            bottomUp_ = info->bmiHeader.biHeight > 0;
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
    // Some capture graphs require a real synchronization clock to start source delivery.
    // Keep DirectShow clock defaults; retrieve filter events on read timeout.
    const HRESULT runResult = control_->Run();
    if (FAILED(runResult)) { failureStage_ = L"DirectShow graph Run"; errorCode_ = static_cast<DWORD>(runResult); Stop(); return false; }
    lastSequence_ = 0;
    return true;
}
void DirectShowObsSource::Stop() noexcept {
    if (control_) control_->Stop();
    if (grabberFilter_) {
        ComPtr<ISampleGrabber> grabber;
        if (SUCCEEDED(grabberFilter_.As(&grabber))) grabber->SetCallback(nullptr, 1);
    }
    control_.Reset();
    sinkFilter_.Reset();
    grabberFilter_.Reset();
    sourceFilter_.Reset();
    graph_.Reset();
    callback_.Reset();
    lastSequence_ = 0;
}
bool DirectShowObsSource::ReadFrame(unsigned char* dst, std::size_t size,
    std::uint64_t& timestamp) {
    if (!dst || size < AVCAM_FRAME_BYTES_BGRA32 || !callback_) {
        failureStage_ = L"OBS callback prerequisites";
        errorCode_ = static_cast<DWORD>(E_INVALIDARG);
        return false;
    }
    // Callback stores each video sample as it arrives on a DirectShow streaming
    // thread. The capture thread waits for a NEW sample, not the graph's
    // potentially unavailable GetCurrentBuffer polling state.
    auto* callback = static_cast<FrameCallback*>(callback_.Get());
    if (!callback->WaitForFrame(lastSequence_, dst, bottomUp_)) {
        const unsigned count = callback->CallbackCount();
        if (count > 0) {
            failureStage_ = L"OBS callback delivered wrong frame length";
            errorCode_ = static_cast<DWORD>(callback->RejectedLength());
        } else {
            OAFilterState state = State_Stopped;
            const HRESULT stateResult = control_ ? control_->GetState(1000, &state) : E_POINTER;
            if (FAILED(stateResult)) {
                failureStage_ = L"OBS graph not running/GetState failed";
                errorCode_ = static_cast<DWORD>(stateResult);
            } else if (state != State_Running) {
                failureStage_ = L"OBS graph is stopped or paused";
                errorCode_ = static_cast<DWORD>(state);
            } else {
                ComPtr<IMediaEvent> events;
                if (SUCCEEDED(graph_.As(&events))) {
                    long eventCode = 0;
                    LONG_PTR param1 = 0;
                    LONG_PTR param2 = 0;
                    if (SUCCEEDED(events->GetEvent(&eventCode, &param1, &param2, 0))) {
                        if (eventCode == EC_ERRORABORT || eventCode == EC_STREAM_ERROR_STOPPED || eventCode == EC_STREAM_ERROR_STILLPLAYING) {
                            failureStage_ = L"OBS graph streaming error event";
                            errorCode_ = static_cast<DWORD>(param1);
                        } else {
                            failureStage_ = L"OBS graph emitted event without frames";
                            errorCode_ = static_cast<DWORD>(eventCode);
                        }
                        events->FreeEventParams(eventCode, param1, param2);
                    } else {
                        failureStage_ = L"OBS graph running but no samples/events";
                        errorCode_ = static_cast<DWORD>(HRESULT_FROM_WIN32(ERROR_TIMEOUT));
                    }
                } else {
                    failureStage_ = L"OBS graph running but no samples (no IMediaEvent)";
                    errorCode_ = static_cast<DWORD>(HRESULT_FROM_WIN32(ERROR_TIMEOUT));
                }
            }
        }
        return false;
    }
    timestamp = static_cast<std::uint64_t>(GetTickCount64()) * 10000ull;
    return true;
}
