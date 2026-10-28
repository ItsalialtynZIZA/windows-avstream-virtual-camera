// Offline COM contract audit for the AvCam Device MFT built from the Microsoft sample.
// Loads ONLY the explicitly supplied DLL, without COM registration or camera activation.
// Does not call IMFDeviceTransform::InitializeTransform, touch registry, or open any camera.
// Separate CI process uses a timeout because third-party DLL entry points may hang.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <unknwn.h>
#include <mfidl.h>
#include <mftransform.h>
#include <ksproxy.h>
#include <iomanip>
#include <iostream>
#include <string>

namespace {
constexpr CLSID kAvCamClsid{
    0x1de4c92f, 0xa801, 0x45bb, {0x9d,0x5a,0x74,0x72,0x3a,0x8e,0xf4,0x60}
};
using GetClassObject = HRESULT (STDAPICALLTYPE *)(REFCLSID, REFIID, LPVOID*);
void Trace(const wchar_t* label, HRESULT hr) {
    std::wcout << label << L"=0x" << std::hex
               << static_cast<unsigned long>(hr) << std::dec << L"\n";
}
bool CheckInterface(IUnknown* obj, REFIID iid, const wchar_t* name) {
    void* iface = nullptr;
    const HRESULT hr = obj->QueryInterface(iid, &iface);
    if (iface) static_cast<IUnknown*>(iface)->Release();
    std::wcout << L"AVCAM_DMFT_QI_" << name << L"="
               << (SUCCEEDED(hr) && iface ? L"PASS" : L"FAIL") << L"\n";
    if (FAILED(hr)) Trace(L"QI_HRESULT", hr);
    return SUCCEEDED(hr) && iface;
}
int Inspect(const wchar_t* dllPath) {
    const DWORD attrib = GetFileAttributesW(dllPath);
    if (attrib == INVALID_FILE_ATTRIBUTES ||
        (attrib & FILE_ATTRIBUTE_DIRECTORY)) {
        std::wcerr << L"AVCAM_DMFT_DLL_MISSING\n";
        return 2;
    }
    // LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR requires absolute path. Limit search
    // to system directories and the explicitly selected build artifact.
    const HMODULE dll = LoadLibraryExW(
        dllPath, nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!dll) {
        std::wcerr << L"AVCAM_DMFT_DLL_LOAD_FAILED=" << GetLastError() << L"\n";
        return 3;
    }
    const auto factoryProc = reinterpret_cast<GetClassObject>(
        GetProcAddress(dll, "DllGetClassObject"));
    if (!factoryProc) {
        std::wcerr << L"AVCAM_DMFT_CLASS_FACTORY_EXPORT_MISSING\n";
        FreeLibrary(dll);
        return 4;
    }
    const HRESULT initHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initHr)) {
        Trace(L"CoInitializeEx", initHr);
        FreeLibrary(dll);
        return 5;
    }
    IClassFactory* factory = nullptr;
    HRESULT hr = factoryProc(kAvCamClsid, IID_IClassFactory,
                             reinterpret_cast<void**>(&factory));
    if (FAILED(hr) || !factory) {
        Trace(L"DllGetClassObject", hr);
        CoUninitialize();
        FreeLibrary(dll);
        return 6;
    }
    IUnknown* obj = nullptr;
    hr = factory->CreateInstance(nullptr, IID_IUnknown,
                                 reinterpret_cast<void**>(&obj));
    factory->Release();
    if (FAILED(hr) || !obj) {
        Trace(L"CreateInstance", hr);
        CoUninitialize();
        FreeLibrary(dll);
        return 7;
    }
    bool all = true;
    all = CheckInterface(obj, __uuidof(IMFDeviceTransform), L"IMFDeviceTransform") && all;
    all = CheckInterface(obj, __uuidof(IMFShutdown), L"IMFShutdown") && all;
    all = CheckInterface(obj, __uuidof(IMFRealTimeClientEx), L"IMFRealTimeClientEx") && all;
    all = CheckInterface(obj, __uuidof(IKsControl), L"IKsControl") && all;
    all = CheckInterface(obj, __uuidof(IMFMediaEventGenerator), L"IMFMediaEventGenerator") && all;
    // Some chains additionally require IMFTransform, report separately.
    (void)CheckInterface(obj, __uuidof(IMFTransform), L"IMFTransform_OPTIONAL");
    obj->Release();
    CoUninitialize();
    FreeLibrary(dll);
    std::wcout << L"AVCAM_DMFT_REQUIRED_COM_INTERFACES="
               << (all ? L"PASS" : L"FAIL") << L"\n"
               << L"AVCAM_OEM_PIN_COMPATIBILITY=NOT_VERIFIED\n"
               << L"AVCAM_SONIX_CHAIN_INSTALL=BLOCKED\n";
    return all ? 0 : 8;
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && std::wstring(argv[1]) == L"--selftest") {
        if (sizeof(GUID) != 16 || kAvCamClsid.Data1 != 0x1de4c92f)
            return 10;
        std::wcout << L"AVCAM_DMFT_COM_CONTRACT_SELFTEST_PASS\n";
        return 0;
    }
    if (argc == 3 && std::wstring(argv[1]) == L"--inspect-dll") {
        return Inspect(argv[2]);
    }
    std::wcerr << L"Usage: AvCamDmftComContract.exe --selftest | --inspect-dll <absolute-dll-path>\n";
    return 2;
}
