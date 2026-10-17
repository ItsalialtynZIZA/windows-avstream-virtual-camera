// Media Foundation camera probe: no kernel driver changes and no OBS needed.
// Enumerates actual native formats and asynchronously requests one video sample.
// Build: CMake target AvCamMfProbe (Windows SDK).
#include <Windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wrl/client.h>

#include <atomic>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

using Microsoft::WRL::ComPtr;

static void PrintResult(const wchar_t* stage, HRESULT hr)
{
    std::wcout << stage << L": HRESULT=0x"
               << std::hex << std::setw(8) << std::setfill(L'0')
               << static_cast<unsigned long>(hr)
               << std::dec << std::setfill(L' ') << L"\n";
}

class SampleCallback final : public IMFSourceReaderCallback
{
public:
    SampleCallback() : event_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override
    {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (iid == IID_IUnknown || iid == __uuidof(IMFSourceReaderCallback)) {
            *object = static_cast<IMFSourceReaderCallback*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return refs_.fetch_add(1) + 1;
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG left = refs_.fetch_sub(1) - 1;
        if (!left) delete this;
        return left;
    }

    HRESULT STDMETHODCALLTYPE OnReadSample(
        HRESULT status, DWORD /* stream */, DWORD flags,
        LONGLONG timestamp, IMFSample* sample) override
    {
        result_ = status;
        flags_ = flags;
        timestamp_ = timestamp;
        sampleBytes_ = 0;
        if (sample) {
            DWORD bytes = 0;
            HRESULT countHr = sample->GetTotalLength(&bytes);
            if (SUCCEEDED(countHr)) sampleBytes_ = bytes;
            else result_ = countHr;
        }
        if (event_) SetEvent(event_);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnEvent(DWORD /* stream */, IMFMediaEvent* /* event */) override
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnFlush(DWORD /* stream */) override
    {
        return S_OK;
    }

    HANDLE Event() const { return event_; }
    HRESULT Result() const { return result_; }
    DWORD Flags() const { return flags_; }
    DWORD SampleBytes() const { return sampleBytes_; }
    LONGLONG Timestamp() const { return timestamp_; }

private:
    ~SampleCallback()
    {
        if (event_) CloseHandle(event_);
    }

    std::atomic<ULONG> refs_{1};
    HANDLE event_{nullptr};
    HRESULT result_{E_PENDING};
    DWORD flags_{0};
    DWORD sampleBytes_{0};
    LONGLONG timestamp_{0};
};

struct MfRuntime
{
    bool com = false;
    bool mf = false;
    ~MfRuntime()
    {
        if (mf) MFShutdown();
        if (com) CoUninitialize();
    }
};

int wmain()
{
    MfRuntime runtime;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    PrintResult(L"COM initialization", hr);
    if (FAILED(hr)) return 1;
    runtime.com = true;

    hr = MFStartup(MF_VERSION);
    PrintResult(L"Media Foundation startup", hr);
    if (FAILED(hr)) return 2;
    runtime.mf = true;

    ComPtr<IMFAttributes> query;
    hr = MFCreateAttributes(&query, 1);
    if (SUCCEEDED(hr)) {
        hr = query->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    }
    PrintResult(L"Camera enumeration setup", hr);
    if (FAILED(hr)) return 3;

    IMFActivate** raw = nullptr;
    UINT32 count = 0;
    hr = MFEnumDeviceSources(query.Get(), &raw, &count);
    PrintResult(L"Enumerate cameras", hr);
    if (FAILED(hr)) return 4;

    std::vector<ComPtr<IMFActivate>> devices;
    devices.reserve(count);
    for (UINT32 i = 0; i < count; ++i) {
        ComPtr<IMFActivate> d;
        d.Attach(raw[i]);
        devices.push_back(std::move(d));
    }
    CoTaskMemFree(raw);

    ComPtr<IMFActivate> camera;
    for (const auto& device : devices) {
        wchar_t* name = nullptr;
        UINT32 length = 0;
        hr = device->GetAllocatedString(
            MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &length);
        if (SUCCEEDED(hr)) {
            std::wstring label(name, length);
            std::wcout << L"Camera: " << label << L"\n";
            if (label.find(L"AvCam Research Virtual USB Camera") != std::wstring::npos) {
                camera = device;
            }
            CoTaskMemFree(name);
        } else {
            PrintResult(L"Get friendly name", hr);
        }
    }
    if (!camera) {
        std::wcerr << L"AvCam camera not enumerated by Media Foundation.\n";
        return 5;
    }

    std::wcout << L"Opening AvCam media source..." << std::endl;
    ComPtr<IMFMediaSource> source;
    hr = camera->ActivateObject(IID_PPV_ARGS(source.GetAddressOf()));
    PrintResult(L"Activate AvCam media source", hr);
    if (FAILED(hr)) return 6;

    ComPtr<IMFAttributes> readerConfig;
    hr = MFCreateAttributes(&readerConfig, 2);
    if (FAILED(hr)) {
        PrintResult(L"Create source reader options", hr);
        return 7;
    }

    auto* rawCallback = new SampleCallback();
    ComPtr<IMFSourceReaderCallback> callback;
    callback.Attach(rawCallback);
    if (!rawCallback->Event()) {
        std::wcerr << L"Could not allocate completion event.\n";
        return 8;
    }
    hr = readerConfig->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, callback.Get());
    PrintResult(L"Configure asynchronous sample callback", hr);
    if (FAILED(hr)) return 9;

    ComPtr<IMFSourceReader> reader;
    std::wcout << L"Creating Media Foundation source reader..." << std::endl;
    hr = MFCreateSourceReaderFromMediaSource(source.Get(), readerConfig.Get(), &reader);
    PrintResult(L"Create source reader", hr);
    if (FAILED(hr)) return 10;

    ComPtr<IMFMediaType> firstNative;
    unsigned int types = 0;
    for (DWORD index = 0; index < 32; ++index) {
        ComPtr<IMFMediaType> native;
        hr = reader->GetNativeMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
                                        index, &native);
        if (hr == MF_E_NO_MORE_TYPES) break;
        if (FAILED(hr)) {
            PrintResult(L"Get native video type", hr);
            break;
        }
        if (index == 0) firstNative = native;
        GUID subtype{};
        UINT32 width = 0, height = 0, rateNum = 0, rateDen = 0;
        native->GetGUID(MF_MT_SUBTYPE, &subtype);
        MFGetAttributeSize(native.Get(), MF_MT_FRAME_SIZE, &width, &height);
        MFGetAttributeRatio(native.Get(), MF_MT_FRAME_RATE, &rateNum, &rateDen);
        std::wcout << L"NativeType[" << index << L"] subtype=0x"
                   << std::hex << subtype.Data1 << std::dec
                   << L" size=" << width << L"x" << height
                   << L" fps=" << rateNum << L"/" << rateDen << L"\n";
        ++types;
    }
    if (types == 0 || !firstNative) {
        std::wcerr << L"No native video media types exposed.\n";
        return 11;
    }

