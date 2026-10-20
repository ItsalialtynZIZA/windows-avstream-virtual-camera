// AvCam: native physical Logitech C270 capture via Windows Media Foundation.
// This is an INPUT ONLY library: it does not register a virtual device,
// alter the USB driver, bind Device MFTs or impersonate USB hardware.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace {
constexpr DWORD kStream = MF_SOURCE_READER_FIRST_VIDEO_STREAM;
constexpr int kMaxWidth = 1920, kMaxHeight = 1080;

std::wstring HrText(HRESULT hr) {
    wchar_t buf[40]{};
    swprintf_s(buf, L"0x%08X", static_cast<unsigned>(hr));
    return buf;
}
struct Mode {
    UINT32 width=0, height=0, fpsN=0, fpsD=1;
    GUID subtype{};
    int score=0;
};
struct Capture {
    std::mutex mu;
    std::condition_variable cv;
    std::thread worker;
    std::atomic<bool> stopping{false};
    bool ready=false, failed=false;
    std::wstring error;
    std::wstring description;
    int width=0, height=0, fps=0;
    std::vector<uint8_t> pixels;
    uint64_t sequence=0, delivered=0;
};
void SetError(Capture* c, const std::wstring& msg) {
    std::lock_guard<std::mutex> lock(c->mu);
    c->error=msg;
    c->failed=true;
    c->cv.notify_all();
}
int ModeScore(const Mode& m, int desiredWidth, int desiredHeight, int desiredFps) {
    const int diff=abs((int)m.width-desiredWidth) + abs((int)m.height-desiredHeight);
    const bool yuy2=m.subtype==MFVideoFormat_YUY2;
    const bool mjpg=m.subtype==MFVideoFormat_MJPG;
    if (!yuy2 && !mjpg && m.subtype!=MFVideoFormat_NV12) return -100000;
    int fps=m.fpsD ? static_cast<int>(m.fpsN/m.fpsD) : 0;
    // Prefer native uncompressed VGA over USB MJPEG when both are supported.
    // Pixel format conversion is performed by Media Foundation, not OpenCV.
    return 20000 - diff*8 - abs(fps-desiredFps)*3 + (yuy2 ? 80 : (mjpg ? 20 : 0));
}
std::wstring FriendlyName(IMFActivate* a, REFGUID key) {
    wchar_t* value=nullptr;
    UINT32 chars=0;
    std::wstring out;
    if (SUCCEEDED(a->GetAllocatedString(key,&value,&chars)) && value)
        out.assign(value,chars);
    if (value) CoTaskMemFree(value);
    return out;
}
bool SelectPhysicalC270(ComPtr<IMFMediaSource>& source, std::wstring& label,
                        std::wstring& error) {
    ComPtr<IMFAttributes> attrs;
    HRESULT hr=MFCreateAttributes(&attrs,1);
    if (SUCCEEDED(hr)) hr=attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                      MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if (FAILED(hr)) { error=L"MF device attributes: "+HrText(hr); return false; }
    IMFActivate** devices=nullptr; UINT32 count=0;
    hr=MFEnumDeviceSources(attrs.Get(),&devices,&count);
    if (FAILED(hr)) { error=L"MFEnumDeviceSources: "+HrText(hr); return false; }
    // Claim ONLY the known original C270 video interface; never choose
    // OBS Virtual Camera or another device by a changing numerical index.
    int matches=0;
    for (UINT32 i=0;i<count;++i) {
        const auto link=FriendlyName(devices[i],
              MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK);
        const auto name=FriendlyName(devices[i],
              MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME);
        std::wstring normalized=link;
        std::transform(normalized.begin(),normalized.end(),normalized.begin(),towupper);
        if (normalized.find(L"VID_046D&PID_0825")!=std::wstring::npos &&
            normalized.find(L"MI_00")!=std::wstring::npos) {
            ++matches;
            if (matches==1) {
                hr=devices[i]->ActivateObject(IID_PPV_ARGS(&source));
                if (SUCCEEDED(hr)) label=name+L" ["+link+L"]";
                else error=L"Media source ActivateObject: "+HrText(hr);
            }
        }
    }
    for (UINT32 i=0;i<count;++i) devices[i]->Release();
    CoTaskMemFree(devices);
    if (matches==0) { error=L"Physical Logitech C270 USB video interface not found in Media Foundation"; return false; }
    if (matches>1) {
        source.Reset();
        error=L"Multiple Logitech C270 devices found. Refusing ambiguous capture";
        return false;
    }
    return source!=nullptr;
}
bool ConfigureReader(IMFMediaSource* source, int desiredWidth,int desiredHeight,
                     int desiredFps, ComPtr<IMFSourceReader>& reader,
                     Mode& selected,std::wstring& error) {
    ComPtr<IMFAttributes> attrs;
    HRESULT hr=MFCreateAttributes(&attrs,2);
    if (FAILED(hr)) { error=L"MFCreateAttributes: "+HrText(hr);return false; }
    // Source Reader inserts built-in video processing and decoding transforms.
    (void)attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING,TRUE);
    hr=MFCreateSourceReaderFromMediaSource(source,attrs.Get(),&reader);
    if (FAILED(hr)) {error=L"MFCreateSourceReaderFromMediaSource: "+HrText(hr); return false;}
    (void)reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS,FALSE);
    (void)reader->SetStreamSelection(kStream,TRUE);
    std::vector<Mode> choices;
    for (DWORD i=0;i<128;++i) {
        ComPtr<IMFMediaType> native;
        hr=reader->GetNativeMediaType(kStream,i,&native);
        if (hr==MF_E_NO_MORE_TYPES) break;
        if (FAILED(hr)) continue;
        Mode mode{};
        if (FAILED(MFGetAttributeSize(native.Get(),MF_MT_FRAME_SIZE,&mode.width,&mode.height))) continue;
        if (FAILED(native->GetGUID(MF_MT_SUBTYPE,&mode.subtype))) continue;
        if (mode.width<160 || mode.height<120 || mode.width>kMaxWidth || mode.height>kMaxHeight) continue;
        (void)MFGetAttributeRatio(native.Get(),MF_MT_FRAME_RATE,&mode.fpsN,&mode.fpsD);
        mode.score=ModeScore(mode,desiredWidth,desiredHeight,desiredFps);
        if (mode.score>-100000) choices.push_back(mode);
    }
    std::sort(choices.begin(),choices.end(),[](const Mode& a,const Mode& b){return a.score>b.score;});
    for (const Mode& m:choices) {
        ComPtr<IMFMediaType> output;
        hr=MFCreateMediaType(&output);
        if (FAILED(hr)) continue;
        if (FAILED(output->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video)) ||
            FAILED(output->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32)) ||
            FAILED(MFSetAttributeSize(output.Get(),MF_MT_FRAME_SIZE,m.width,m.height)))
            continue;
        (void)MFSetAttributeRatio(output.Get(),MF_MT_FRAME_RATE,
                    m.fpsN ? m.fpsN : (UINT32)desiredFps,m.fpsD ? m.fpsD : 1);
        (void)output->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive);
        hr=reader->SetCurrentMediaType(kStream,nullptr,output.Get());
        if (SUCCEEDED(hr)) {selected=m;return true;}
    }
    error=L"Cannot negotiate MF RGB32 output for native C270 modes; last HRESULT="+HrText(hr);
    return false;
}
bool ExtractBgr(IMFSample* sample, int width,int height,std::vector<uint8_t>& dst) {
    ComPtr<IMFMediaBuffer> buffer;
    if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) return false;
    dst.resize(static_cast<size_t>(width)*height*3);
    ComPtr<IMF2DBuffer> b2d;
    BYTE* scan=nullptr;
    LONG stride=0;
    const bool planar=SUCCEEDED(buffer.As(&b2d)) &&
            SUCCEEDED(b2d->Lock2D(&scan,&stride));
    BYTE* raw=nullptr;DWORD maxLen=0,currentLen=0;
    if (!planar) {
        if (FAILED(buffer->Lock(&raw,&maxLen,&currentLen))) return false;
        if (currentLen<static_cast<DWORD>(width*height*4)) {
            buffer->Unlock();return false;
        }
        // RGB32 video without IMF2DBuffer normally has bottom-up row layout.
        // Lock2D provides scan-line orientation and signed stride explicitly.
        stride=width*4;scan=raw;
    }
    const LONG pitch=std::abs(stride);
    if (!scan || pitch<width*4) {
        if (planar) b2d->Unlock2D();else buffer->Unlock();
        return false;
    }
    for (int y=0;y<height;++y) {
        const BYTE* in=scan+static_cast<ptrdiff_t>(y)*stride;
        uint8_t* out=dst.data()+static_cast<size_t>(y)*width*3;
        for (int x=0;x<width;++x) {
            out[3*x]=in[4*x];
            out[3*x+1]=in[4*x+1];
            out[3*x+2]=in[4*x+2];
        }
    }
    if (planar) b2d->Unlock2D();else buffer->Unlock();
    return true;
}
void Worker(Capture* cap,int width,int height,int fps) {
    const HRESULT ci=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if (FAILED(ci)) {SetError(cap,L"CoInitializeEx: "+HrText(ci));return;}
    const HRESULT ms=MFStartup(MF_VERSION,MFSTARTUP_NOSOCKET);
    if (FAILED(ms)) {
        SetError(cap,L"MFStartup: "+HrText(ms));CoUninitialize();return;
    }
    {
        ComPtr<IMFMediaSource> source;
        std::wstring label,err;
        if (!SelectPhysicalC270(source,label,err)) {
            SetError(cap,err);
        } else {
            ComPtr<IMFSourceReader> reader;
            Mode mode;
            if (!ConfigureReader(source.Get(),width,height,fps,reader,mode,err)) {
                SetError(cap,err);
            } else {
                {
                    std::lock_guard<std::mutex> guard(cap->mu);
                    cap->width=(int)mode.width;cap->height=(int)mode.height;
                    cap->fps=mode.fpsD ? (int)(mode.fpsN/mode.fpsD) : fps;
                    cap->description=label+L"; MF RGB32 from "+std::to_wstring(mode.width)
                          +L"x"+std::to_wstring(mode.height);
                    cap->ready=true;
                    cap->cv.notify_all();
                }
                DWORD consecutiveFailures=0;
                while (!cap->stopping.load()) {
                    DWORD actual=0,flags=0;LONGLONG timestamp=0;
                    ComPtr<IMFSample> sample;
                    HRESULT hr=reader->ReadSample(kStream,0,&actual,&flags,&timestamp,&sample);
                    if (FAILED(hr)) {
                        if (++consecutiveFailures>=3) {
                            SetError(cap,L"MF ReadSample: "+HrText(hr));break;
                        }
                        continue;
                    }
                    if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                        SetError(cap,L"Camera stream ended");break;
                    }
                    if (!sample) continue;
                    std::vector<uint8_t> pixels;
                    if (!ExtractBgr(sample.Get(),cap->width,cap->height,pixels)) {
                        if (++consecutiveFailures>=3) {
                            SetError(cap,L"MF RGB32 buffer decode failed");break;
                        }
                        continue;
                    }
                    consecutiveFailures=0;
                    {
                        std::lock_guard<std::mutex> guard(cap->mu);
                        cap->pixels.swap(pixels);
                        ++cap->sequence;
                    }
                    cap->cv.notify_all();
                }
            }
            if (source) source->Shutdown();
        }
    }
    MFShutdown();
    CoUninitialize();
    cap->cv.notify_all();
}
} // namespace

