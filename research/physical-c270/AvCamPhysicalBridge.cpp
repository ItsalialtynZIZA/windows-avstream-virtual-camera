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

int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && wcscmp(argv[1], L"--self-test") == 0) {
        // Reproduce Camera Frame Server retaining a mapping while the
        // publisher exits, then prove a replacement publisher can take over.
        HANDLE retainedReader = nullptr;
        {
            Mapping first;
            if (!CreateSharedMapping(first)) return 20;
            retainedReader = OpenFileMappingW(FILE_MAP_READ, FALSE,
                AvCamPhysical::kMappingName);
            if (!retainedReader) return 21;
            std::vector<unsigned char> dummy(32, 0xab);
            dummy.front() = dummy.back() = 0xff;
            if (!Publish(first, dummy, GetTickCount64() * 10000ull))
                return 22;
        }
        {
            Mapping second;
            if (!CreateSharedMapping(second)) {
                CloseHandle(retainedReader);
                return 23;
            }
            std::vector<unsigned char> dummy(32, 0xcd);
            dummy.front() = dummy.back() = 0xff;
            if (!Publish(second, dummy, GetTickCount64() * 10000ull)) {
                CloseHandle(retainedReader);
                return 24;
            }
        }
        // A stopped OBS publisher must not leave a permanently fresh
        // frame in the mapping. The Device MFT uses the same monotonic
        // timestamp rule before replacing a physical Logitech sample.
        HANDLE verify = OpenFileMappingW(FILE_MAP_READ, FALSE,
            AvCamPhysical::kMappingName);
        // The retained handle keeps the section alive through the restart.
        if (!verify) {
            CloseHandle(retainedReader);
            return 25;
        }
        const auto* view = static_cast<const BYTE*>(
            MapViewOfFile(verify, FILE_MAP_READ, 0, 0,
                          sizeof(AvCamPhysical::FrameHeader)));
        if (!view) {
            CloseHandle(verify);
            CloseHandle(retainedReader);
            return 26;
        }
        const auto* header =
            reinterpret_cast<const AvCamPhysical::FrameHeader*>(view);
        const std::uint64_t stamp = header->timestamp;
        const std::uint64_t now = GetTickCount64() * 10000ull;
        const bool published = header->magic == AvCamPhysical::kMagic &&
            header->version == AvCamPhysical::kVersion &&
            header->length == 32 && (header->sequence & 1) == 0 &&
            stamp <= now && now - stamp <= 20000000ull;
        UnmapViewOfFile(view);
        CloseHandle(verify);
        CloseHandle(retainedReader);
        if (!published) return 27;
        std::puts("AVCAM_BRIDGE_MAPPING_RESTART_SELFTEST_PASSED");
        return 0;
    }
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
    // A mapping may exist even when OBS Virtual Camera failed to start.
    // Only the first *fresh* frame proves the camera producer is live.
    ULONGLONG lastWarningAt = 0;
    auto warnOccasionally = [&](const char* message, DWORD error) {
        const ULONGLONG now = GetTickCount64();
        if (lastWarningAt == 0 || now - lastWarningAt >= 30000ull) {
            std::printf("%s (0x%08lX). Waiting for recovery; no Logitech capture used.\n",
                message, error);
            lastWarningAt = now;
        }
    };
    std::puts("AvCam Physical Bridge: persistent OBS watcher -> 1280x720 MJPEG.");
    std::puts("No direct capture of the physical Logitech C270 is performed by Bridge.");
    std::puts("Waiting for OBS Virtual Camera; press Ctrl+C to stop.");
    while (!g_stop.load()) {
        ObsSharedMemorySource source;
        if (!source.Start()) {
            // OBS may start later than our scheduled task. An already-present
            // but stale OBS mapping is also handled in the inner read loop.
            if ((reconnects++ % 30ull) == 0ull)
                warnOccasionally("OBS Virtual Camera mapping unavailable", source.LastErrorCode());
            for (int tick = 0; tick < 20 && !g_stop.load(); ++tick)
                Sleep(100);
            continue;
        }
        reconnects = 0;
        bool sourcePublished = false;
        while (!g_stop.load()) {
            std::uint64_t timestamp = 0;
            if (!source.ReadFrame(bgra.data(), bgra.size(), timestamp)) {
                if (sourcePublished)
                    std::puts("OBS stopped publishing; AvCam will retry automatically.");
                else
                    warnOccasionally("OBS mapping exists but no fresh video frames",
                        source.LastErrorCode());
                break;
            }
            if (!AvCamUde::EncodeBgraToMjpeg(wic.Get(), bgra.data(),
                    AVCAM_FRAME_WIDTH, AVCAM_FRAME_HEIGHT,
                    AVCAM_FRAME_STRIDE_BGRA32, jpeg) ||
                !Publish(mapping, jpeg, timestamp)) {
                warnOccasionally("JPEG encoding or publication failed", ERROR_INVALID_DATA);
                break;
            }
            if (!sourcePublished) {
                std::puts("OBS Virtual Camera is actively publishing frames to AvCam.");
                sourcePublished = true;
                lastWarningAt = 0;
            }
            ++frames;
            if ((frames % 900ull) == 0ull)
                std::printf("FramesPublished=%llu; LastJpegBytes=%zu\n",
                    frames, jpeg.size());
        }
        source.Stop();
        // The camera MFT automatically rejects MJPEG after its timestamps
        // expire and then leaves the real Logitech samples unchanged.
        for (int tick = 0; tick < 10 && !g_stop.load(); ++tick)
            Sleep(100);
    }
    std::printf("AvCam Physical Bridge stopped; total frames=%llu\n", frames);
    CoUninitialize();
    return 0;
}
