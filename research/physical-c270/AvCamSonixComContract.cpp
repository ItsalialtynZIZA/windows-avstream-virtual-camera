// Isolated OEM Device MFT COM interface probe for the Lenovo 13D3:56FF lab.
// Explicit opt-in ONLY. Does not initialize the transform or attach a camera,
// register COM, edit the OEM MFT chain, request frames, or read face data.
// Loading a vendor DLL executes its DllMain. Use only the expected signed OEM
// System32 module, in a disposable child process with an external timeout.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <unknwn.h>
#include <mfidl.h>
#include <mftransform.h>
#include <ks.h>
#include <ksproxy.h>
#include <wintrust.h>
#include <softpub.h>
#include <cwchar>
#include <cstdio>
#include <string>

namespace {
constexpr CLSID kSonixClsid{
    0x5a50829a, 0x86dd, 0x4d18, {0x86, 0x85, 0x89, 0x1e, 0xee, 0x64, 0x3c, 0x24}
};
using DllGetClassObjectFn = HRESULT (STDAPICALLTYPE*)(REFCLSID, REFIID, void**);

bool IsExpectedOemModule(const wchar_t* input) {
    if (!input || !*input) return false;
    wchar_t dir[MAX_PATH]{};
    if (!GetSystemDirectoryW(dir, MAX_PATH)) return false;
    std::wstring expected(dir);
    expected += L"\\SONiX\\SonixDeviceMFT.dll";
    wchar_t resolved[MAX_PATH]{};
    const DWORD length = GetFullPathNameW(input, MAX_PATH, resolved, nullptr);
    if (!length || length >= MAX_PATH) return false;
    return _wcsicmp(resolved, expected.c_str()) == 0;
}

bool IsSignedOemModule(const wchar_t* path) {
    WINTRUST_FILE_INFO file{};
    file.cbStruct = sizeof(file);
    file.pcwszFilePath = path;
    WINTRUST_DATA data{};
    data.cbStruct = sizeof(data);
    data.dwUIChoice = WTD_UI_NONE;
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &file;
    data.dwStateAction = WTD_STATEACTION_IGNORE;
    // This is local signature validation, not an online revocation audit.
    data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    return WinVerifyTrust(nullptr, &action, &data) == ERROR_SUCCESS;
}

bool HasInterface(IUnknown* object, REFIID iid, const wchar_t* label) {
    void* ptr = nullptr;
    HRESULT hr = object->QueryInterface(iid, &ptr);
    if (ptr) reinterpret_cast<IUnknown*>(ptr)->Release();
    const bool ok = SUCCEEDED(hr) && ptr != nullptr;
    std::wprintf(L"AVCAM_SONIX_QI_%ls=%ls hr=0x%08lX\n",
                 label, ok ? L"PASS" : L"NOT_AVAILABLE",
                 static_cast<unsigned long>(hr));
    return ok;
}

int Inspect(const wchar_t* dllPath) {
    if (!IsExpectedOemModule(dllPath)) {
        std::puts("AVCAM_SONIX_PROBE_REJECTED_UNEXPECTED_DLL_PATH");
        return 2;
    }
    const DWORD attrib = GetFileAttributesW(dllPath);
    if (attrib == INVALID_FILE_ATTRIBUTES || (attrib & FILE_ATTRIBUTE_DIRECTORY)) {
        std::puts("AVCAM_SONIX_PROBE_DLL_MISSING");
        return 3;
    }
    if (!IsSignedOemModule(dllPath)) {
        std::puts("AVCAM_SONIX_PROBE_REJECTED_INVALID_SIGNATURE");
        return 4;
    }
    // DLL load invokes OEM DllMain, but does not call InitializeTransform.
    HMODULE dll = LoadLibraryExW(dllPath, nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!dll) {
        std::printf("AVCAM_SONIX_PROBE_LOAD_FAILED win32=%lu\n", GetLastError());
        return 5;
    }
    const auto getClass = reinterpret_cast<DllGetClassObjectFn>(
        GetProcAddress(dll, "DllGetClassObject"));
    if (!getClass) {
        std::puts("AVCAM_SONIX_PROBE_NO_CLASS_FACTORY_EXPORT");
        FreeLibrary(dll);
        return 6;
    }
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized)) {
        std::printf("AVCAM_SONIX_PROBE_COM_INIT_FAILED hr=0x%08lX\n",
                    static_cast<unsigned long>(initialized));
        FreeLibrary(dll);
        return 7;
    }

    IClassFactory* factory = nullptr;
    HRESULT hr = getClass(kSonixClsid, IID_IClassFactory,
                          reinterpret_cast<void**>(&factory));
    if (FAILED(hr) || !factory) {
        std::printf("AVCAM_SONIX_PROBE_CLASS_FACTORY_FAILED hr=0x%08lX\n",
                    static_cast<unsigned long>(hr));
        CoUninitialize();
        FreeLibrary(dll);
        return 8;
    }
    IUnknown* object = nullptr;
    hr = factory->CreateInstance(nullptr, IID_IUnknown,
                                  reinterpret_cast<void**>(&object));
    factory->Release();
    if (FAILED(hr) || !object) {
        std::printf("AVCAM_SONIX_PROBE_CLASS_CREATION_FAILED hr=0x%08lX\n",
                    static_cast<unsigned long>(hr));
        CoUninitialize();
        FreeLibrary(dll);
        return 9;
    }

    const bool transform = HasInterface(
        object, __uuidof(IMFDeviceTransform), L"IMFDeviceTransform");
    (void)HasInterface(object, __uuidof(IKsControl), L"IKsControl");
    (void)HasInterface(object, __uuidof(IMFRealTimeClientEx), L"IMFRealTimeClientEx");
    (void)HasInterface(object, __uuidof(IMFShutdown), L"IMFShutdown");
    object->Release();
    CoUninitialize();
    FreeLibrary(dll);
    std::puts(transform ? "AVCAM_SONIX_COM_CONTRACT_OBSERVED_PASS"
                        : "AVCAM_SONIX_COM_CONTRACT_MISSING_DEVICE_TRANSFORM");
    std::puts("AVCAM_SONIX_CONNECTED_PINS=NOT_VERIFIED");
    std::puts("AVCAM_SONIX_MEDIA_TYPES=NOT_VERIFIED");
    std::puts("AVCAM_SONIX_CHAIN_INSTALL=BLOCKED");
    return transform ? 0 : 10;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && wcscmp(argv[1], L"--selftest") == 0) {
        if (sizeof(GUID) != 16 || kSonixClsid.Data1 != 0x5a50829a)
            return 11;
        if (IsExpectedOemModule(L"C:\\random\\SonixDeviceMFT.dll"))
            return 12;
        std::puts("AVCAM_SONIX_COM_PROBE_PATH_GUARD_SELFTEST_PASS");
        std::puts("AVCAM_SONIX_CHAIN_INSTALL=BLOCKED");
        return 0;
    }
    if (argc == 4 && wcscmp(argv[1], L"--inspect-sonix") == 0 &&
        wcscmp(argv[3], L"--i-accept-oem-dll-execution") == 0)
        return Inspect(argv[2]);
    std::puts("Usage: AvCamSonixComContract.exe --selftest");
    std::puts("       AvCamSonixComContract.exe --inspect-sonix <absolute OEM DLL path> --i-accept-oem-dll-execution");
    std::puts("Optional test loads signed OEM code in this process; never initializes a camera.");
    return 1;
}
