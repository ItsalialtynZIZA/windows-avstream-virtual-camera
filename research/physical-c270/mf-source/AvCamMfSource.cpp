// Experimental native COM camera source for the official Windows 11
// MFCreateVirtualCamera path. Does not register itself, modify Logitech,
// load a kernel driver, or replace any physical endpoint.
// Video is *strict OBS only* from AvCam IPC V2, with no RAW fallback.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mferror.h>
#include <wrl.h>
#include <wrl/client.h>
#include <ks.h>
#include <ksmedia.h>
#include <ksproxy.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>
#include "AvCamMfPipe.h"

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Make;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;
using Microsoft::WRL::ClassicCom;

namespace {
constexpr GUID kAvCamMfSource = {
    0x8af84a8d, 0x96ed, 0x4f61,
    {0xb1, 0xc3, 0x5e, 0xdc, 0x62, 0xf2, 0xd5, 0x91}
};
constexpr LONGLONG kDuration = 10000000LL / 30LL;
std::atomic<long> g_objects{0};

std::uint64_t NowMs() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}
bool Fresh(std::uint64_t now, std::uint64_t captured) noexcept {
    return captured && captured <= now && now - captured <= 2000;
}

class AvCamMfSource;

class AvCamMfStream final :
    public RuntimeClass<RuntimeClassFlags<ClassicCom>,
        Microsoft::WRL::ChainInterfaces<IMFMediaStream2, IMFMediaStream, IMFMediaEventGenerator>> {
public:
    AvCamMfStream() { ++g_objects; }
    ~AvCamMfStream() override { Shutdown(); --g_objects; }

    HRESULT Initialize(AvCamMfSource* owner, IMFMediaType* type) {
        owner_ = owner;
        HRESULT hr = MFCreateEventQueue(queue_.GetAddressOf());
        if (FAILED(hr)) return hr;
        IMFMediaType* types[] = {type};
        hr = MFCreateStreamDescriptor(0, 1, types, desc_.GetAddressOf());
        if (FAILED(hr)) return hr;
        ComPtr<IMFMediaTypeHandler> handler;
        hr = desc_->GetMediaTypeHandler(handler.GetAddressOf());
        if (FAILED(hr)) return hr;
        hr = handler->SetCurrentMediaType(type);
        if (FAILED(hr)) return hr;
        // Advertise capture output to Windows camera enumeration.
        hr = desc_->SetGUID(MF_DEVICESTREAM_STREAM_CATEGORY, PINNAME_VIDEO_CAPTURE);
        if (FAILED(hr)) return hr;
        return desc_->SetUINT32(MF_DEVICESTREAM_STREAM_ID, 0);
    }
    IMFStreamDescriptor* Descriptor() const { return desc_.Get(); }

    // IMFMediaEventGenerator
    STDMETHOD(GetEvent)(DWORD flags, IMFMediaEvent** event) override {
        if (!event) return E_POINTER;
        if (shutdown_) return MF_E_SHUTDOWN;
        ComPtr<IMFMediaEventQueue> queue = queue_;
        return queue ? queue->GetEvent(flags, event) : MF_E_SHUTDOWN;
    }
    STDMETHOD(BeginGetEvent)(IMFAsyncCallback* callback, IUnknown* state) override {
        if (shutdown_) return MF_E_SHUTDOWN;
        return queue_ ? queue_->BeginGetEvent(callback, state) : MF_E_SHUTDOWN;
    }
    STDMETHOD(EndGetEvent)(IMFAsyncResult* result, IMFMediaEvent** event) override {
        if (shutdown_) return MF_E_SHUTDOWN;
        return queue_ ? queue_->EndGetEvent(result, event) : MF_E_SHUTDOWN;
    }
    STDMETHOD(QueueEvent)(MediaEventType type, REFGUID guid,
                          HRESULT status, const PROPVARIANT* value) override {
        if (shutdown_) return MF_E_SHUTDOWN;
        return queue_ ? queue_->QueueEventParamVar(type, guid, status, value)
                      : MF_E_SHUTDOWN;
    }

    // IMFMediaStream2 allows Frame Server to control stream state.
    STDMETHOD(SetStreamState)(MF_STREAM_STATE state) override {
        if (shutdown_) return MF_E_SHUTDOWN;
        // Frame Server can drive the stream through IMFMediaStream2, without
        // an explicit separate IMFMediaSource::Start call. Apply changes to
        // the actual sample worker, not just an informational state field.
        if (state == MF_STREAM_STATE_RUNNING) return Start();
        if (state == MF_STREAM_STATE_STOPPED) return Stop();
        return MF_E_INVALID_STATE_TRANSITION; // Camera stream has no pause.
    }
    STDMETHOD(GetStreamState)(MF_STREAM_STATE* state) override {
        if (!state) return E_POINTER;
        if (shutdown_) return MF_E_SHUTDOWN;
        *state = streamState_.load();
        return S_OK;
    }

    // IMFMediaStream
    STDMETHOD(GetMediaSource)(IMFMediaSource** source) override;
    STDMETHOD(GetStreamDescriptor)(IMFStreamDescriptor** desc) override {
        if (!desc) return E_POINTER;
        *desc = nullptr;
        if (shutdown_) return MF_E_SHUTDOWN;
        return desc_.CopyTo(desc);
    }
    STDMETHOD(RequestSample)(IUnknown* token) override {
        if (shutdown_) return MF_E_SHUTDOWN;
        if (!running_) return MF_E_INVALIDREQUEST;
        std::lock_guard<std::mutex> guard(tokensMutex_);
        if (pending_.size() >= 8) return MF_E_NOTACCEPTING;
        pending_.emplace_back(token);  // Hold caller token through asynchronous work.
        return S_OK;
    }

    HRESULT Start() {
        if (shutdown_) return MF_E_SHUTDOWN;
        if (running_.exchange(true)) return S_OK;
        try {
            worker_ = std::thread([this] { PumpObs(); });
        } catch (...) {
            running_ = false;
            return E_OUTOFMEMORY;
        }
        streamState_ = MF_STREAM_STATE_RUNNING;
        return queue_->QueueEventParamVar(MEStreamStarted, GUID_NULL, S_OK, nullptr);
    }
    HRESULT Stop() {
        const bool wasRunning = running_.exchange(false);
        streamState_ = MF_STREAM_STATE_STOPPED;
        if (worker_.joinable()) worker_.join();
        {
            std::lock_guard<std::mutex> guard(tokensMutex_);
            pending_.clear();
        }
        return wasRunning && queue_ && !shutdown_
            ? queue_->QueueEventParamVar(MEStreamStopped, GUID_NULL, S_OK, nullptr)
            : S_OK;
    }
    void Shutdown() noexcept {
        running_ = false;
        if (worker_.joinable()) worker_.join();
        {
            std::lock_guard<std::mutex> guard(tokensMutex_);
            pending_.clear();
        }
        if (shutdown_.exchange(true)) return;
        owner_ = nullptr;
        if (queue_) queue_->Shutdown();
        queue_.Reset();
        desc_.Reset();
    }
private:
    void PumpObs() noexcept {
        // Camera Frame Server runs as LocalService in session 0. The
        // interactive user's Local\ IPC mapping cannot be reached there.
        // ONLY read NV12 from the ACL-protected cross-session OBS pipe.
        // No Logitech device or RAW mapping is opened in this service.
        avcam::mfpipe::Reader reader;
        while (running_) {
            avcam::mfpipe::Frame frame;
            if (!reader.Next(frame)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(35));
                continue;
            }
            if (!Fresh(NowMs(), frame.header.capturedMs) ||
                frame.nv12.size() != avcam::mfpipe::PayloadBytes)
                continue;

            ComPtr<IUnknown> token;
            {
                std::lock_guard<std::mutex> guard(tokensMutex_);
                if (pending_.empty()) continue;
                token = pending_.front();
                pending_.pop_front();
            }
            ComPtr<IMFMediaBuffer> buffer;
            HRESULT hr = MFCreateMemoryBuffer(
                static_cast<DWORD>(frame.nv12.size()), buffer.GetAddressOf());
            if (FAILED(hr)) continue;
            BYTE* target = nullptr;
            DWORD capacity = 0;
            hr = buffer->Lock(&target, &capacity, nullptr);
            if (FAILED(hr)) continue;
            if (capacity >= frame.nv12.size())
                std::memcpy(target, frame.nv12.data(), frame.nv12.size());
            buffer->Unlock();
            if (capacity < frame.nv12.size() ||
                FAILED(buffer->SetCurrentLength(
                    static_cast<DWORD>(frame.nv12.size()))))
                continue;
            ComPtr<IMFSample> sample;
            if (FAILED(MFCreateSample(sample.GetAddressOf()))) continue;
            if (FAILED(sample->AddBuffer(buffer.Get()))) continue;
            if (token) sample->SetUnknown(MFSampleExtension_Token, token.Get());
            sample->SetSampleTime(MFGetSystemTime());
            sample->SetSampleDuration(kDuration);
            if (running_ && queue_)
                queue_->QueueEventParamUnk(
                    MEMediaSample, GUID_NULL, S_OK, sample.Get());
        }
        reader.Close();
    }
    AvCamMfSource* owner_ = nullptr; // Broken during source shutdown.
    ComPtr<IMFMediaEventQueue> queue_;
    ComPtr<IMFStreamDescriptor> desc_;
    std::atomic<bool> running_{false};
    std::atomic<MF_STREAM_STATE> streamState_{MF_STREAM_STATE_STOPPED};
    std::atomic<bool> shutdown_{false};
    std::mutex tokensMutex_;
    std::deque<ComPtr<IUnknown>> pending_;
    std::thread worker_;
};

