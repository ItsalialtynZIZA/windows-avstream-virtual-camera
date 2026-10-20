#include "DirectShowFrameSource.h"
#include "VirtualCameraProtocol.h"
#include <dvdmedia.h>
#include <oleauto.h>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <cwctype>
#include <utility>
#include <iostream>
#include <mutex>
#include <atomic>
#include <new>

using Microsoft::WRL::ComPtr;
namespace {
const CLSID kSampleGrabber = {0xC1F400A0,0x3F08,0x11D3,{0x9F,0x0B,0x00,0x60,0x08,0x03,0x9E,0x37}};
const CLSID kNullRenderer = {0xC1F400A4,0x3F08,0x11D3,{0x9F,0x0B,0x00,0x60,0x08,0x03,0x9E,0x37}};
struct __declspec(uuid("6B652FFF-11FE-4FCE-92AD-0266B5D7C78F")) ISampleGrabberAv : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE SetOneShot(BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetMediaType(const AM_MEDIA_TYPE*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetConnectedMediaType(AM_MEDIA_TYPE*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetBufferSamples(BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentBuffer(long*,long*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentSample(IMediaSample**) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetCallback(IUnknown*,long) = 0;
};
std::wstring Lower(std::wstring s) {
    std::transform(s.begin(),s.end(),s.begin(),[](wchar_t x){return std::towlower(x);});
    return s;
}
void FreeType(AM_MEDIA_TYPE& m) {
    if (m.cbFormat) CoTaskMemFree(m.pbFormat);
    if (m.pUnk) m.pUnk->Release();
    m.pbFormat=nullptr; m.cbFormat=0; m.pUnk=nullptr;
}
bool FindCamera(const std::wstring& wanted, IBaseFilter** result) {
    *result=nullptr;
    ComPtr<ICreateDevEnum> enumerator;
    if (FAILED(CoCreateInstance(CLSID_SystemDeviceEnum,nullptr,CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&enumerator)))) return false;
    ComPtr<IEnumMoniker> devices;
    if (enumerator->CreateClassEnumerator(CLSID_VideoInputDeviceCategory,&devices,0)!=S_OK) return false;
    ULONG count=0; ComPtr<IMoniker> moniker;
    unsigned matches=0; ComPtr<IBaseFilter> selected;
    while (devices->Next(1,moniker.ReleaseAndGetAddressOf(),&count)==S_OK) {
        ComPtr<IPropertyBag> bag;
        VARIANT v; VariantInit(&v);
        if (SUCCEEDED(moniker->BindToStorage(nullptr,nullptr,IID_PPV_ARGS(&bag))) &&
            SUCCEEDED(bag->Read(L"FriendlyName",&v,nullptr)) && v.vt==VT_BSTR) {
            std::wstring n=v.bstrVal;
            if (Lower(n).find(Lower(wanted))!=std::wstring::npos) {
                ++matches;
                if (matches==1) (void)moniker->BindToObject(nullptr,nullptr,IID_PPV_ARGS(&selected));
            }
        }
        VariantClear(&v);
    }
    if (matches!=1 || !selected) return false;
    *result=selected.Detach();
    return true;
}
}

struct __declspec(uuid("0579154A-2B53-4994-B0D0-E773148EFF85")) ISampleGrabberCBAv : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE SampleCB(double, IMediaSample*) = 0;
    virtual HRESULT STDMETHODCALLTYPE BufferCB(double, BYTE*, long) = 0;
};
struct DirectShowFrameSource::Grabber final : ISampleGrabberCBAv {
    std::atomic<ULONG> refs{1};
    std::mutex lock;
    std::vector<unsigned char> latest;
    unsigned long long serial = 0, readSerial = 0;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id == IID_IUnknown || id == __uuidof(ISampleGrabberCBAv)) {
            *out = static_cast<ISampleGrabberCBAv*>(this);
            AddRef(); return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG n = --refs; if (n == 0) delete this; return n;
    }
    HRESULT STDMETHODCALLTYPE SampleCB(double, IMediaSample*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE BufferCB(double, BYTE* buffer, long n) override {
        if (!buffer || n <= 0 || n > 4096 * 2160 * 4) return E_INVALIDARG;
        std::lock_guard<std::mutex> guard(lock);
        latest.assign(buffer, buffer + n);
        ++serial; return S_OK;
    }
    bool Pop(std::vector<unsigned char>& output) {
        std::lock_guard<std::mutex> guard(lock);
        if (latest.empty() || readSerial == serial) return false;
        output = latest; readSerial = serial; return true;
    }
};

DirectShowFrameSource::DirectShowFrameSource(std::wstring name):name_(std::move(name)) {}
DirectShowFrameSource::~DirectShowFrameSource(){ Stop(); }
bool DirectShowFrameSource::Start() {
    Stop();
    if (!FindCamera(name_,camera_.ReleaseAndGetAddressOf())) return false;
    if (FAILED(CoCreateInstance(CLSID_FilterGraph,nullptr,CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&graph_))) ||
        FAILED(CoCreateInstance(kSampleGrabber,nullptr,CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&grabberFilter_))) ||
        FAILED(CoCreateInstance(kNullRenderer,nullptr,CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&nullRenderer_)))) {Stop();return false;}
    ComPtr<ISampleGrabberAv> grabber;
    if (FAILED(grabberFilter_.As(&grabber))) {Stop();return false;}
    AM_MEDIA_TYPE requested{};
    requested.majortype=MEDIATYPE_Video;
    requested.subtype=MEDIASUBTYPE_RGB32;
    requested.formattype=FORMAT_VideoInfo;
    if (FAILED(grabber->SetMediaType(&requested)) ||
        FAILED(grabber->SetBufferSamples(FALSE)) ||
        FAILED(grabber->SetOneShot(FALSE))) {Stop();return false;}
    ComPtr<ICaptureGraphBuilder2> builder;
    if (FAILED(CoCreateInstance(CLSID_CaptureGraphBuilder2,nullptr,CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&builder))) ||
        FAILED(builder->SetFiltergraph(graph_.Get())) ||
        FAILED(graph_->AddFilter(camera_.Get(),L"OBS capture")) ||
        FAILED(graph_->AddFilter(grabberFilter_.Get(),L"RGB32 grabber")) ||
        FAILED(graph_->AddFilter(nullRenderer_.Get(),L"Null renderer"))) {Stop();return false;}
    HRESULT hr=builder->RenderStream(&PIN_CATEGORY_CAPTURE,&MEDIATYPE_Video,
        camera_.Get(),grabberFilter_.Get(),nullRenderer_.Get());
    if (FAILED(hr)) hr=builder->RenderStream(nullptr,&MEDIATYPE_Video,
        camera_.Get(),grabberFilter_.Get(),nullRenderer_.Get());
    if (FAILED(hr)) {Stop();return false;}
    AM_MEDIA_TYPE connected{};
    if (FAILED(grabber->GetConnectedMediaType(&connected))) {Stop();return false;}
    bool valid=connected.subtype==MEDIASUBTYPE_RGB32 &&
        connected.formattype==FORMAT_VideoInfo &&
        connected.pbFormat && connected.cbFormat>=sizeof(VIDEOINFOHEADER);
    if (valid) {
        auto* info=reinterpret_cast<const VIDEOINFOHEADER*>(connected.pbFormat);
        width_=info->bmiHeader.biWidth;
        height_=info->bmiHeader.biHeight;
        bottomUp_=height_>0;
        if (height_<0) height_=-height_;
        valid=width_>0 && height_>0 && width_<=4096 && height_<=2160 &&
            info->bmiHeader.biBitCount==32;
    }
    FreeType(connected);
    if (!valid) {Stop();return false;}
    grabber_ = new (std::nothrow) Grabber();
    if (!grabber_ || FAILED(grabber->SetCallback(static_cast<ISampleGrabberCBAv*>(grabber_), 1))) {
        Stop();return false;
    }
    if (FAILED(graph_.As(&control_)) || FAILED(control_->Run())) {
        Stop();return false;
    }
    running_=true;
    return true;
}
void DirectShowFrameSource::Stop() noexcept {
    if (control_) (void)control_->Stop();
    if (grabberFilter_) {
        ComPtr<ISampleGrabberAv> grabber;
        if (SUCCEEDED(grabberFilter_.As(&grabber))) (void)grabber->SetCallback(nullptr, 1);
    }
    if (grabber_) { grabber_->Release(); grabber_ = nullptr; }
    running_=false;
    control_.Reset();graph_.Reset();grabberFilter_.Reset();nullRenderer_.Reset();
    camera_.Reset();raw_.clear();width_=height_=0;
}
bool DirectShowFrameSource::ReadFrame(unsigned char* dst,std::size_t bytes,std::uint64_t& timestamp) {
    if (!running_ || !dst || bytes<AVCAM_FRAME_BYTES_BGRA32 || width_<=0 || height_<=0) return false;
    if (!grabber_ || !grabber_->Pop(raw_)) return false;
    const std::size_t pitch = static_cast<std::size_t>(width_) * 4;
    if (raw_.size() < pitch * static_cast<std::size_t>(height_)) return false;
    for (unsigned y=0;y<AVCAM_FRAME_HEIGHT;++y) {
        const unsigned sy=static_cast<unsigned>((static_cast<std::uint64_t>(y)*height_)/AVCAM_FRAME_HEIGHT);
        const unsigned row=bottomUp_ ? static_cast<unsigned>(height_-1)-sy : sy;
        const auto* src=raw_.data()+static_cast<std::size_t>(row)*pitch;
        auto* out=dst+static_cast<std::size_t>(y)*AVCAM_FRAME_STRIDE_BGRA32;
        for (unsigned x=0;x<AVCAM_FRAME_WIDTH;++x) {
            const unsigned sx=static_cast<unsigned>((static_cast<std::uint64_t>(x)*width_)/AVCAM_FRAME_WIDTH);
            std::memcpy(out+4*x,src+4*sx,3);
            out[4*x+3]=255;
        }
    }
    timestamp=static_cast<std::uint64_t>(GetTickCount64())*10000ull;
    return true;
}
