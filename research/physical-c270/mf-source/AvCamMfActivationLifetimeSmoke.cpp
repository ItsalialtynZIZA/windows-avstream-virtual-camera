// Regression for Windows Camera Frame Server's separate activation/source
// lifetimes. The activation COM wrapper may be released before the client
// stops using the AddRef'd IMFMediaSource. Releasing activation must not
// implicitly Shutdown() the live media source (MF_E_SHUTDOWN).
// No virtual camera, registry, physical device, service, or user consent.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <wrl/client.h>
#include <iostream>
using Microsoft::WRL::ComPtr;
using GetClass = HRESULT (__stdcall*)(REFCLSID, REFIID, void**);
using CanUnload = HRESULT (__stdcall*)();
constexpr GUID kAvCamSource = {
    0x8af84a8d, 0x96ed, 0x4f61,
    {0xb1,0xc3,0x5e,0xdc,0x62,0xf2,0xd5,0x91}
};

int VerifySource(IMFMediaSource* source) {
    if (!source) return 14;
    DWORD flags = 0;
    HRESULT hr = source->GetCharacteristics(&flags);
    if (hr == MF_E_SHUTDOWN) return 15;
    if (FAILED(hr) || !(flags & MFMEDIASOURCE_IS_LIVE)) return 16;

    ComPtr<IMFPresentationDescriptor> descriptor;
    hr = source->CreatePresentationDescriptor(descriptor.GetAddressOf());
    if (hr == MF_E_SHUTDOWN) return 17;
    if (FAILED(hr)) return 18;
    PROPVARIANT position{};
    position.vt = VT_EMPTY;
    hr = source->Start(descriptor.Get(), nullptr, &position);
    if (hr == MF_E_SHUTDOWN) return 19;
    if (FAILED(hr)) return 20;
    hr = source->Stop();
    if (FAILED(hr)) return 21;
    return 0;
}

int Run(const wchar_t* path) {
    HMODULE dll = LoadLibraryW(path);
    if (!dll) return 3;
    const auto getClass = reinterpret_cast<GetClass>(
        GetProcAddress(dll, "DllGetClassObject"));
    const auto canUnload = reinterpret_cast<CanUnload>(
        GetProcAddress(dll, "DllCanUnloadNow"));
    int outcome = 0;
    if (!getClass || !canUnload) outcome = 4;
    if (!outcome) {
        {
            ComPtr<IClassFactory> factory;
            HRESULT hr = getClass(
                kAvCamSource, IID_PPV_ARGS(factory.GetAddressOf()));
            if (FAILED(hr)) outcome = 5;
            if (!outcome) {
                ComPtr<IMFActivate> activation;
                hr = factory->CreateInstance(
                    nullptr, IID_PPV_ARGS(activation.GetAddressOf()));
                if (FAILED(hr)) outcome = 6;
                ComPtr<IMFMediaSource> source;
                if (!outcome) {
                    hr = activation->ActivateObject(
                        IID_PPV_ARGS(source.GetAddressOf()));
                    if (FAILED(hr)) outcome = 7;
                }
                if (!outcome) {
                    // Release activation WITHOUT explicit ShutdownObject.
                    // The independently referenced MF source MUST stay live.
                    activation.Reset();
                    outcome = VerifySource(source.Get());
                    if (!outcome)
                        std::cout << "AVCAM_MF_SOURCE_SURVIVES_ACTIVATE_RELEASE_PASS\n";
                }
                if (source) source->Shutdown();
                source.Reset();
            }
            factory.Reset();
        }

        if (!outcome) {
            ComPtr<IClassFactory> factory;
            HRESULT hr = getClass(
                kAvCamSource, IID_PPV_ARGS(factory.GetAddressOf()));
            if (FAILED(hr)) outcome = 22;
            ComPtr<IMFActivate> activation;
            ComPtr<IMFMediaSource> source;
            if (!outcome) {
                hr = factory->CreateInstance(
                    nullptr, IID_PPV_ARGS(activation.GetAddressOf()));
                if (FAILED(hr)) outcome = 23;
            }
            if (!outcome) {
                hr = activation->ActivateObject(
                    IID_PPV_ARGS(source.GetAddressOf()));
                if (FAILED(hr)) outcome = 24;
            }
            if (!outcome) {
                // Explicit DetachObject also transfers lifetime to source.
                hr = activation->DetachObject();
                if (FAILED(hr)) outcome = 25;
                activation.Reset();
                if (!outcome) outcome = VerifySource(source.Get());
                if (!outcome)
                    std::cout << "AVCAM_MF_SOURCE_SURVIVES_ACTIVATE_DETACH_PASS\n";
            }
            if (source) source->Shutdown();
            source.Reset();
            activation.Reset();
            factory.Reset();
        }
        if (!outcome && canUnload() != S_OK)
            outcome = 26;
        if (!outcome)
            std::cout << "AVCAM_MF_ACTIVATION_LIFETIME_PASS\n";
    }
    FreeLibrary(dll);
    return outcome;
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) return 27;
    hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) { CoUninitialize(); return 28; }
    const int result = Run(argv[1]);
    MFShutdown();
    CoUninitialize();
    if (result)
        std::cerr << "AVCAM_MF_ACTIVATION_LIFETIME_FAILED_STAGE=" << result << "\n";
    return result;
}
