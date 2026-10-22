// Full camera-free Windows integration:
// synthetic OBS IPC V2 -> separate OBS-only pipe broker process ->
// unregistered AvCam MF COM DLL -> IMFMediaStream MEMediaSample NV12 pixels.
// Physical RAW is simultaneously published to verify stream isolation.
// No camera registration, test-signed driver, elevation or browser needed.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <wrl/client.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include "AvCamMfPipe.h"
#include "../AvCamRouterIpc.h"

using Microsoft::WRL::ComPtr;
namespace {
constexpr GUID SourceClsid = {
    0x8af84a8d, 0x96ed, 0x4f61,
    {0xb1, 0xc3, 0x5e, 0xdc, 0x62, 0xf2, 0xd5, 0x91}
};
using ClassFactoryFn = HRESULT (__stdcall*)(REFCLSID, REFIID, void**);

std::uint64_t NowMs() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

class BrokerProcess final {
public:
    bool Start(const wchar_t* exe) {
        std::wstring command = L"\"" + std::wstring(exe) + L"\" --frames 80";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION info{};
        if (!CreateProcessW(exe, command.data(), nullptr, nullptr, FALSE,
                            CREATE_NO_WINDOW, nullptr, nullptr,
                            &startup, &info)) return false;
        process_ = info.hProcess;
        CloseHandle(info.hThread);
        return true;
    }
    bool Running() const noexcept {
        return process_ && WaitForSingleObject(process_, 0) == WAIT_TIMEOUT;
    }
    ~BrokerProcess() {
        if (!process_) return;
        if (Running()) TerminateProcess(process_, 100);
        WaitForSingleObject(process_, 5000);
        CloseHandle(process_);
    }
private:
    HANDLE process_ = nullptr;
};

template<typename T>
bool AwaitEvent(T* source, MediaEventType wanted,
                ComPtr<IMFMediaEvent>& found,
                DWORD timeoutMs = 6000) {
    const std::uint64_t limit = NowMs() + timeoutMs;
    while (NowMs() < limit) {
        ComPtr<IMFMediaEvent> event;
        const HRESULT hr = source->GetEvent(
            MF_EVENT_FLAG_NO_WAIT, event.GetAddressOf());
        if (SUCCEEDED(hr) && event) {
            MediaEventType type = MEUnknown;
            if (FAILED(event->GetType(&type))) return false;
            if (type == MEError) return false;
            if (type == wanted) {
                found = event;
                return true;
            }
        } else if (hr != MF_E_NO_EVENTS_AVAILABLE) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

bool ValidateSample(IMFSample* sample) {
    if (!sample) return false;
    LONGLONG time = 0;
    LONGLONG duration = 0;
    if (FAILED(sample->GetSampleTime(&time)) || time <= 0 ||
        FAILED(sample->GetSampleDuration(&duration)) || duration <= 0)
        return false;
    ComPtr<IMFMediaBuffer> buffer;
    if (FAILED(sample->ConvertToContiguousBuffer(buffer.GetAddressOf())))
        return false;
    BYTE* pixels = nullptr;
    DWORD capacity = 0, length = 0;
    if (FAILED(buffer->Lock(&pixels, &capacity, &length))) return false;
    const auto offset = static_cast<DWORD>(
        avcam::mfpipe::Width * avcam::mfpipe::Height);
    // Synthetic OBS 0x9b gray -> BT.601 studio NV12 Y=149 UV=128.
    // A RAW 0x33 frame would instead yield Y=60; reject it explicitly.
    const bool valid = length == avcam::mfpipe::PayloadBytes &&
        pixels && pixels[0] == 149 &&
        pixels[offset - 1] == 149 &&
        pixels[offset] == 128 &&
        pixels[length - 1] == 128;
    buffer->Unlock();
    return valid;
}

int Exercise(const wchar_t* dllPath, const wchar_t* brokerPath) {
    avcam::ipc::Publisher raw, obs;
    if (!raw.Start(avcam::ipc::Channel::Raw)) return 2;
    std::vector<unsigned char> rawPixels(avcam::ipc::FrameBytes, 0x33);
    if (!raw.Publish(rawPixels.data(), rawPixels.size(), 1, NowMs()))
        return 3;
    if (!obs.Start(avcam::ipc::Channel::Obs)) return 4;

    BrokerProcess broker;
    if (!broker.Start(brokerPath)) return 5;
    HMODULE dll = LoadLibraryW(dllPath);
    if (!dll) return 6;
    int result = 0;
    const auto factoryFn = reinterpret_cast<ClassFactoryFn>(
        GetProcAddress(dll, "DllGetClassObject"));
    ComPtr<IClassFactory> factory;
    ComPtr<IMFActivate> activation;
    ComPtr<IMFMediaSource> source;
    ComPtr<IMFPresentationDescriptor> descriptor;
    ComPtr<IMFMediaStream> stream;

    if (!factoryFn ||
        FAILED(factoryFn(SourceClsid, IID_PPV_ARGS(factory.GetAddressOf()))))
        result = 7;
    if (!result && FAILED(factory->CreateInstance(
            nullptr, IID_PPV_ARGS(activation.GetAddressOf()))))
        result = 8;
    if (!result && FAILED(activation->ActivateObject(
            IID_PPV_ARGS(source.GetAddressOf()))))
        result = 9;
    if (!result && FAILED(source->CreatePresentationDescriptor(
            descriptor.GetAddressOf())))
        result = 10;

    std::atomic<bool> keepPublishing{true};
    std::thread publisher;
    if (!result) {
        // Keep the synthetic OBS source alive across MF Start() and
        // asynchronous RequestSample() calls. No real capture devices.
        publisher = std::thread([&] {
            std::vector<unsigned char> pixels(avcam::ipc::FrameBytes, 0x9b);
            for (std::uint64_t i = 1; i <= 160 && keepPublishing; ++i) {
                obs.Publish(pixels.data(), pixels.size(), i, NowMs());
                std::this_thread::sleep_for(std::chrono::milliseconds(38));
            }
        });

        PROPVARIANT start{};
        start.vt = VT_EMPTY;
        if (FAILED(source->Start(descriptor.Get(), nullptr, &start)))
            result = 11;
    }
    if (!result) {
        ComPtr<IMFMediaEvent> event;
        if (!AwaitEvent(source.Get(), MENewStream, event)) {
            result = 12;
        } else {
            PROPVARIANT value{};
            PropVariantInit(&value);
            const HRESULT hr = event->GetValue(&value);
            if (SUCCEEDED(hr) && value.vt == VT_UNKNOWN && value.punkVal)
                value.punkVal->QueryInterface(IID_PPV_ARGS(stream.GetAddressOf()));
            PropVariantClear(&value);
            if (!stream) result = 13;
        }
    }
    int frames = 0;
    if (!result) {
        // Each request must result in the MF event with genuine pixel data.
        for (int i = 0; i < 5; ++i) {
            if (FAILED(stream->RequestSample(nullptr))) {
                result = 14;
                break;
            }
            ComPtr<IMFMediaEvent> event;
            if (!AwaitEvent(stream.Get(), MEMediaSample, event)) {
                result = 15;
                break;
            }
            PROPVARIANT value{};
            PropVariantInit(&value);
            const HRESULT hr = event->GetValue(&value);
            ComPtr<IMFSample> sample;
            if (SUCCEEDED(hr) && value.vt == VT_UNKNOWN && value.punkVal)
                value.punkVal->QueryInterface(IID_PPV_ARGS(sample.GetAddressOf()));
            PropVariantClear(&value);
            if (!ValidateSample(sample.Get())) {
                result = 16;
                break;
            }
            ++frames;
        }
    }
    keepPublishing = false;
    if (publisher.joinable()) publisher.join();
    if (source) {
        source->Stop();
        source->Shutdown();
    }
    stream.Reset();
    descriptor.Reset();
    source.Reset();
    if (activation) activation->ShutdownObject();
    activation.Reset();
    factory.Reset();
    FreeLibrary(dll);
    obs.Stop();
    raw.Stop();
    if (!result && frames == 5) {
        std::cout << "AVCAM_MF_END_TO_END_OBS_ONLY_SAMPLES_PASS\n"
                  << "AVCAM_MF_END_TO_END_SAMPLE_COUNT=" << frames << "\n"
                  << "AVCAM_MF_END_TO_END_RAW_ISOLATION_PASS\n";
    } else {
        if (!result) result = 17;
        std::cerr << "AVCAM_MF_END_TO_END_FAILED stage=" << result
                  << " samples=" << frames << "\n";
    }
    return result;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 1;
    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(init)) return 18;
    const HRESULT startup = MFStartup(MF_VERSION);
    if (FAILED(startup)) { CoUninitialize(); return 19; }
    const int result = Exercise(argv[1], argv[2]);
    MFShutdown();
    CoUninitialize();
    return result;
}
