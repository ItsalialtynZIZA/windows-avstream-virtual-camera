/*
 * AvCam UdeCx research user-mode source: OBS Virtual Camera -> Media Foundation
 * -> BGRA32 -> WIC JPEG 1920x1080 -> private UdeCx controller upload IOCTL.
 *
 * The default research driver has child autoprobe disabled and therefore
 * rejects actual upload. Do NOT install the driver on your primary Windows.
 * The --dry-run mode exercises real OBS capture/encoding without touching it.
 */
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "../../../feeder/src/MediaFoundationFrameSource.h"
#include "../../../shared/VirtualCameraProtocol.h"
#include "../AvCamUdeUploadProtocol.h"
#include "AvCamUdeMjpegEncoder.h"
#include "../../../feeder/src/MediaFoundationDeviceSelector.h"

using Microsoft::WRL::ComPtr;

namespace {
constexpr wchar_t kControllerName[] = LR"(\\.\AvCamUdeController0)";
struct Com {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~Com() { if (SUCCEEDED(hr)) CoUninitialize(); }
};
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle() = default;
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
void Usage() {
    std::wcout
        << L"AvCamUdeObsFeeder --device-name \"OBS Virtual Camera\" "
        << L"[--frames 120] [--dry-run]\n"
        << L"Research VM only. --dry-run does not open the UdeCx controller.\n";
}
bool ParsePositive(const wchar_t* value, unsigned& count) {
    if (value == nullptr || *value == L'\0') return false;
    unsigned result = 0;
    for (const wchar_t* p = value; *p; ++p) {
        if (*p < L'0' || *p > L'9') return false;
        const unsigned digit = static_cast<unsigned>(*p - L'0');
        if (result > (10000u - digit) / 10u) return false;
        result = result * 10u + digit;
    }
    if (result == 0) return false;
    count = result;
    return true;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    std::wstring deviceName = L"OBS Virtual Camera";
    std::wstring sourceUsbId;
    unsigned frames = 120u;
    bool dryRun = false;
    for (int index = 1; index < argc; ++index) {
        const std::wstring arg = argv[index];
        if (arg == L"--device-name" && index + 1 < argc) {
            deviceName = argv[++index];
        } else if (arg == L"--source-usb-id" && index + 1 < argc) {
            sourceUsbId = argv[++index];
            if (!AvCamSourceSelection::IsVidPidPair(sourceUsbId)) {
                std::wcerr << L"Expected USB ID VID_XXXX&PID_XXXX.\\n";
                return 2;
            }
        } else if (arg == L"--frames" && index + 1 < argc) {
            if (!ParsePositive(argv[++index], frames)) {
                Usage();
                return 2;
            }
        } else if (arg == L"--dry-run") {
            dryRun = true;
        } else if (arg == L"--help") {
            Usage();
            return 0;
        } else {
            Usage();
            return 2;
        }
    }
    if (deviceName.empty()) return 2;

    Com com;
    if (FAILED(com.hr)) {
        std::wcerr << L"COM MTA initialization failed\n";
        return 3;
    }
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(
        CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&wic)))) {
        std::wcerr << L"WIC factory unavailable\n";
        return 3;
    }

    MediaFoundationFrameSource source(deviceName, sourceUsbId);
    if (!source.Start()) {
        std::wcerr << L"Media Foundation source unavailable: "
                   << deviceName << L"\n";
        return 4;
    }
    Handle controller;
    if (!dryRun) {
        controller.value = CreateFileW(
            kControllerName, GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (controller.value == INVALID_HANDLE_VALUE) {
            std::wcerr << L"Research UdeCx controller not available. "
                       << L"Win32 error=" << GetLastError() << L"\n";
            source.Stop();
            return 5;
        }
    }

    std::vector<unsigned char> bgra(AVCAM_FRAME_BYTES_BGRA32);
    std::vector<unsigned char> jpeg;
    std::vector<unsigned char> packet;
    unsigned accepted = 0;
    unsigned busy = 0;
    for (unsigned index = 0; index < frames; ++index) {
        std::uint64_t timestamp = 0;
        if (!source.ReadFrame(bgra.data(), bgra.size(), timestamp)) {
            std::wcerr << L"Capture failed at frame " << index << L"\n";
            source.Stop();
            return 6;
        }
        if (!AvCamUde::EncodeBgraToMjpeg(
            wic.Get(), bgra.data(), AVCAM_FRAME_WIDTH,
            AVCAM_FRAME_HEIGHT, AVCAM_FRAME_STRIDE_BGRA32, jpeg)) {
            std::wcerr << L"WIC MJPEG encode failed, or JPEG exceeds "
                       << AVCAM_UVC_FRAME_MAX_BYTES << L" bytes\n";
            source.Stop();
            return 7;
        }
        if (!dryRun) {
            AVCAM_UDE_UPLOAD_HEADER header{};
            header.Magic = AVCAM_UDE_UPLOAD_MAGIC;
            header.Version = AVCAM_UDE_UPLOAD_VERSION;
            header.Width = AVCAM_UDE_UPLOAD_WIDTH;
            header.Height = AVCAM_UDE_UPLOAD_HEIGHT;
            header.FrameBytes = static_cast<unsigned>(jpeg.size());
            packet.resize(sizeof(header) + jpeg.size());
            std::memcpy(packet.data(), &header, sizeof(header));
            std::memcpy(packet.data() + sizeof(header),
                        jpeg.data(), jpeg.size());
            DWORD returned = 0;
            if (!DeviceIoControl(controller.value,
                  IOCTL_AVCAM_UDE_SUBMIT_MJPEG,
                  packet.data(), static_cast<DWORD>(packet.size()),
                  nullptr, 0, &returned, nullptr)) {
                const DWORD error = GetLastError();
                if (error == ERROR_BUSY) {
                    ++busy; // Back-pressure: drop rather than retry stale frame.
                    continue;
                }
                std::wcerr << L"UdeCx research frame upload failed: "
                           << error << L"\n";
                source.Stop();
                return 8;
            }
        }
        ++accepted;
    }
    source.Stop();
    std::wcout << L"Capture/encode completed. Frames=" << accepted
               << L" skipped_busy=" << busy
               << L" mode=" << (dryRun ? L"dry-run" : L"upload")
               << L". No Media Foundation USB capture is implied.\n";
    return 0;
}
