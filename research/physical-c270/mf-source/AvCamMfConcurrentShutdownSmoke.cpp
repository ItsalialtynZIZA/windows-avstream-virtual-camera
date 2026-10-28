// Stress the in-process AvCam MF source across concurrent COM calls and
// Shutdown. No COM registration, physical camera, driver or FrameServer.
// Capture data is deliberately absent; only event/stream lifetime is tested.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <wrl/client.h>
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

using Microsoft::WRL::ComPtr;
using FactoryFn = HRESULT (__stdcall*)(REFCLSID, REFIID, void**);
constexpr GUID kSource = {
    0x8af84a8d, 0x96ed, 0x4f61,
    {0xb1,0xc3,0x5e,0xdc,0x62,0xf2,0xd5,0x91}
};

struct ThreadCom final {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~ThreadCom() { if (SUCCEEDED(hr)) CoUninitialize(); }
};

bool OkOrShutdown(HRESULT hr) {
    return hr == S_OK || hr == MF_E_SHUTDOWN;
}
bool EventResult(HRESULT hr) {
    return hr == S_OK || hr == MF_E_NO_EVENTS_AVAILABLE ||
           hr == MF_E_SHUTDOWN;
}

int Run(const wchar_t* dllPath) {
    HMODULE dll = LoadLibraryW(dllPath);
    if (!dll) return 3;
    int result = 0;
    {
        const auto getClass = reinterpret_cast<FactoryFn>(
            GetProcAddress(dll, "DllGetClassObject"));
        ComPtr<IClassFactory> factory;
        ComPtr<IMFActivate> activation;
        ComPtr<IMFMediaSource> source;
        ComPtr<IMFMediaSourceEx> sourceEx;
        ComPtr<IMFPresentationDescriptor> pd;
        ComPtr<IMFMediaStream2> stream;
        if (!getClass ||
            FAILED(getClass(kSource, IID_PPV_ARGS(factory.GetAddressOf()))))
            result = 4;
        if (!result && FAILED(factory->CreateInstance(
                nullptr, IID_PPV_ARGS(activation.GetAddressOf()))))
            result = 5;
        if (!result && FAILED(activation->ActivateObject(
                IID_PPV_ARGS(source.GetAddressOf()))))
            result = 6;
        if (!result && FAILED(source.As(&sourceEx))) result = 7;
        if (!result && FAILED(source->CreatePresentationDescriptor(
                pd.GetAddressOf()))) result = 8;

        PROPVARIANT pos{};
        pos.vt = VT_EMPTY;
        if (!result && FAILED(source->Start(pd.Get(), nullptr, &pos)))
            result = 9;
        // Read MENewStream through a bounded nonblocking poll so the test
        // cannot accidentally wait forever in an event dequeue.
        if (!result) {
            for (int attempt = 0; attempt < 200 && !stream; ++attempt) {
                ComPtr<IMFMediaEvent> evt;
                HRESULT hr = source->GetEvent(
                    MF_EVENT_FLAG_NO_WAIT, evt.GetAddressOf());
                if (SUCCEEDED(hr) && evt) {
                    MediaEventType type = MEUnknown;
                    if (FAILED(evt->GetType(&type))) { result = 10; break; }
                    if (type != MENewStream) continue;
                    PROPVARIANT value{};
                    PropVariantInit(&value);
                    hr = evt->GetValue(&value);
                    if (SUCCEEDED(hr) && value.vt == VT_UNKNOWN && value.punkVal)
                        hr = value.punkVal->QueryInterface(
                            IID_PPV_ARGS(stream.GetAddressOf()));
                    else hr = E_NOINTERFACE;
                    PropVariantClear(&value);
                    if (FAILED(hr)) result = 11;
                } else if (hr != MF_E_NO_EVENTS_AVAILABLE) {
                    result = 12;
                    break;
                }
                if (!stream)
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            if (!stream && !result) result = 13;
        }

        // Verify IMFMediaStream2 transitions separately from source events.
        if (!result && FAILED(stream->SetStreamState(MF_STREAM_STATE_STOPPED)))
            result = 14;
        if (!result && FAILED(stream->SetStreamState(MF_STREAM_STATE_RUNNING)))
            result = 15;

        std::atomic<int> failure{0};
        std::atomic<bool> finish{false};
        if (!result) {
            std::thread events([&] {
                ThreadCom com;
                if (FAILED(com.hr)) { failure = 20; return; }
                for (int i = 0; i < 250 && !finish; ++i) {
                    ComPtr<IMFMediaEvent> evt;
                    HRESULT hr = source->GetEvent(
                        MF_EVENT_FLAG_NO_WAIT, evt.GetAddressOf());
                    if (!EventResult(hr)) { failure = 21; break; }
                    evt.Reset();
                    hr = stream->GetEvent(
                        MF_EVENT_FLAG_NO_WAIT, evt.GetAddressOf());
                    if (!EventResult(hr)) { failure = 22; break; }
                    ComPtr<IMFPresentationDescriptor> candidate;
                    hr = source->CreatePresentationDescriptor(
                        candidate.GetAddressOf());
                    if (!OkOrShutdown(hr)) { failure = 23; break; }
                    ComPtr<IMFAttributes> attrs;
                    hr = sourceEx->GetSourceAttributes(attrs.GetAddressOf());
                    if (!OkOrShutdown(hr)) { failure = 24; break; }
                    attrs.Reset();
                    hr = sourceEx->GetStreamAttributes(0, attrs.GetAddressOf());
                    if (!OkOrShutdown(hr)) { failure = 25; break; }
                    ComPtr<IMFStreamDescriptor> desc;
                    hr = stream->GetStreamDescriptor(desc.GetAddressOf());
                    if (!OkOrShutdown(hr)) { failure = 26; break; }
                    ComPtr<IMFMediaSource> parent;
                    hr = stream->GetMediaSource(parent.GetAddressOf());
                    if (!OkOrShutdown(hr)) { failure = 27; break; }
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            });
            std::thread lifecycle([&] {
                ThreadCom com;
                if (FAILED(com.hr)) { failure = 28; return; }
                for (int i = 0; i < 35 && !finish; ++i) {
                    HRESULT hr = source->Stop();
                    if (hr == MF_E_SHUTDOWN) break;
                    if (FAILED(hr)) { failure = 29; break; }
                    hr = source->Start(pd.Get(), nullptr, &pos);
                    if (hr == MF_E_SHUTDOWN) break;
                    if (FAILED(hr)) { failure = 30; break; }
                }
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(35));
            // Race Shutdown against active callbacks; source and stream
            // event readers must terminate without a use-after-free.
            const HRESULT shutdown = source->Shutdown();
            if (FAILED(shutdown)) failure = 31;
            finish = true;
            events.join();
            lifecycle.join();
            if (failure) result = failure.load();
            if (!result) {
                DWORD flags = 0;
                if (source->GetCharacteristics(&flags) != MF_E_SHUTDOWN)
                    result = 32;
                ComPtr<IMFStreamDescriptor> after;
                if (stream->GetStreamDescriptor(after.GetAddressOf()) !=
                    MF_E_SHUTDOWN) result = 33;
            }
        }
        if (source) source->Shutdown();
        if (activation) activation->ShutdownObject();
        stream.Reset();
        pd.Reset();
        sourceEx.Reset();
        source.Reset();
        activation.Reset();
        factory.Reset();
    }
    FreeLibrary(dll);
    return result;
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    ThreadCom com;
    if (FAILED(com.hr)) return 40;
    const HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) return 41;
    const int result = Run(argv[1]);
    MFShutdown();
    if (!result) {
        std::cout << "AVCAM_MF_CONCURRENT_SHUTDOWN_NO_CAMERA_PASS\n"
                  << "AVCAM_MF_FRAME_SERVER_LIVE_CAPTURE_NOT_TESTED\n";
    } else {
        std::cerr << "AVCAM_MF_CONCURRENT_SHUTDOWN_FAILED_STAGE="
                  << result << "\n";
    }
    return result;
}