class AvCamMfSource final :
    public RuntimeClass<RuntimeClassFlags<ClassicCom>,
        Microsoft::WRL::ChainInterfaces<
            IMFMediaSourceEx, IMFMediaSource, IMFMediaEventGenerator>,
        IMFGetService, IKsControl> {
public:
    AvCamMfSource() { ++g_objects; }
    ~AvCamMfSource() override { Shutdown(); --g_objects; }
    HRESULT Initialize() {
        std::lock_guard<std::mutex> guard(mutex_);
        if (queue_) return MF_E_ALREADY_INITIALIZED;
        HRESULT hr = MFCreateEventQueue(queue_.GetAddressOf());
        if (FAILED(hr)) return hr;
        hr = MFCreateAttributes(attributes_.GetAddressOf(), 4);
        if (FAILED(hr)) return hr;
        hr = attributes_->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                  MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
        if (FAILED(hr)) return hr;

        ComPtr<IMFMediaType> type;
        hr = MFCreateMediaType(type.GetAddressOf());
        if (FAILED(hr)) return hr;
        hr = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (FAILED(hr)) return hr;
        hr = type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
        if (FAILED(hr)) return hr;
        hr = type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        if (FAILED(hr)) return hr;
        hr = MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE,
                                static_cast<UINT32>(avcam::mfpipe::Width),
                                static_cast<UINT32>(avcam::mfpipe::Height));
        if (FAILED(hr)) return hr;
        hr = MFSetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, 30, 1);
        if (FAILED(hr)) return hr;
        hr = MFSetAttributeRatio(type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        if (FAILED(hr)) return hr;
        hr = type->SetUINT32(MF_MT_SAMPLE_SIZE,
                              static_cast<UINT32>(avcam::mfpipe::PayloadBytes));
        if (FAILED(hr)) return hr;
        hr = type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
        if (FAILED(hr)) return hr;

        stream_ = Make<AvCamMfStream>();
        if (!stream_) return E_OUTOFMEMORY;
        hr = stream_->Initialize(this, type.Get());
        if (FAILED(hr)) return hr;
        IMFStreamDescriptor* descs[] = {stream_->Descriptor()};
        hr = MFCreatePresentationDescriptor(1, descs, pd_.GetAddressOf());
        if (FAILED(hr)) return hr;
        return pd_->SelectStream(0);
    }
    // IMFMediaEventGenerator
    STDMETHOD(GetEvent)(DWORD flags, IMFMediaEvent** event) override {
        if (!event) return E_POINTER;
        if (shutdown_) return MF_E_SHUTDOWN;
        ComPtr<IMFMediaEventQueue> queue = queue_;
        return queue ? queue->GetEvent(flags, event) : MF_E_SHUTDOWN;
    }
    STDMETHOD(BeginGetEvent)(IMFAsyncCallback* cb, IUnknown* state) override {
        if (shutdown_) return MF_E_SHUTDOWN;
        return queue_ ? queue_->BeginGetEvent(cb, state) : MF_E_SHUTDOWN;
    }
    STDMETHOD(EndGetEvent)(IMFAsyncResult* res, IMFMediaEvent** event) override {
        if (shutdown_) return MF_E_SHUTDOWN;
        return queue_ ? queue_->EndGetEvent(res, event) : MF_E_SHUTDOWN;
    }
    STDMETHOD(QueueEvent)(MediaEventType type, REFGUID guid,
                          HRESULT status, const PROPVARIANT* value) override {
        if (shutdown_) return MF_E_SHUTDOWN;
        return queue_ ? queue_->QueueEventParamVar(type, guid, status, value)
                      : MF_E_SHUTDOWN;
    }
    // IMFMediaSource
    STDMETHOD(GetCharacteristics)(DWORD* flags) override {
        if (!flags) return E_POINTER;
        if (shutdown_) return MF_E_SHUTDOWN;
        *flags = MFMEDIASOURCE_IS_LIVE;
        return S_OK;
    }
    STDMETHOD(CreatePresentationDescriptor)(IMFPresentationDescriptor** pd) override {
        if (!pd) return E_POINTER;
        if (shutdown_) return MF_E_SHUTDOWN;
        return pd_ ? pd_->Clone(pd) : MF_E_NOT_INITIALIZED;
    }
    STDMETHOD(Start)(IMFPresentationDescriptor* descriptor,
                     const GUID* timeFormat, const PROPVARIANT* start) override {
        if (!descriptor || !start) return E_INVALIDARG;
        if (timeFormat && *timeFormat != GUID_NULL)
            return MF_E_UNSUPPORTED_TIME_FORMAT;
        std::lock_guard<std::mutex> guard(mutex_);
        if (shutdown_) return MF_E_SHUTDOWN;
        DWORD count = 0;
        HRESULT hr = descriptor->GetStreamDescriptorCount(&count);
        if (FAILED(hr)) return hr;
        if (count != 1) return MF_E_INVALIDSTREAMNUMBER;
        BOOL selected = FALSE;
        ComPtr<IMFStreamDescriptor> desc;
        hr = descriptor->GetStreamDescriptorByIndex(
            0, &selected, desc.GetAddressOf());
        if (FAILED(hr)) return hr;
        if (!selected) return MF_E_INVALIDREQUEST;
        DWORD id = 999;
        hr = desc->GetStreamIdentifier(&id);
        if (FAILED(hr)) return hr;
        if (id != 0) return MF_E_INVALIDSTREAMNUMBER;
        if (started_) return S_OK;
        hr = queue_->QueueEventParamUnk(MENewStream, GUID_NULL,
                                       S_OK, stream_.Get());
        if (FAILED(hr)) return hr;
        hr = stream_->Start();
        if (FAILED(hr)) return hr;
        started_ = true;
        return queue_->QueueEventParamVar(MESourceStarted, GUID_NULL, S_OK, start);
    }
    STDMETHOD(Stop)() override {
        std::lock_guard<std::mutex> guard(mutex_);
        if (shutdown_) return MF_E_SHUTDOWN;
        if (!started_) return MF_E_INVALID_STATE_TRANSITION;
        started_ = false;
        const HRESULT hr = stream_->Stop();
        if (FAILED(hr)) return hr;
        return queue_->QueueEventParamVar(MESourceStopped, GUID_NULL, S_OK, nullptr);
    }
    STDMETHOD(Pause)() override { return MF_E_INVALID_STATE_TRANSITION; }
    STDMETHOD(Shutdown)() override {
        std::lock_guard<std::mutex> guard(mutex_);
        if (shutdown_.exchange(true)) return S_OK;
        started_ = false;
        if (stream_) stream_->Shutdown();
        if (queue_) queue_->Shutdown();
        stream_.Reset();
        pd_.Reset();
        attributes_.Reset();
        queue_.Reset();
        return S_OK;
    }
    // IMFMediaSourceEx
    STDMETHOD(GetSourceAttributes)(IMFAttributes** attributes) override {
        if (!attributes) return E_POINTER;
        *attributes = nullptr;
        if (shutdown_) return MF_E_SHUTDOWN;
        return attributes_.CopyTo(attributes);
    }
    STDMETHOD(GetStreamAttributes)(DWORD streamID,
                                   IMFAttributes** attributes) override {
        if (!attributes) return E_POINTER;
        *attributes = nullptr;
        if (shutdown_) return MF_E_SHUTDOWN;
        if (streamID != 0) return MF_E_INVALIDSTREAMNUMBER;
        return stream_->Descriptor()->QueryInterface(IID_PPV_ARGS(attributes));
    }
    STDMETHOD(SetD3DManager)(IUnknown*) override { return S_OK; }

    // Mandatory Frame Server interfaces even when no camera controls or
    // auxiliary Media Foundation services are implemented.
    STDMETHOD(GetService)(REFGUID, REFIID, LPVOID* output) override {
        if (!output) return E_POINTER;
        *output = nullptr;
        return shutdown_ ? MF_E_SHUTDOWN : MF_E_UNSUPPORTED_SERVICE;
    }
    STDMETHOD(KsProperty)(PKSPROPERTY, ULONG, LPVOID, ULONG,
                          ULONG* bytesReturned) override {
        if (bytesReturned) *bytesReturned = 0;
        return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
    }
    STDMETHOD(KsMethod)(PKSMETHOD, ULONG, LPVOID, ULONG,
                        ULONG* bytesReturned) override {
        if (bytesReturned) *bytesReturned = 0;
        return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
    }
    STDMETHOD(KsEvent)(PKSEVENT, ULONG, LPVOID, ULONG,
                       ULONG* bytesReturned) override {
        if (bytesReturned) *bytesReturned = 0;
        return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
    }