    hr = reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    PrintResult(L"Deselect streams", hr);
    if (FAILED(hr)) return 12;
    hr = reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), TRUE);
    PrintResult(L"Select video stream", hr);
    if (FAILED(hr)) return 13;

    hr = reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
                                     nullptr, firstNative.Get());
    PrintResult(L"Select first native format", hr);
    if (FAILED(hr)) return 14;

    std::wcout << L"Requesting one video sample (8-second timeout)..." << std::endl;
    hr = reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0,
                             nullptr, nullptr, nullptr, nullptr);
    PrintResult(L"Queue asynchronous ReadSample", hr);
    if (FAILED(hr)) return 15;

    const DWORD waitResult = WaitForSingleObject(rawCallback->Event(), 8000);
    if (waitResult == WAIT_TIMEOUT) {
        std::wcout << L"NO_SAMPLE_WITHIN_8_SECONDS. The media source opened,"
                      L" but did not deliver a frame.\n";
        return 16;
    }
    if (waitResult != WAIT_OBJECT_0) {
        std::wcerr << L"WaitForSingleObject failed, Win32 error="
                   << GetLastError() << L"\n";
        return 17;
    }
    PrintResult(L"Asynchronous sample result", rawCallback->Result());
    std::wcout << L"SampleBytes=" << rawCallback->SampleBytes()
               << L" Flags=0x" << std::hex << rawCallback->Flags() << std::dec
               << L" Timestamp100ns=" << rawCallback->Timestamp() << L"\n";
    if (FAILED(rawCallback->Result()) || rawCallback->SampleBytes() == 0) {
        return 18;
    }
    std::wcout << L"MEDIA_FOUNDATION_SAMPLE_RECEIVED\n";
    return 0;
}
