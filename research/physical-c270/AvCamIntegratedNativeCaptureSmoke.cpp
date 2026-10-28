// Bounded normal-camera capture smoke for Lenovo 82K1 (13D3:56FF).
// Uses the OEM Media Foundation capture pipeline and native MJPEG 1280x720.
// Does not replace any frames, write frames to disk, register COM or edit drivers.
// A separate PowerShell watchdog MUST bound ReadSample() in case a driver blocks.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <iomanip>
#include <iostream>
#include <string>

using Microsoft::WRL::ComPtr;

namespace {
constexpr DWORD kStream = MF_SOURCE_READER_FIRST_VIDEO_STREAM;
constexpr unsigned kFrames = 30;

struct MfLifetime {
    HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    HRESULT mf = FAILED(co) ? co : MFStartup(MF_VERSION);
    ~MfLifetime() {
        if (SUCCEEDED(mf)) (void)MFShutdown();
        if (SUCCEEDED(co)) CoUninitialize();
    }
};
struct Activations {
    IMFActivate** array = nullptr;
    UINT32 count = 0;
    ~Activations() {
        if (!array) return;
        for (UINT32 i = 0; i < count; ++i)
            if (array[i]) array[i]->Release();
        CoTaskMemFree(array);
    }
};
std::wstring Link(IMFActivate* obj) {
    WCHAR* data = nullptr;
    UINT32 chars = 0;
    const HRESULT hr = obj->GetAllocatedString(
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &data, &chars);
    const std::wstring result = SUCCEEDED(hr) && data ? std::wstring(data) : L"";
    if (data) CoTaskMemFree(data);
    return result;
}
bool IsTarget(std::wstring link) {
    std::transform(link.begin(), link.end(), link.begin(),
                   [](wchar_t c) { return std::towlower(c); });
    return link.find(L"vid_13d3&pid_56ff") != std::wstring::npos &&
           link.find(L"mi_00") != std::wstring::npos;
}
bool IsRequiredMediaType(IMFMediaType* type) {
    if (!type) return false;
    GUID sub{};
    UINT32 w=0, h=0, n=0, d=0;
    if (FAILED(type->GetGUID(MF_MT_SUBTYPE, &sub)) ||
        FAILED(MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &w, &h)))
        return false;
    (void)MFGetAttributeRatio(type, MF_MT_FRAME_RATE, &n, &d);
    // The live smoke is exactly the format already advertised by the OEM.
    return sub == MFVideoFormat_MJPG && w == 1280 && h == 720 &&
           d != 0 && (uint64_t)n == 30ull * d;
}
bool HasJpegEnvelope(const unsigned char* data, size_t size) {
    if (!data || size < 32u) return false;
    if (data[0] != 0xff || data[1] != 0xd8) return false;
    // Camera/driver may append a little padding after the JPEG EOI.
    const size_t start = size > 32 ? size - 32 : 2;
    for (size_t i = start; i + 1 < size; ++i) {
        if (data[i] == 0xff && data[i + 1] == 0xd9) return true;
    }
    return false;
}
uint64_t FingerprintPrefix(const unsigned char* data, size_t length) {
    uint64_t h = 14695981039346656037ull;
    const size_t count = std::min(length, size_t(4096));
    for (size_t i = 0; i < count; ++i) {
        h ^= data[i];
        h *= 1099511628211ull;
    }
    return h;
}
void Error(const wchar_t* label, HRESULT hr) {
    std::wcerr << label << L" HRESULT=0x" << std::hex
               << static_cast<unsigned long>(hr) << std::dec << L"\n";
}
int Capture() {
    MfLifetime scope;
    if (FAILED(scope.co) || FAILED(scope.mf)) {
        Error(L"Media Foundation startup failed", FAILED(scope.co) ? scope.co : scope.mf);
        return 3;
    }
    ComPtr<IMFAttributes> attrs;
    HRESULT hr = MFCreateAttributes(&attrs, 1);
    if (SUCCEEDED(hr)) hr = attrs->SetGUID(
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if (FAILED(hr)) { Error(L"Create attributes", hr); return 3; }
    Activations devices;
    hr = MFEnumDeviceSources(attrs.Get(), &devices.array, &devices.count);
    if (FAILED(hr)) { Error(L"MFEnumDeviceSources", hr); return 4; }
    IMFActivate* selected = nullptr;
    unsigned matches = 0;
    for (UINT32 i = 0; i < devices.count; ++i) {
        if (devices.array[i] && IsTarget(Link(devices.array[i]))) {
            selected = devices.array[i];
            ++matches;
        }
    }
    std::wcout << L"AVCAM_CAPTURE_CAMERA_MATCHES=" << matches << std::endl;
    if (matches != 1) return 5;

    ComPtr<IMFMediaSource> source;
    hr = selected->ActivateObject(IID_PPV_ARGS(source.GetAddressOf()));
    if (FAILED(hr)) { Error(L"Activate Integrated Camera", hr); return 6; }

    // Scoped release before device shutdown, even on a failed acquisition.
    int exitCode = 0;
    {
        ComPtr<IMFSourceReader> reader;
        hr = MFCreateSourceReaderFromMediaSource(source.Get(), nullptr, &reader);
        if (FAILED(hr)) {
            Error(L"MFCreateSourceReaderFromMediaSource", hr);
            exitCode = 7;
        } else {
            ComPtr<IMFMediaType> required;
            for (DWORD idx = 0; idx < 256; ++idx) {
                ComPtr<IMFMediaType> media;
                hr = reader->GetNativeMediaType(kStream, idx, &media);
                if (hr == MF_E_NO_MORE_TYPES) break;
                if (FAILED(hr)) {
                    Error(L"GetNativeMediaType", hr);
                    break;
                }
                if (IsRequiredMediaType(media.Get())) {
                    required = media;
                    break;
                }
            }
            if (!required) {
                std::wcerr << L"Native MJPEG 1280x720@30 type not available\n";
                exitCode = 8;
            } else {
                hr = reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
                if (SUCCEEDED(hr)) hr = reader->SetStreamSelection(kStream, TRUE);
                if (SUCCEEDED(hr)) hr = reader->SetCurrentMediaType(
                    kStream, nullptr, required.Get());
                ComPtr<IMFMediaType> actual;
                if (SUCCEEDED(hr)) hr = reader->GetCurrentMediaType(kStream, &actual);
                if (FAILED(hr) || !IsRequiredMediaType(actual.Get())) {
                    Error(L"Select native MJPEG 1280x720@30", hr);
                    exitCode = 9;
                } else {
                    std::wcout << L"AVCAM_CAPTURE_NEGOTIATED=MJPG_1280X720_30" << std::endl;
                    unsigned good = 0, malformed = 0, changed = 0, ticks = 0;
                    DWORD firstFlags = 0;
                    uint64_t priorHash = 0;
                    bool priorFrame = false;
                    LONGLONG priorTs = -1;
                    unsigned nonIncreasing = 0;
                    const auto startMs = GetTickCount64();
                    for (unsigned attempts = 0; attempts < kFrames + 12 && good < kFrames; ++attempts) {
                        DWORD stream = 0, flags = 0;
                        LONGLONG ts = 0;
                        ComPtr<IMFSample> sample;
                        hr = reader->ReadSample(kStream, 0, &stream, &flags, &ts, &sample);
                        if (FAILED(hr)) {
                            Error(L"ReadSample", hr);
                            exitCode = 10; break;
                        }
                        firstFlags |= flags;
                        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                            std::wcerr << L"Unexpected end of camera stream\n";
                            exitCode = 11; break;
                        }
                        if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
                            std::wcerr << L"Camera changed the negotiated media type\n";
                            exitCode = 12; break;
                        }
                        if (!sample) {
                            ++ticks;
                            continue;
                        }
                        ComPtr<IMFMediaBuffer> buf;
                        hr = sample->ConvertToContiguousBuffer(&buf);
                        if (FAILED(hr)) {
                            Error(L"ConvertToContiguousBuffer", hr);
                            exitCode = 13; break;
                        }
                        BYTE* bytes = nullptr;
                        DWORD maxLength = 0, length = 0;
                        hr = buf->Lock(&bytes, &maxLength, &length);
                        if (FAILED(hr)) {
                            Error(L"IMFMediaBuffer::Lock", hr);
                            exitCode = 14; break;
                        }
                        bool valid = HasJpegEnvelope(bytes, length);
                        uint64_t hash = valid ? FingerprintPrefix(bytes, length) : 0;
                        (void)buf->Unlock();
                        if (!valid) {
                            ++malformed;
                            continue;
                        }
                        if (priorFrame && hash != priorHash) ++changed;
                        priorHash = hash;
                        priorFrame = true;
                        if (priorTs >= 0 && ts <= priorTs) ++nonIncreasing;
                        priorTs = ts;
                        ++good;
                    }
                    const auto elapsedMs = GetTickCount64() - startMs;
                    std::wcout << L"AVCAM_CAPTURE_FRAMES=" << good
                               << L" CHANGED_PREFIXES=" << changed
                               << L" MALFORMED=" << malformed
                               << L" TICKS=" << ticks
                               << L" NON_INCREASING_TS=" << nonIncreasing
                               << L" ELAPSED_MS=" << elapsedMs
                               << L" FLAGS=" << firstFlags << std::endl;
                    if (exitCode == 0 && good == kFrames && malformed == 0 && nonIncreasing == 0) {
                        std::wcout << L"AVCAM_INTEGRATED_NATIVE_MJPEG_CAPTURE_PASS" << std::endl;
                    } else if (exitCode == 0) {
                        exitCode = 15;
                    }
                }
            }
        }
    }
    (void)source->Shutdown();
    source.Reset();
    std::wcout << L"AVCAM_MFT_CHAIN_INTEROP=NOT_VERIFIED" << std::endl;
    return exitCode;
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && std::wstring(argv[1]) == L"--selftest") {
        const unsigned char valid[] = {0xff,0xd8,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                                       0,0,0,0,0,0,0,0,0,0,0,0,0,0,0xff,0xd9};
        if (!HasJpegEnvelope(valid, sizeof(valid))) return 20;
        if (HasJpegEnvelope(valid, 4)) return 21;
        if (IsTarget(L"usb#vid_046d&pid_0825&mi_00")) return 22;
        if (!IsTarget(L"usb#vid_13d3&pid_56ff&mi_00")) return 23;
        std::wcout << L"AVCAM_INTEGRATED_NATIVE_MJPEG_SELFTEST_PASS" << std::endl;
        return 0;
    }
    if (argc != 2 || std::wstring(argv[1]) != L"--capture-smoke") {
        std::wcerr << L"Usage: AvCamIntegratedNativeCaptureSmoke.exe --selftest | --capture-smoke\n";
        return 2;
    }
    return Capture();
}
