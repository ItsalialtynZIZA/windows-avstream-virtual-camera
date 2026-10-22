// Experimental Windows 11 software-camera controller.
// Uses the documented MFCreateVirtualCamera user-mode API.
// No kernel driver, Device MFT, physical-device PnP or Chrome changes.
// IMPORTANT: The experimental AvCam COM media source must be installed
// separately via an explicit user-approved script; a registered CLSID alone
// does not establish that the Camera Frame Server can activate it.
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
    // After a real Windows 11 Frame Server hang, never run this path by
    // default. An explicit risk flag is required for further debugging.
    if (argc == 3 && std::wstring(argv[1]) == L"--session" &&
        std::wstring(argv[2]) == L"--accept-unstable-frame-server")
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

// Use explicit late binding: GitHub Windows Server images need not contain
// mfsensorgroup.dll, but the diagnostic EXE must still run and fail closed.
struct VirtualCameraApi {
    HMODULE module = nullptr;
    decltype(&MFCreateVirtualCamera) create = nullptr;
    VirtualCameraApi() noexcept {
        module = LoadLibraryExW(L"mfsensorgroup.dll", nullptr,
                                LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (module)
            create = reinterpret_cast<decltype(&MFCreateVirtualCamera)>(
                GetProcAddress(module, "MFCreateVirtualCamera"));
    }
    VirtualCameraApi(const VirtualCameraApi&) = delete;
    VirtualCameraApi& operator=(const VirtualCameraApi&) = delete;
    ~VirtualCameraApi() { if (module) FreeLibrary(module); }
    bool Ready() const noexcept { return create != nullptr; }
};

bool ApiExists() noexcept {
    VirtualCameraApi api;
    return api.Ready();
}

void PrintUsage() {
    std::wcout << L"AvCam Windows 11 user-mode software camera controller\n"
                  L"  --preflight   Read-only check for Win11 API and source registration\n"
                  L"  --session --accept-unstable-frame-server\n"
                  L"                UNSAFE live test after observed Windows Frame Server hang.\n"
                  L"  --help        This help\n"
                  L"The experimental AvCam MF COM DLL is not installed or validated in Camera.\n"
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
    VirtualCameraApi api;
    if (!api.Ready() || !IsRegistered()) {
        std::wcerr << L"AVCAM_SOFTWARE_CAMERA_NOT_READY "
                      L"MFCreateVirtualCamera API or AvCam COM source is missing\n";
        return 6;
    }
    Apartment apartment;
    if (!apartment.Ready()) return 8;
    HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) return 8;

    Microsoft::WRL::ComPtr<IMFVirtualCamera> camera;
    hr = api.create(
        MFVirtualCameraType_SoftwareCameraSource,
        MFVirtualCameraLifetime_Session,
        MFVirtualCameraAccess_CurrentUser,
        CameraName,
        SourceClsid,
        nullptr, 0,
        camera.GetAddressOf());
    if (FAILED(hr)) {
        std::wcerr << L"AVCAM_SOFTWARE_CAMERA_CREATE_FAILED hr=0x"
                   << std::hex << static_cast<unsigned long>(hr) << L"\n";
        MFShutdown();
        return 9;
    }
    std::wcout << L"AVCAM_SOFTWARE_CAMERA_CREATE_PASS" << std::endl;
    hr = camera->Start(nullptr);
    if (FAILED(hr)) {
        std::wcerr << L"AVCAM_SOFTWARE_CAMERA_START_FAILED hr=0x"
                   << std::hex << static_cast<unsigned long>(hr) << L"\n";
        const HRESULT shutdown = camera->Shutdown();
        std::wcerr << L"AVCAM_SOFTWARE_CAMERA_FAILED_SESSION_CLEANUP hr=0x"
                   << std::hex << static_cast<unsigned long>(shutdown) << L"\n";
        camera.Reset();
        MFShutdown();
        return 9;
    }

    std::wcout << L"AVCAM_SOFTWARE_CAMERA_SESSION_CREATED "
                  L"experimental_source_requires_end_to_end_camera_validation\n"
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
        if (argc == 2 && std::wstring(argv[1]) == L"--session") {
            std::wcout << L"AVCAM_SOFTWARE_CAMERA_FRAMESERVER_HANG_QUARANTINED\n"
                          L"Real Windows 11 Frame Server hang observed on 2026-10-22.\n";
        }
        PrintUsage();
        return 2;
    }
    return 2;
}