private:
    std::mutex mutex_;
    ComPtr<IMFMediaEventQueue> queue_;
    ComPtr<IMFPresentationDescriptor> pd_;
    ComPtr<IMFAttributes> attributes_;
    ComPtr<AvCamMfStream> stream_;
    std::atomic<bool> shutdown_{false};
    bool started_ = false;
};

HRESULT AvCamMfStream::GetMediaSource(IMFMediaSource** source) {
    if (!source) return E_POINTER;
    *source = nullptr;
    if (shutdown_) return MF_E_SHUTDOWN;
    return owner_ ? owner_->QueryInterface(IID_PPV_ARGS(source))
                  : MF_E_SHUTDOWN;
}

// MFCreateVirtualCamera activates the registered CLSID as IMFActivate,
// which must in turn instantiate IMFMediaSource.
class AvCamMfActivation final :
    public RuntimeClass<RuntimeClassFlags<ClassicCom>,
        Microsoft::WRL::ChainInterfaces<IMFActivate, IMFAttributes>> {
public:
    AvCamMfActivation() { ++g_objects; }
    ~AvCamMfActivation() override { ShutdownObject(); --g_objects; }
    HRESULT Initialize() { return MFCreateAttributes(attributes_.GetAddressOf(), 8); }

    STDMETHOD(ActivateObject)(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        std::lock_guard<std::mutex> guard(mutex_);
        if (!source_) {
            source_ = Make<AvCamMfSource>();
            if (!source_) return E_OUTOFMEMORY;
            HRESULT hr = source_->Initialize();
            if (FAILED(hr)) { source_.Reset(); return hr; }
        }
        return source_->QueryInterface(iid, result);
    }
    STDMETHOD(ShutdownObject)() override {
        std::lock_guard<std::mutex> guard(mutex_);
        if (source_) source_->Shutdown();
        source_.Reset();
        return S_OK;
    }
    STDMETHOD(DetachObject)() override {
        std::lock_guard<std::mutex> guard(mutex_);
        source_.Reset();
        return S_OK;
    }

    // IMFActivate inherits the IMFAttributes contract. Delegate every
    // attribute operation to an MF-created in-memory store.
    STDMETHOD(GetItem)(REFGUID k, PROPVARIANT* v) override {
        return attributes_->GetItem(k, v);
    }
    STDMETHOD(GetItemType)(REFGUID k, MF_ATTRIBUTE_TYPE* v) override {
        return attributes_->GetItemType(k, v);
    }
    STDMETHOD(CompareItem)(REFGUID k, REFPROPVARIANT v, BOOL* out) override {
        return attributes_->CompareItem(k, v, out);
    }
    STDMETHOD(Compare)(IMFAttributes* other, MF_ATTRIBUTES_MATCH_TYPE type,
                       BOOL* out) override {
        return attributes_->Compare(other, type, out);
    }
    STDMETHOD(GetUINT32)(REFGUID k, UINT32* v) override {
        return attributes_->GetUINT32(k, v);
    }
    STDMETHOD(GetUINT64)(REFGUID k, UINT64* v) override {
        return attributes_->GetUINT64(k, v);
    }
    STDMETHOD(GetDouble)(REFGUID k, double* v) override {
        return attributes_->GetDouble(k, v);
    }
    STDMETHOD(GetGUID)(REFGUID k, GUID* v) override {
        return attributes_->GetGUID(k, v);
    }
    STDMETHOD(GetStringLength)(REFGUID k, UINT32* v) override {
        return attributes_->GetStringLength(k, v);
    }
    STDMETHOD(GetString)(REFGUID k, LPWSTR v, UINT32 n, UINT32* len) override {
        return attributes_->GetString(k, v, n, len);
    }
    STDMETHOD(GetAllocatedString)(REFGUID k, LPWSTR* v, UINT32* len) override {
        return attributes_->GetAllocatedString(k, v, len);
    }
    STDMETHOD(GetBlobSize)(REFGUID k, UINT32* v) override {
        return attributes_->GetBlobSize(k, v);
    }
    STDMETHOD(GetBlob)(REFGUID k, UINT8* v, UINT32 n, UINT32* len) override {
        return attributes_->GetBlob(k, v, n, len);
    }
    STDMETHOD(GetAllocatedBlob)(REFGUID k, UINT8** v, UINT32* len) override {
        return attributes_->GetAllocatedBlob(k, v, len);
    }
    STDMETHOD(GetUnknown)(REFGUID k, REFIID iid, LPVOID* v) override {
        return attributes_->GetUnknown(k, iid, v);
    }
    STDMETHOD(SetItem)(REFGUID k, REFPROPVARIANT v) override {
        return attributes_->SetItem(k, v);
    }
    STDMETHOD(DeleteItem)(REFGUID k) override {
        return attributes_->DeleteItem(k);
    }
    STDMETHOD(DeleteAllItems)() override { return attributes_->DeleteAllItems(); }
    STDMETHOD(SetUINT32)(REFGUID k, UINT32 v) override {
        return attributes_->SetUINT32(k, v);
    }
    STDMETHOD(SetUINT64)(REFGUID k, UINT64 v) override {
        return attributes_->SetUINT64(k, v);
    }
    STDMETHOD(SetDouble)(REFGUID k, double v) override {
        return attributes_->SetDouble(k, v);
    }
    STDMETHOD(SetGUID)(REFGUID k, REFGUID v) override {
        return attributes_->SetGUID(k, v);
    }
    STDMETHOD(SetString)(REFGUID k, LPCWSTR v) override {
        return attributes_->SetString(k, v);
    }
    STDMETHOD(SetBlob)(REFGUID k, const UINT8* v, UINT32 n) override {
        return attributes_->SetBlob(k, v, n);
    }
    STDMETHOD(SetUnknown)(REFGUID k, IUnknown* v) override {
        return attributes_->SetUnknown(k, v);
    }
    STDMETHOD(LockStore)() override { return attributes_->LockStore(); }
    STDMETHOD(UnlockStore)() override { return attributes_->UnlockStore(); }
    STDMETHOD(GetCount)(UINT32* n) override { return attributes_->GetCount(n); }
    STDMETHOD(GetItemByIndex)(UINT32 i, GUID* k, PROPVARIANT* v) override {
        return attributes_->GetItemByIndex(i, k, v);
    }
    STDMETHOD(CopyAllItems)(IMFAttributes* other) override {
        return attributes_->CopyAllItems(other);
    }
private:
    std::mutex mutex_;
    ComPtr<IMFAttributes> attributes_;
    ComPtr<AvCamMfSource> source_;
};