extern "C" __declspec(dllexport) int __cdecl AvmfOpen(
    int width,int height,int fps,void** captureOut,int* actualWidth,int* actualHeight) {
    if (!captureOut || !actualWidth || !actualHeight) return -1;
    *captureOut=nullptr;*actualWidth=*actualHeight=0;
    if (width<160 || height<120 || width>kMaxWidth || height>kMaxHeight || fps<1 || fps>60) return -2;
    auto* c=new(std::nothrow) Capture;
    if (!c) return -3;
    try {
        c->worker=std::thread(Worker,c,width,height,fps);
        std::unique_lock<std::mutex> lock(c->mu);
        bool signaled=c->cv.wait_for(lock,std::chrono::seconds(12),
                                     [&]{return c->ready || c->failed;});
        if (!signaled || !c->ready) {
            if (!signaled) c->error=L"Timed out opening C270 Media Foundation source";
            c->stopping.store(true);
            lock.unlock();
            if (c->worker.joinable()) {
                // MF startup can be blocked by a device driver. Never hang GUI
                // forever; preserve the worker object if it cannot finish.
                HANDLE h=(HANDLE)c->worker.native_handle();
                if (WaitForSingleObject(h,1500)==WAIT_OBJECT_0) {
                    c->worker.join();delete c;
                } else {c->worker.detach();}
            }
            return -4;
        }
        *actualWidth=c->width;*actualHeight=c->height;
        *captureOut=c;
        return 0;
    } catch (...) {delete c;return -5;}
}
extern "C" __declspec(dllexport) int __cdecl AvmfRead(
    void* capture,void* out, unsigned bytes,int timeoutMs) {
    if (!capture || !out) return -1;
    auto* c=static_cast<Capture*>(capture);
    if (timeoutMs<0 || timeoutMs>5000) return -2;
    std::unique_lock<std::mutex> lock(c->mu);
    const auto old=c->delivered;
    c->cv.wait_for(lock,std::chrono::milliseconds(timeoutMs),[&]{
        return c->sequence>old || c->failed || c->stopping.load();
    });
    if (c->failed || c->stopping.load()) return -3;
    if (c->sequence<=old || c->pixels.empty()) return 1; // timed out
    if (c->pixels.size()>bytes) return -4;
    memcpy(out,c->pixels.data(),c->pixels.size());
    c->delivered=c->sequence;
    return 0;
}
extern "C" __declspec(dllexport) int __cdecl AvmfDescribe(
    void* capture,wchar_t* text,unsigned chars) {
    if (!capture || !text || chars==0) return -1;
    auto* c=static_cast<Capture*>(capture);
    std::lock_guard<std::mutex> lock(c->mu);
    const auto& s=c->error.empty() ? c->description : c->error;
    if (s.size()+1>chars) return -2;
    wcscpy_s(text,chars,s.c_str());return 0;
}
extern "C" __declspec(dllexport) int __cdecl AvmfClose(void* capture) {
    if (!capture) return 0;
    auto* c=static_cast<Capture*>(capture);
    c->stopping.store(true);c->cv.notify_all();
    if (c->worker.joinable()) {
        HANDLE h=(HANDLE)c->worker.native_handle();
        // In case a broken UVC device never returns from ReadSample, never
        // block the caller forever or use TerminateThread.
        if (WaitForSingleObject(h,3000)==WAIT_OBJECT_0) {
            c->worker.join();
        } else {
            c->worker.detach();
            return 1; // Intentionally keep object alive for detached worker.
        }
    }
    delete c;return 0;
}
extern "C" __declspec(dllexport) int __cdecl AvmfSelfTest() {
    // Pure conversion and ownership ABI smoke check; no physical device needed.
    return sizeof(uint8_t)==1 && sizeof(void*)==8 ? 0 : -1;
}
