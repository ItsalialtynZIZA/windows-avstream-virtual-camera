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
#include "mf-source/AvCamMfColor.h"
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
bool CreateSharedMapping(Mapping& map,
                         const wchar_t* mappingName = AvCamPhysical::kMappingName,
                         const wchar_t* mutexName = L"Global\\AvCamPhysicalPublisherLockV1",
                         bool localDiagnostic = false,
                         DWORD mappingBytes = AvCamPhysical::kTotalBytes) {
    // An abandoned publisher must be replaceable without restarting the
    // Windows Camera Frame Server, which can retain a shared mapping handle.
    // A named mutex prevents two live publishers from writing concurrently.
    map.publisherMutex = CreateMutexW(nullptr, FALSE, mutexName);
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
    map.handle = CreateFileMappingW(INVALID_HANDLE_VALUE,
        localDiagnostic ? nullptr : &sa,
        PAGE_READWRITE, 0, mappingBytes,
        mappingName);
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
        0, 0, mappingBytes));
    if (!map.view) {
        std::printf("MapViewOfFile failed: %lu\n", GetLastError());
        return false;
    }
    ZeroMemory(map.view, mappingBytes);
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
// A second, format-distinct mapping is optional for old JPEG-only clients.
// The header is committed atomically with the same odd/even sequence logic.
bool PublishNv12(Mapping& mapping, const std::vector<std::uint8_t>& pixels,
                 std::uint64_t timestamp) {
    if (!mapping.view || pixels.size() != AvCamPhysical::kNv12PayloadBytes)
        return false;
    auto* h = reinterpret_cast<AvCamPhysical::FrameHeader*>(mapping.view);
    InterlockedIncrement(&h->sequence);
    h->magic = AvCamPhysical::kNv12Magic;
    h->version = AvCamPhysical::kVersion;
    h->width = AvCamPhysical::kWidth;
    h->height = AvCamPhysical::kHeight;
    h->length = AvCamPhysical::kNv12PayloadBytes;
    h->timestamp = timestamp;
    memcpy(mapping.view + sizeof(*h), pixels.data(), pixels.size());
    MemoryBarrier();
    InterlockedIncrement(&h->sequence);
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

        // Validate the second uncompressed-format wire path end to end:
        // BGRA -> NV12 -> versioned section -> byte-identical reader.
        // Use a per-process LOCAL section, never the real camera mapping.
        wchar_t nv12Name[96]{}, mutexName[96]{};
        (void)swprintf_s(nv12Name,
            L"Local\\AvCamNv12TransportSelfTest_%lu", GetCurrentProcessId());
        (void)swprintf_s(mutexName,
            L"Local\\AvCamNv12TransportSelfTestLock_%lu", GetCurrentProcessId());
        Mapping nv12Local;
        if (!CreateSharedMapping(nv12Local, nv12Name, mutexName, true,
                                 AvCamPhysical::kNv12TotalBytes)) return 40;
        std::vector<std::uint8_t> bgraSelf(avcam::mfcolor::BgraBytes);
        for (std::size_t i = 0; i < bgraSelf.size(); i += 4) {
            bgraSelf[i] = 33; bgraSelf[i + 1] = 71;
            bgraSelf[i + 2] = 204; bgraSelf[i + 3] = 255;
        }
        std::vector<std::uint8_t> pixels(avcam::mfcolor::Nv12Bytes);
        if (!avcam::mfcolor::Convert(
                bgraSelf.data(), bgraSelf.size(),
                pixels.data(), pixels.size())) return 41;
        if (!PublishNv12(nv12Local, pixels,
                         GetTickCount64() * 10000ull)) return 42;

        HANDLE readHandle = OpenFileMappingW(FILE_MAP_READ, FALSE, nv12Name);
        if (!readHandle) return 43;
        const auto* bytes = static_cast<const BYTE*>(MapViewOfFile(
            readHandle, FILE_MAP_READ, 0, 0,
            AvCamPhysical::kNv12TotalBytes));
        if (!bytes) { CloseHandle(readHandle); return 44; }
        const auto* h = reinterpret_cast<const AvCamPhysical::FrameHeader*>(bytes);
        const bool headerOk = h->magic == AvCamPhysical::kNv12Magic &&
            h->version == AvCamPhysical::kVersion &&
            h->width == 1280 && h->height == 720 &&
            h->length == AvCamPhysical::kNv12PayloadBytes &&
            (h->sequence & 1) == 0 &&
            h->timestamp <= GetTickCount64() * 10000ull &&
            (GetTickCount64() * 10000ull - h->timestamp) <= 20000000ull;
        const bool bytesOk = headerOk &&
            std::memcmp(bytes + sizeof(*h), pixels.data(), pixels.size()) == 0;
        UnmapViewOfFile(bytes);
        CloseHandle(readHandle);
        if (!bytesOk) return 45;
        std::puts("AVCAM_NV12_TRANSPORT_LOCAL_SELFTEST_PASS camera_modified=0");
        return 0;
    }
    // Optional staged-start diagnostic: start Deep Live Cam against the
    // unmodified physical stream before allowing OBS publication.
    // No camera configuration is changed by this switch.
    const bool waitForEnter =
        argc == 2 && wcscmp(argv[1], L"--wait-for-enter") == 0;
    // Bounded, camera-free solid-red MJPEG transport test. Does not activate
    // any device-specific MFT or make the physical Logitech output red.
    const bool redTest = argc == 2 && wcscmp(argv[1], L"--red-test") == 0;
    const bool obsTest = argc == 2 && wcscmp(argv[1], L"--obs-test") == 0;
    if (argc > 1 && !waitForEnter && !redTest && !obsTest) {
        std::puts("Usage: AvCamPhysicalBridge.exe [--self-test|--wait-for-enter|--red-test|--obs-test]");
        return 2;
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
    // An isolated Local\\ test mapping can NEVER be read by the installed
    // device-wide MFT, which expects Global\\AvCamPhysicalMjpegV1.
    // Even a stale legacy MFT installation therefore cannot consume red-test
    // samples accidentally. The normal OBS bridge keeps its legacy ABI.
    const wchar_t* const targetMapping = redTest ?
        L"Local\\AvCamRedMjpegDiagnosticV1" :
        (obsTest ? L"Local\\AvCamObsMjpegDiagnosticV1" : AvCamPhysical::kMappingName);
    const wchar_t* const publisherLock = redTest ?
        L"Local\\AvCamRedPublisherLockV1" :
        (obsTest ? L"Local\\AvCamObsPublisherLockV1" : L"Global\\AvCamPhysicalPublisherLockV1");
    if (!CreateSharedMapping(mapping, targetMapping, publisherLock,
                             redTest || obsTest)) {
        wic.Reset();
            CoUninitialize();
        return 12;
    }
    // Independent NV12 path for hosts whose active Device MFT pin type
    // is uncompressed NV12 rather than MJPEG. Diagnostic mappings remain
    // local and cannot accidentally feed a production MFT.
    Mapping nv12Mapping;
    bool nv12Enabled = false;
    if (!redTest && !obsTest) {
        nv12Enabled = CreateSharedMapping(
            nv12Mapping, AvCamPhysical::kNv12MappingName,
            L"Global\\AvCamPhysicalNv12PublisherLockV1", false,
            AvCamPhysical::kNv12TotalBytes);
        if (!nv12Enabled)
            std::puts("NV12 transport unavailable; MJPEG transport remains active.");
    }
    std::vector<std::uint8_t> nv12Pixels(
        nv12Enabled ? AvCamPhysical::kNv12PayloadBytes : 0u);
    std::vector<unsigned char> bgra(AVCAM_FRAME_BYTES_BGRA32);
    std::vector<unsigned char> jpeg;
    if (redTest) {
        // BGRA, opaque, pure red. Encode once, publish at nominal 30 FPS.
        // All data stays in our private mapping. No camera device is opened.
        for (std::size_t index = 0; index < bgra.size(); index += 4) {
            bgra[index] = 0;
            bgra[index + 1] = 0;
            bgra[index + 2] = 255;
            bgra[index + 3] = 255;
        }
        if (!AvCamUde::EncodeBgraToMjpeg(wic.Get(), bgra.data(),
                AVCAM_FRAME_WIDTH, AVCAM_FRAME_HEIGHT,
                AVCAM_FRAME_STRIDE_BGRA32, jpeg) ||
            jpeg.size() < 32 || jpeg.size() > AvCamPhysical::kCapacity ||
            jpeg[0] != 0xff || jpeg[1] != 0xd8 ||
            jpeg[jpeg.size() - 2] != 0xff || jpeg[jpeg.size() - 1] != 0xd9) {
            std::puts("AVCAM_RED_MJPEG_ENCODE_FAILED");
            wic.Reset();
            CoUninitialize();
            return 31;
        }
        for (unsigned count = 0; count < 90 && !g_stop.load(); ++count) {
            if (!Publish(mapping, jpeg, GetTickCount64() * 10000ull)) {
                std::puts("AVCAM_RED_MJPEG_PUBLISH_FAILED");
                wic.Reset();
            CoUninitialize();
                return 32;
            }
            Sleep(33);
        }
        if (g_stop.load()) {
            std::puts("AVCAM_RED_MJPEG_INTERRUPTED");
            wic.Reset();
            CoUninitialize();
            return 33;
        }
        std::printf("AVCAM_RED_MJPEG_TRANSPORT_PASS frames=90 jpeg_bytes=%zu camera_modified=0\n",
            jpeg.size());
        wic.Reset();
            CoUninitialize();
        return 0;
    }
    if (obsTest) {
        // Direct live validation: OBS Virtual Camera (including Window Capture)
        // -> NV12 -> BGRA -> JPEG -> private IPC. Does not open C270, register
        // a COM camera transform, or modify any physical capture endpoint.
        ObsSharedMemorySource source;
        if (!source.Start()) {
            std::printf("AVCAM_OBS_MJPEG_SOURCE_FAILED code=0x%08lX\n", source.LastErrorCode());
            wic.Reset();
            CoUninitialize();
            return 34;
        }
        unsigned frames = 0;
        unsigned changed = 0;
        std::uint64_t previousHash = 0;
        const ULONGLONG begin = GetTickCount64();
        while (!g_stop.load() && frames < 90 && GetTickCount64() - begin < 10000ull) {
            std::uint64_t timestamp = 0;
            if (!source.ReadFrame(bgra.data(), bgra.size(), timestamp)) break;
            // Sample B,G,R bytes from across the entire picture. The counter
            // is a motion indicator, not a proof of source identity.
            std::uint64_t hash = 1469598103934665603ull;
            for (std::size_t index = 0; index + 2 < bgra.size(); index += 4096) {
                for (unsigned channel = 0; channel < 3; ++channel) {
                    hash ^= bgra[index + channel];
                    hash *= 1099511628211ull;
                }
            }
            if (frames > 0 && previousHash != hash) ++changed;
            previousHash = hash;
            if (!AvCamUde::EncodeBgraToMjpeg(wic.Get(), bgra.data(),
                    AVCAM_FRAME_WIDTH, AVCAM_FRAME_HEIGHT,
                    AVCAM_FRAME_STRIDE_BGRA32, jpeg) ||
                jpeg.size() < 32 || jpeg.size() > AvCamPhysical::kCapacity ||
                jpeg[0] != 0xff || jpeg[1] != 0xd8 ||
                jpeg[jpeg.size() - 2] != 0xff || jpeg[jpeg.size() - 1] != 0xd9 ||
                !Publish(mapping, jpeg, timestamp)) {
                std::puts("AVCAM_OBS_MJPEG_ENCODE_OR_PUBLISH_FAILED");
                source.Stop();
                wic.Reset();
            CoUninitialize();
                return 35;
            }
            ++frames;
        }
        const ULONGLONG elapsedMs = GetTickCount64() - begin;
        source.Stop();
        if (frames < 30 || g_stop.load()) {
            std::printf("AVCAM_OBS_MJPEG_LIVE_INCOMPLETE frames=%u changed=%u elapsed_ms=%llu\n",
                frames, changed, static_cast<unsigned long long>(elapsedMs));
            wic.Reset();
            CoUninitialize();
            return 36;
        }
        const double fps = elapsedMs ?
            (static_cast<double>(frames) * 1000.0 / static_cast<double>(elapsedMs)) : 0.0;
        std::printf("AVCAM_OBS_MJPEG_LIVE_PASS frames=%u changed=%u fps=%.2f camera_modified=0\n",
            frames, changed, fps);
        wic.Reset();
            CoUninitialize();
        return 0;
    }
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
    if (waitForEnter) {
        std::puts("STAGED_START: Start Deep Live Cam and verify its physical C270 input");
        std::puts("before continuing. Press ENTER to permit OBS frame publication.");
        std::puts("Press Ctrl+C instead to leave the camera unchanged.");
        // Never enter the publishing loop until explicitly released.
        // CLI option is intended for an interactive operator console only.
        (void)getchar();
        if (g_stop.load()) {
            wic.Reset();
            CoUninitialize();
            return 0;
        }
        std::puts("STAGED_START_RELEASED: OBS publication may now begin.");
    }
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
            if (nv12Enabled) {
                if (!avcam::mfcolor::Convert(
                        bgra.data(), bgra.size(),
                        nv12Pixels.data(), nv12Pixels.size()) ||
                    !PublishNv12(nv12Mapping, nv12Pixels, timestamp)) {
                    warnOccasionally("NV12 conversion or publication failed", ERROR_INVALID_DATA);
                }
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
    wic.Reset();
            CoUninitialize();
    return 0;
}
