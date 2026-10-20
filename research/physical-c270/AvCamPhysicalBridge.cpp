// Sends OBS Virtual Camera 1280x720 MJPEG to a bounded, cross-session mapping.
// No driver, registry, or physical camera modifications. Requires an elevated
// console only to create the Global\ page-file mapping.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <string>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>
#include "AvCamPhysicalWire.h"
#include "../../feeder/src/ObsSharedMemorySource.h"
#include "../udecx/tools/AvCamUdeMjpegEncoder.h"
#include "../../shared/VirtualCameraProtocol.h"

namespace {
std::atomic<bool> g_stop{false};
BOOL WINAPI CtrlHandler(DWORD code) {
    if (code == CTRL_C_EVENT || code == CTRL_BREAK_EVENT || code == CTRL_CLOSE_EVENT) {
        g_stop.store(true);
        return TRUE;
    }
    return FALSE;
}
struct Mapping {
    HANDLE handle = nullptr;
    HANDLE publisherMutex = nullptr;
    BYTE* view = nullptr;
    ~Mapping() {
        if (view) UnmapViewOfFile(view);
        if (handle) CloseHandle(handle);
        if (publisherMutex) {
            ReleaseMutex(publisherMutex);
            CloseHandle(publisherMutex);
        }
    }
};
bool CreateSharedMapping(Mapping& map) {
    // An abandoned publisher must be replaceable without restarting the
    // Windows Camera Frame Server, which can retain a shared mapping handle.
    // A named mutex prevents two live publishers from writing concurrently.
    map.publisherMutex = CreateMutexW(nullptr, FALSE,
        L"Global\\AvCamPhysicalPublisherLockV1");
    if (!map.publisherMutex) {
        std::printf("Create publisher mutex failed: %lu\n", GetLastError());
        return false;
    }
    const DWORD lockResult = WaitForSingleObject(map.publisherMutex, 0);
    if (lockResult != WAIT_OBJECT_0 && lockResult != WAIT_ABANDONED) {
        std::puts("Another AvCam Physical Bridge publisher is already running.");
        CloseHandle(map.publisherMutex);
        map.publisherMutex = nullptr;
        return false;
    }
    // Service accounts can read frames, but cannot modify them. Only elevated
    // admins and LocalSystem can publish frames. Never grant Everyone write.
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
        L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GR;;;LS)(A;;GR;;;NS)",
        SDDL_REVISION_1, &sd, nullptr)) return false;
    SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
    map.handle = CreateFileMappingW(INVALID_HANDLE_VALUE, &sa,
        PAGE_READWRITE, 0, AvCamPhysical::kTotalBytes,
        AvCamPhysical::kMappingName);
    const DWORD result = GetLastError();
    LocalFree(sd);
    if (!map.handle) {
        std::printf("Create Global mapping failed, error %lu (run as administrator)\n", result);
        return false;
    }
    if (result == ERROR_ALREADY_EXISTS) {
        // Expected if a camera process still has the previous mapping open.
        // Our exclusive publisher mutex makes this takeover safe.
        std::puts("Reusing retained AvCam camera mapping after publisher restart.");
    }
    map.view = static_cast<BYTE*>(MapViewOfFile(map.handle, FILE_MAP_WRITE,
        0, 0, AvCamPhysical::kTotalBytes));
    if (!map.view) {
        std::printf("MapViewOfFile failed: %lu\n", GetLastError());
        return false;
    }
    ZeroMemory(map.view, AvCamPhysical::kTotalBytes);
    return true;
}
bool Publish(Mapping& mapping, const std::vector<unsigned char>& jpeg,
             std::uint64_t timestamp) {
    if (jpeg.size() < 32 || jpeg.size() > AvCamPhysical::kCapacity) return false;
    auto* h = reinterpret_cast<AvCamPhysical::FrameHeader*>(mapping.view);
    InterlockedIncrement(&h->sequence); // odd, changing
    h->magic = AvCamPhysical::kMagic;
    h->version = AvCamPhysical::kVersion;
    h->width = AvCamPhysical::kWidth;
    h->height = AvCamPhysical::kHeight;
    h->length = static_cast<std::uint32_t>(jpeg.size());
    h->timestamp = timestamp;
    memcpy(mapping.view + sizeof(*h), jpeg.data(), jpeg.size());
    MemoryBarrier();
    InterlockedIncrement(&h->sequence); // even, complete
    return true;
}
}

int wmain() {
    SetConsoleCtrlHandler(CtrlHandler, TRUE);
    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(init)) return 10;
    Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) {
        CoUninitialize();
        return 11;
    }
    Mapping mapping;
    if (!CreateSharedMapping(mapping)) {
        CoUninitialize();
        return 12;
    }
    std::vector<unsigned char> bgra(AVCAM_FRAME_BYTES_BGRA32);
    std::vector<unsigned char> jpeg;
    unsigned long long frames = 0;
    unsigned long long reconnects = 0;
    std::puts("AvCam Physical Bridge: persistent OBS watcher -> 1280x720 MJPEG.");
    std::puts("Waiting for OBS Virtual Camera; press Ctrl+C to stop.");
    while (!g_stop.load()) {
        ObsSharedMemorySource source;
        if (!source.Start()) {
            // OBS may start after Windows logon. Wait indefinitely instead
            // of exiting and requiring another elevated manual launch.
            if ((reconnects++ % 30ull) == 0ull)
                std::printf("Waiting for OBS Virtual Camera (0x%08lX)\n",
                    source.LastErrorCode());
            for (int tick = 0; tick < 20 && !g_stop.load(); ++tick)
                Sleep(100);
            continue;
        }
        reconnects = 0;
        std::puts("OBS Virtual Camera connected.");
        while (!g_stop.load()) {
            std::uint64_t timestamp = 0;
            if (!source.ReadFrame(bgra.data(), bgra.size(), timestamp)) {
                std::printf("OBS feed interrupted (0x%08lX). Reconnecting...\n",
                    source.LastErrorCode());
                break;
            }
            if (!AvCamUde::EncodeBgraToMjpeg(wic.Get(), bgra.data(),
                    AVCAM_FRAME_WIDTH, AVCAM_FRAME_HEIGHT,
                    AVCAM_FRAME_STRIDE_BGRA32, jpeg) ||
                !Publish(mapping, jpeg, timestamp)) {
                std::puts("JPEG encoding or publication failed; retrying.");
                break;
            }
            ++frames;
            if ((frames % 900ull) == 0ull)
                std::printf("FramesPublished=%llu; LastJpegBytes=%zu\n",
                    frames, jpeg.size());
        }
        source.Stop();
        // The Device MFT checks frame age. When OBS is stopped or missing,
        // it safely falls back to the original Logitech camera image.
        for (int tick = 0; tick < 10 && !g_stop.load(); ++tick)
            Sleep(100);
    }
    std::printf("AvCam Physical Bridge stopped; total frames=%llu\n", frames);
    CoUninitialize();
    return 0;
}
