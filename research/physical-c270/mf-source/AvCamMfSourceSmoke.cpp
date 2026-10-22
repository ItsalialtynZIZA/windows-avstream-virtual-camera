// Loads the native AvCam MF source DLL directly from the CI artifact tree.
// Tests factory, COM activation, stream descriptor and start/stop/shutdown.
// No COM registration, physical camera, driver, video source, or privileges.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <ks.h>
#include <ksmedia.h>
#include <wrl/client.h>
#include <iostream>
using Microsoft::WRL::ComPtr;

constexpr GUID kSource = {
    0x8af84a8d, 0x96ed, 0x4f61,
    {0xb1,0xc3,0x5e,0xdc,0x62,0xf2,0xd5,0x91}
};
using GetClass = HRESULT (__stdcall *)(REFCLSID, REFIID, void**);

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(init)) return 3;
    const HRESULT startup = MFStartup(MF_VERSION);
    if (FAILED(startup)) { CoUninitialize(); return 4; }
    int result = 0;
    HMODULE dll = LoadLibraryW(argv[1]);
    if (!dll) {
        result = 5;
    } else {
        const auto getClass = reinterpret_cast<GetClass>(
            GetProcAddress(dll, "DllGetClassObject"));
        if (!getClass) result = 6;
        else {
            ComPtr<IClassFactory> factory;
            HRESULT hr = getClass(kSource, IID_PPV_ARGS(factory.GetAddressOf()));
            if (FAILED(hr)) result = 7;
            else {
                ComPtr<IMFActivate> activation;
                hr = factory->CreateInstance(
                    nullptr, IID_PPV_ARGS(activation.GetAddressOf()));
                if (FAILED(hr)) result = 8;
                else {
                    ComPtr<IMFMediaSource> source;
                    hr = activation->ActivateObject(
                        IID_PPV_ARGS(source.GetAddressOf()));
                    if (FAILED(hr)) {
                        std::cerr << "AVCAM_MF_ACTIVATION_FAILED hr=0x"
                                  << std::hex << static_cast<unsigned long>(hr)
                                  << std::dec << "\n";
                        result = 9;
                    }
                    else {
                        ComPtr<IMFMediaSourceEx> extended;
                        hr = source.As(&extended);
                        if (FAILED(hr)) result = 10;
                        ComPtr<IMFGetService> service;
                        ComPtr<IKsControl> controls;
                        if (result == 0 && FAILED(source.As(&service))) result = 18;
                        if (result == 0 && FAILED(source.As(&controls))) result = 19;
                        if (result == 0) {
                            ComPtr<IUnknown> noService;
                            if (service->GetService(
                                    GUID_NULL, IID_PPV_ARGS(noService.GetAddressOf()))
                                != MF_E_UNSUPPORTED_SERVICE) result = 20;
                        }
                        ComPtr<IMFPresentationDescriptor> pd;
                        if (result == 0) hr = source->CreatePresentationDescriptor(
                            pd.GetAddressOf());
                        if (FAILED(hr)) result = 11;
                        DWORD count = 0;
                        if (result == 0) hr = pd->GetStreamDescriptorCount(&count);
                        if (FAILED(hr) || count != 1) result = 12;
                        ComPtr<IMFStreamDescriptor> desc;
                        BOOL selected = FALSE;
                        if (result == 0)
                            hr = pd->GetStreamDescriptorByIndex(
                                0, &selected, desc.GetAddressOf());
                        if (FAILED(hr) || !selected) result = 13;
                        ComPtr<IMFMediaTypeHandler> handler;
                        ComPtr<IMFMediaType> mediaType;
                        GUID subtype{};
                        if (result == 0) hr = desc->GetMediaTypeHandler(
                            handler.GetAddressOf());
                        if (result == 0 && SUCCEEDED(hr))
                            hr = handler->GetCurrentMediaType(
                                mediaType.GetAddressOf());
                        if (result == 0 && SUCCEEDED(hr))
                            hr = mediaType->GetGUID(MF_MT_SUBTYPE, &subtype);
                        if (result == 0 && (FAILED(hr) || subtype != MFVideoFormat_NV12))
                            result = 14;

                        PROPVARIANT start{};
                        start.vt = VT_EMPTY;
                        if (result == 0) hr = source->Start(pd.Get(), nullptr, &start);
                        if (result == 0 && FAILED(hr)) result = 15;
                        if (result == 0) {
                            ComPtr<IMFMediaEvent> event;
                            hr = source->GetEvent(
                                MF_EVENT_FLAG_NO_WAIT, event.GetAddressOf());
                            MediaEventType type = MEUnknown;
                            if (SUCCEEDED(hr) && event) hr = event->GetType(&type);
                            if (FAILED(hr) || type != MENewStream) {
                                result = 21;
                            } else {
                                PROPVARIANT value{};
                                PropVariantInit(&value);
                                hr = event->GetValue(&value);
                                ComPtr<IMFMediaStream2> stream2;
                                if (SUCCEEDED(hr) && value.vt == VT_UNKNOWN &&
                                    value.punkVal)
                                    hr = value.punkVal->QueryInterface(
                                        IID_PPV_ARGS(stream2.GetAddressOf()));
                                else hr = E_NOINTERFACE;
                                PropVariantClear(&value);
                                MF_STREAM_STATE streamState{};
                                if (SUCCEEDED(hr))
                                    hr = stream2->GetStreamState(&streamState);
                                if (FAILED(hr)) result = 22;
                            }
                        }
                        if (result == 0 && FAILED(source->Stop())) result = 16;
                        if (FAILED(source->Shutdown()) && result == 0)
                            result = 17;
                        source.Reset();
                    }
                    activation->ShutdownObject();
                }
            }
        }
        FreeLibrary(dll);
    }
    MFShutdown();
    CoUninitialize();
    if (!result)
        std::cout << "AVCAM_MF_COM_ACTIVATION_STREAM_LIFECYCLE_PASS\n";
    return result;
}
