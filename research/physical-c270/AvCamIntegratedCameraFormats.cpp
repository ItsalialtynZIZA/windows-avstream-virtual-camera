// Read-only Media Foundation native-type inventory for Lenovo Integrated Camera 13D3:56FF.
// The camera source is activated to inspect its presentation descriptor, but no
// Start(), SourceReader::ReadSample(), registry modification or COM registration occurs.
// Do not run alongside applications that currently own the integrated camera.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mferror.h>
#include <wrl/client.h>
#include <algorithm>
#include <cwctype>
#include <iomanip>
#include <iostream>
#include <string>

using Microsoft::WRL::ComPtr;

namespace {
bool IsTarget(const std::wstring& path) {
    std::wstring lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](wchar_t c) { return std::towlower(c); });
    return lower.find(L"vid_13d3&pid_56ff") != std::wstring::npos &&
           lower.find(L"mi_00") != std::wstring::npos;
}
bool IsReplacementType(const GUID& subtype, UINT32 width, UINT32 height) {
    return subtype == MFVideoFormat_MJPG && width == 1280 && height == 720;
}
const wchar_t* SubtypeName(const GUID& subtype) {
    if (subtype == MFVideoFormat_MJPG) return L"MJPG";
    if (subtype == MFVideoFormat_NV12) return L"NV12";
    if (subtype == MFVideoFormat_YUY2) return L"YUY2";
    if (subtype == MFVideoFormat_RGB32) return L"RGB32";
    if (subtype == MFVideoFormat_H264) return L"H264";
    return L"OTHER";
}
void PrintError(const wchar_t* action, HRESULT hr) {
    std::wcerr << action << L" HRESULT=0x" << std::hex
               << static_cast<unsigned long>(hr) << std::dec << L"\n";
}
struct MfSession {
    HRESULT com = E_FAIL;
    HRESULT mf = E_FAIL;
    MfSession() {
        com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (SUCCEEDED(com)) mf = MFStartup(MF_VERSION);
    }
    ~MfSession() {
        if (SUCCEEDED(mf)) (void)MFShutdown();
        if (SUCCEEDED(com)) CoUninitialize();
    }
};
struct ActivationList {
    IMFActivate** items = nullptr;
    UINT32 count = 0;
    ~ActivationList() {
        if (items) {
            for (UINT32 i = 0; i < count; ++i) {
                if (items[i]) items[i]->Release();
            }
            CoTaskMemFree(items);
        }
    }
};
std::wstring GetLink(IMFActivate* activate) {
    WCHAR* raw = nullptr;
    UINT32 size = 0;
    const HRESULT hr = activate->GetAllocatedString(
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
        &raw, &size);
    if (FAILED(hr)) {
        if (raw) CoTaskMemFree(raw);
        return L"";
    }
    const std::wstring link = raw ? std::wstring(raw) : L"";
    CoTaskMemFree(raw);
    return link;
}
int Inspect() {
    MfSession session;
    if (FAILED(session.com)) {
        PrintError(L"CoInitializeEx", session.com);
        return 3;
    }
    if (FAILED(session.mf)) {
        PrintError(L"MFStartup", session.mf);
        return 3;
    }
    ComPtr<IMFAttributes> attrs;
    HRESULT hr = MFCreateAttributes(&attrs, 1);
    if (FAILED(hr)) { PrintError(L"MFCreateAttributes", hr); return 3; }
    hr = attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if (FAILED(hr)) { PrintError(L"SetGUID", hr); return 3; }
    ActivationList devices;
    hr = MFEnumDeviceSources(attrs.Get(), &devices.items, &devices.count);
    if (FAILED(hr)) { PrintError(L"MFEnumDeviceSources", hr); return 3; }
    IMFActivate* target = nullptr;
    UINT32 matches = 0;
    for (UINT32 i = 0; i < devices.count; ++i) {
        if (!devices.items[i]) continue;
        if (IsTarget(GetLink(devices.items[i]))) {
            target = devices.items[i];
            ++matches;
        }
    }
    std::wcout << L"AVCAM_INTEGRATED_MF_MATCHES=" << matches << L"\n";
    if (matches != 1) {
        std::wcerr << L"Exactly one 13D3:56FF MI_00 integrated MF source required\n";
        return 4;
    }
    ComPtr<IMFMediaSource> source;
    hr = target->ActivateObject(IID_PPV_ARGS(source.GetAddressOf()));
    if (FAILED(hr)) {
        PrintError(L"Integrated camera MF activation (may be in use or blocked by privacy)", hr);
        return 5;
    }
    ComPtr<IMFPresentationDescriptor> presentation;
    hr = source->CreatePresentationDescriptor(&presentation);
    if (FAILED(hr)) {
        PrintError(L"CreatePresentationDescriptor", hr);
        (void)source->Shutdown();
        return 6;
    }
    DWORD count = 0;
    hr = presentation->GetStreamDescriptorCount(&count);
    if (FAILED(hr) || count > 16) {
        PrintError(L"GetStreamDescriptorCount", hr);
        (void)source->Shutdown();
        return 6;
    }
    bool mjpg1280 = false;
    unsigned total = 0;
    for (DWORD i = 0; i < count; ++i) {
        BOOL selected = FALSE;
        ComPtr<IMFStreamDescriptor> stream;
        hr = presentation->GetStreamDescriptorByIndex(
            i, &selected, stream.GetAddressOf());
        if (FAILED(hr)) { PrintError(L"GetStreamDescriptorByIndex", hr); continue; }
        ComPtr<IMFMediaTypeHandler> handler;
        hr = stream->GetMediaTypeHandler(&handler);
        if (FAILED(hr)) { PrintError(L"GetMediaTypeHandler", hr); continue; }
        DWORD types = 0;
        hr = handler->GetMediaTypeCount(&types);
        if (FAILED(hr)) { PrintError(L"GetMediaTypeCount", hr); continue; }
        if (types > 256) { std::wcerr << L"Media type count exceeds audit bound\n"; continue; }
        for (DWORD j = 0; j < types; ++j) {
            ComPtr<IMFMediaType> type;
            hr = handler->GetMediaTypeByIndex(j, type.GetAddressOf());
            if (FAILED(hr)) continue;
            GUID subtype{};
            UINT32 w = 0, h = 0, fpsN = 0, fpsD = 0;
            if (FAILED(type->GetGUID(MF_MT_SUBTYPE, &subtype)) ||
                FAILED(MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &w, &h)))
                continue;
            (void)MFGetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, &fpsN, &fpsD);
            std::wcout << L"STREAM=" << i << L" TYPE=" << j
                       << L" SUBTYPE=" << SubtypeName(subtype)
                       << L" SIZE=" << w << L"x" << h
                       << L" FPS=" << fpsN << L"/" << fpsD << L"\n";
            if (IsReplacementType(subtype, w, h)) mjpg1280 = true;
            ++total;
        }
    }
    // Release ALL activated COM objects before shutdown.
    presentation.Reset();
    (void)source->Shutdown();
    source.Reset();
    std::wcout << L"AVCAM_INTEGRATED_MF_TYPES=" << total << L"\n"
               << L"AVCAM_INTEGRATED_MJPEG_1280X720="
               << (mjpg1280 ? L"ADVERTISED" : L"NOT_ADVERTISED") << L"\n"
               << L"AVCAM_MFT_CHAIN_INTEROP=NOT_VERIFIED\n"
               << L"AVCAM_MF_TYPES_READ_ONLY_COMPLETE\n";
    return total ? 0 : 7;
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && std::wstring(argv[1]) == L"--selftest") {
        if (!IsTarget(L"\\\\?\\usb#vid_13d3&pid_56ff&mi_00#instance"))
            return 10;
        if (IsTarget(L"\\\\?\\usb#vid_046d&pid_0825&mi_00#instance"))
            return 11;
        if (!IsReplacementType(MFVideoFormat_MJPG, 1280, 720) ||
            IsReplacementType(MFVideoFormat_MJPG, 640, 480) ||
            IsReplacementType(MFVideoFormat_NV12, 1280, 720))
            return 12;
        std::wcout << L"AVCAM_INTEGRATED_MF_FORMAT_SELFTEST_PASS\n";
        return 0;
    }
    if (argc != 2 || std::wstring(argv[1]) != L"--inspect") {
        std::wcerr << L"Usage: AvCamIntegratedCameraFormats.exe --selftest | --inspect\n";
        return 2;
    }
    return Inspect();
}
