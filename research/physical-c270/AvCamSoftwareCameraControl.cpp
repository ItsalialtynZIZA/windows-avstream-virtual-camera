// Experimental Windows 11 software-camera controller.
// Uses the documented MFCreateVirtualCamera user-mode API.
// No kernel driver, Device MFT, physical-device PnP or Chrome changes.
// IMPORTANT: The AvCam media-source COM DLL is not implemented yet.
// This executable refuses session creation until the intended CLSID
// is independently installed and registered by an explicit user action.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfvirtualcamera.h>
#include <wrl/client.h>
#include <iostream>
#include <string>

namespace {
constexpr wchar_t SourceClsid[] =
    L"{8AF84A8D-96ED-4F61-B1C3-5EDC62F2D591}";
constexpr wchar_t CameraName[] = L"AvCam IPC Video";
enum class Mode { Help, Preflight, Session, Invalid };

Mode Parse(int argc, wchar_t** argv) noexcept {
    if (argc == 2 && (std::wstring(argv[1]) == L"--help" ||
                      std::wstring(argv[1]) == L"-h"))
        return Mode::Help;
    if (argc == 2 && std::wstring(argv[1]) == L"--preflight")
        return Mode::Preflight;
    if (argc == 2 && std::wstring(argv[1]) == L"--session")
        return Mode::Session;
    return Mode::Invalid;
}

bool IsRegistered() noexcept {
    // Frame Server is a separate service process: HKCU-only COM
    // registration is not sufficient for this implementation.
    std::wstring path = L"SOFTWARE\\Classes\\CLSID\\";
    path += SourceClsid;
    path += L"\\InprocServer32";
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0,
                      KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS)
        return false;
    RegCloseKey(key);
    return true;
}

bool ApiExists() noexcept {
    HMODULE module = LoadLibraryExW(L"mfsensorgroup.dll", nullptr,
                                   LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) return false;
    const auto entry = GetProcAddress(module, "MFCreateVirtualCamera");
    FreeLibrary(module);
    return entry != nullptr;
}

void PrintUsage() {
    std::wcout << L"AvCam Windows 11 user-mode software camera controller\n"
                  L"  --preflight   Read-only check for Win11 API and source registration\n"
                  L"  --session     Explicit temporary CurrentUser camera session\n"
                  L"  --help        This help\n"
                  L"The custom AvCam Media Foundation COM source is not yet provided.\n"
                  L"No driver, Logitech or browser settings are modified.\n";
}

class Apartment {
public:
    Apartment() : hr_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~Apartment() { if (SUCCEEDED(hr_)) CoUninitialize(); }
    bool Ready() const noexcept { return SUCCEEDED(hr_); }
private:
    HRESULT hr_;
};

int RunSession() {
    if (!ApiExists() || !IsRegistered()) {
        std::wcerr << L"AVCAM_SOFTWARE_CAMERA_NOT_READY "
                      L"source COM registration is missing\n";
        return 6;
    }
    Apartment apartment;
    if (!apartment.Ready()) return 8;
    HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) return 8;

    Microsoft::WRL::ComPtr<IMFVirtualCamera> camera;
    hr = MFCreateVirtualCamera(
        MFVirtualCameraType_SoftwareCameraSource,
        MFVirtualCameraLifetime_Session,
        MFVirtualCameraAccess_CurrentUser,
        CameraName,
        SourceClsid,
        nullptr, 0,
        camera.GetAddressOf());
    if (SUCCEEDED(hr))
        hr = camera->Start(nullptr);

    if (FAILED(hr)) {
        std::wcerr << L"AVCAM_SOFTWARE_CAMERA_START_FAILED hr=0x"
                   << std::hex << static_cast<unsigned long>(hr) << L"\n";
        if (camera) camera->Shutdown();
        MFShutdown();
        return 9;
    }

    std::wcout << L"AVCAM_SOFTWARE_CAMERA_SESSION_CREATED "
                  L"registered_source_is_not_yet_an_IPC_video_stream\n"
                  L"Press Enter to remove the temporary virtual camera.\n";
    std::wstring ignored;
    std::getline(std::wcin, ignored);
    const HRESULT stopped = camera->Shutdown();
    camera.Reset();
    MFShutdown();
    if (FAILED(stopped)) return 10;
    std::wcout << L"AVCAM_SOFTWARE_CAMERA_SESSION_CLOSED\n";
    return 0;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    switch (Parse(argc, argv)) {
    case Mode::Help:
        PrintUsage();
        return 0;
    case Mode::Preflight: {
        const bool available = ApiExists();
        const bool registered = IsRegistered();
        std::wcout << L"AVCAM_MF_VIRTUAL_CAMERA_API=" << (available ? 1 : 0)
                   << L" media_source_registered=" << (registered ? 1 : 0)
                   << L" current_user_only=1\n";
        // A registered CLSID alone does not prove its DLL works.
        return available && registered ? 0 : 6;
    }
    case Mode::Session:
        return RunSession();
    case Mode::Invalid:
        PrintUsage();
        return 2;
    }
    return 2;
}
