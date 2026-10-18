/*
 * Research-only JPEG file publisher for the native UdeCx controller.
 *
 * Compiled in CI, NOT installed or executed there. Requires an explicitly
 * plugged UdeCx USB child in an isolated VM. The production/legacy AVStream
 * feeder is unchanged. Running against the normal guarded research driver
 * returns ERROR_NOT_READY rather than pretending a camera exists.
 *
 * Usage: AvCamUdeUpload.exe image_1920x1080.jpg [--repeat]
 */
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>
#include "../AvCamUdeUploadProtocol.h"

namespace {
constexpr wchar_t kControllerPath[] = LR"(\\.\AvCamUdeController0)";

struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() {
        if (value != INVALID_HANDLE_VALUE) CloseHandle(value);
    }
    Handle() = default;
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

bool LoadJpeg(const wchar_t* path, std::vector<unsigned char>& result) {
    Handle file;
    file.value = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file.value == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.value, &size) || size.QuadPart < 16 ||
        size.QuadPart > AVCAM_UVC_FRAME_MAX_BYTES) {
        return false;
    }
    const DWORD bytes = static_cast<DWORD>(size.QuadPart);
    result.resize(sizeof(AVCAM_UDE_UPLOAD_HEADER) + bytes);
    AVCAM_UDE_UPLOAD_HEADER header{};
    header.Magic = AVCAM_UDE_UPLOAD_MAGIC;
    header.Version = AVCAM_UDE_UPLOAD_VERSION;
    header.Width = AVCAM_UDE_UPLOAD_WIDTH;
    header.Height = AVCAM_UDE_UPLOAD_HEIGHT;
    header.FrameBytes = bytes;
    std::memcpy(result.data(), &header, sizeof(header));

    DWORD total = 0;
    while (total < bytes) {
        DWORD count = 0;
        if (!ReadFile(file.value, result.data() + sizeof(header) + total,
                      bytes - total, &count, nullptr) || count == 0) {
            return false;
        }
        total += count;
    }
    const unsigned char* jpeg = nullptr;
    unsigned frameBytes = 0;
    return AvCamUdeValidateUpload(
        result.data(), static_cast<unsigned>(result.size()),
        &jpeg, &frameBytes) == AVCAM_UDE_UPLOAD_OK &&
        frameBytes == bytes;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 2 && argc != 3) {
        std::wcerr << L"Usage: AvCamUdeUpload.exe image_1920x1080.jpg [--repeat]\n";
        return 2;
    }
    const bool repeat = argc == 3 && wcscmp(argv[2], L"--repeat") == 0;
    if (argc == 3 && !repeat) {
        std::wcerr << L"Unknown option; only --repeat is supported\n";
        return 2;
    }

    std::vector<unsigned char> packet;
    if (!LoadJpeg(argv[1], packet)) {
        std::wcerr << L"Invalid/unreadable JPEG; 1920x1080 SOF and size <= "
                   << AVCAM_UVC_FRAME_MAX_BYTES << L" bytes required\n";
        return 3;
    }

    Handle controller;
    controller.value = CreateFileW(
        kControllerPath, GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (controller.value == INVALID_HANDLE_VALUE) {
        std::wcerr << L"UdeCx controller unavailable (expected before VM install). "
                   << L"Win32 error=" << GetLastError() << L"\n";
        return 4;
    }

    unsigned long long count = 0;
    do {
        DWORD returned = 0;
        if (!DeviceIoControl(controller.value, IOCTL_AVCAM_UDE_SUBMIT_MJPEG,
                             packet.data(), static_cast<DWORD>(packet.size()),
                             nullptr, 0, &returned, nullptr)) {
            std::wcerr << L"JPEG rejected/not ready; Win32 error="
                       << GetLastError() << L". No frame was claimed.\n";
            return 5;
        }
        ++count;
        if (!repeat) break;
        Sleep(33);
    } while (true);

    std::wcout << L"JPEG accepted for staging: " << count
               << L" upload(s). This does NOT verify USB/Media Foundation video.\n";
    return 0;
}
