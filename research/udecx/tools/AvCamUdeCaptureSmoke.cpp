/*
 * Read-only camera smoke test: physical USB C270 -> Windows Media Foundation
 * RGB32 1280x720 -> Windows Imaging Component JPEG 1920x1080.
 *
 * Deliberately NO UdeCx headers, USB IOCTLs, driver installation, registry
 * writes, INF handling or file output. It cannot upload to a virtual camera.
 * Run with the camera connected and closed in other capture applications.
 */
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "../../../feeder/src/MediaFoundationFrameSource.h"
#include "../../../feeder/src/MediaFoundationDeviceSelector.h"
#include "../../../shared/VirtualCameraProtocol.h"
#include "AvCamUdeMjpegEncoder.h"

using Microsoft::WRL::ComPtr;

namespace {
constexpr unsigned kDefaultFrames = 30u;
constexpr unsigned kMaximumFrames = 300u;

struct ComApartment {
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~ComApartment() {
        if (SUCCEEDED(result)) CoUninitialize();
    }
};

bool ParseFrameCount(const std::wstring& text, unsigned& number) {
    if (text.empty()) return false;
    unsigned value = 0u;
    for (wchar_t ch : text) {
        if (ch < L'0' || ch > L'9') return false;
        const unsigned digit = static_cast<unsigned>(ch - L'0');
        if (value > (kMaximumFrames - digit) / 10u) return false;
        value = value * 10u + digit;
    }
    if (value == 0u || value > kMaximumFrames) return false;
    number = value;
    return true;
}

void PrintUsage() {
    std::wcout <<
        L"AvCamUdeCaptureSmoke.exe [--frames 30] [--obs] [--list]\n"
        L"Default: physical Logi C270 HD WebCam, VID_046D&PID_0825.\n"
        L"  --obs  : use uniquely named OBS Virtual Camera instead.\n"
        L"  --list : show Media Foundation camera names (no capture).\n"
        L"  --frames N : capture and encode N samples, N=1..300.\n"
        L"No kernel drivers, USB IOCTLs, file output or admin access.\n";
}

std::uint64_t Fnv1a(const std::vector<unsigned char>& bytes) {
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned char b : bytes) {
        hash ^= b;
        hash *= 1099511628211ull;
    }
    return hash;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    bool useObs = false;
    bool list = false;
    unsigned targetFrames = kDefaultFrames;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--help" || arg == L"-h") {
            PrintUsage();
            return 0;
        }
        if (arg == L"--obs") {
            useObs = true;
        } else if (arg == L"--list") {
            list = true;
        } else if (arg == L"--frames" && i + 1 < argc) {
            if (!ParseFrameCount(argv[++i], targetFrames)) {
                PrintUsage();
                return 2;
            }
        } else {
            PrintUsage();
            return 2;
        }
    }

    ComApartment com;
    if (FAILED(com.result)) {
        std::wcerr << L"CAPTURE_SMOKE_COM_INIT_FAILED: 0x"
                   << std::hex << static_cast<unsigned long>(com.result)
                   << std::dec << L"\n";
        return 3;
    }

    if (list) {
        const auto cameras = EnumerateMediaFoundationVideoSources();
        for (const auto& name : cameras) std::wcout << L"CAMERA: " << name << L"\n";
        std::wcout << L"CAPTURE_SMOKE_ENUMERATION_DONE count="
                   << cameras.size() << L"\n";
        return 0;
    }

    ComPtr<IWICImagingFactory> wic;
    const HRESULT createWic = CoCreateInstance(
        CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&wic));
    if (FAILED(createWic)) {
        std::wcerr << L"CAPTURE_SMOKE_WIC_UNAVAILABLE\n";
        return 4;
    }

    const std::wstring name = useObs ?
        L"OBS Virtual Camera" : L"Logi C270 HD WebCam";
    const std::wstring usb = useObs ? L"" : L"VID_046D&PID_0825";

    if (!useObs && !AvCamSourceSelection::IsVidPidPair(usb)) {
        std::wcerr << L"CAPTURE_SMOKE_INVALID_PHYSICAL_ID\n";
        return 5;
    }

    std::wcout << L"Source: " << name
               << L" USB filter: " << (usb.empty() ? L"(name only)" : usb)
               << L"\n";
    std::wcout << L"Capture: 1280x720 BGRA; MJPEG output: 1920x1080\n";

    MediaFoundationFrameSource camera(name, usb);
    if (!camera.Start()) {
        std::wcerr <<
            L"CAPTURE_SMOKE_SOURCE_UNAVAILABLE: camera missing, busy, "
            L"ambiguous, or unsupported 1280x720/30 RGB32 output.\n";
        std::wcerr <<
            L"For C270, close OBS or other applications using the webcam. "
            L"To test OBS, start its virtual camera and use --obs.\n";
        return 6;
    }

    std::vector<unsigned char> frame(AVCAM_FRAME_BYTES_BGRA32);
    std::vector<unsigned char> jpeg;
    unsigned samples = 0u;
    std::size_t minJpeg = static_cast<std::size_t>(-1);
    std::size_t maxJpeg = 0u;
    std::uint64_t firstHash = 0u;
    unsigned distinct = 0u;
    const ULONGLONG start = GetTickCount64();

    for (unsigned i = 0; i < targetFrames; ++i) {
        std::uint64_t timestamp = 0u;
        if (!camera.ReadFrame(frame.data(), frame.size(), timestamp)) {
            std::wcerr << L"CAPTURE_SMOKE_READ_FAILED sample=" << i << L"\n";
            return 7;
        }
        if (!AvCamUde::EncodeBgraToMjpeg(
                wic.Get(), frame.data(), AVCAM_FRAME_WIDTH,
                AVCAM_FRAME_HEIGHT, AVCAM_FRAME_STRIDE_BGRA32, jpeg)) {
            std::wcerr << L"CAPTURE_SMOKE_MJPEG_ENCODE_FAILED sample=" << i
                       << L"\n";
            return 8;
        }
        if (jpeg.size() < 4u ||
            jpeg[0] != 0xffu || jpeg[1] != 0xd8u ||
            jpeg[jpeg.size() - 2u] != 0xffu ||
            jpeg[jpeg.size() - 1u] != 0xd9u) {
            std::wcerr << L"CAPTURE_SMOKE_JPEG_MARKERS_INVALID sample=" << i
                       << L"\n";
            return 9;
        }
        const std::uint64_t hash = Fnv1a(jpeg);
        if (samples == 0u) firstHash = hash;
        if (hash != firstHash) ++distinct;
        if (jpeg.size() < minJpeg) minJpeg = jpeg.size();
        if (jpeg.size() > maxJpeg) maxJpeg = jpeg.size();
        ++samples;
    }
    camera.Stop();
    const ULONGLONG elapsed = GetTickCount64() - start;

    std::wcout << L"FramesCaptured=" << samples
               << L" FramesEncoded=" << samples
               << L" JPEGMinBytes=" << minJpeg
               << L" JPEGMaxBytes=" << maxJpeg
               << L" DifferentFromFirst=" << distinct
               << L" ElapsedMs=" << elapsed << L"\n";
    if (samples != targetFrames) return 10;
    std::wcout << L"AVCAM_C270_MF_MJPEG_SMOKE_PASSED\n";
    std::wcout << L"Physical capture and JPEG encoding only. "
                  L"NO UdeCx device or browser video was tested.\n";
    return 0;
}