class AvCamClassFactory final :
    public RuntimeClass<RuntimeClassFlags<ClassicCom>, IClassFactory> {
public:
    AvCamClassFactory() { ++g_objects; }
    ~AvCamClassFactory() override { --g_objects; }
    STDMETHOD(CreateInstance)(IUnknown* outer, REFIID iid,
                              void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        auto obj = Make<AvCamMfActivation>();
        if (!obj) return E_OUTOFMEMORY;
        HRESULT hr = obj->Initialize();
        return SUCCEEDED(hr) ? obj->QueryInterface(iid, result) : hr;
    }
    STDMETHOD(LockServer)(BOOL lock) override {
        if (lock) ++g_objects;
        else --g_objects;
        return S_OK;
    }
};
} // namespace

HRESULT __stdcall DllGetClassObject(
    REFCLSID clsid, REFIID iid, void** result) {
    if (!result) return E_POINTER;
    *result = nullptr;
    if (clsid != kAvCamMfSource) return CLASS_E_CLASSNOTAVAILABLE;
    auto factory = Make<AvCamClassFactory>();
    if (!factory) return E_OUTOFMEMORY;
    return factory->QueryInterface(iid, result);
}
HRESULT __stdcall DllCanUnloadNow() {
    return g_objects.load() == 0 ? S_OK : S_FALSE;
}
BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }
