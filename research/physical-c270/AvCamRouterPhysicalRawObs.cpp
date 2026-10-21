// Read-only physical Logitech C270 + OBS application-space router diagnostic.
// No registration, driver changes, Chrome interception or virtual endpoint.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <chrono>
#include <limits>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
#include "AvCamDualChannelRouter.h"
#include "../../feeder/src/MediaFoundationFrameSource.h"
#include "../../feeder/src/ObsSharedMemorySource.h"

int wmain(int argc, wchar_t** argv) {
    bool rawOnly = false;
    bool continuous = false;
    std::uint64_t maxFrames = 0;
    for (int a = 1; a < argc; ++a) {
        const std::wstring arg(argv[a]);
        if (arg == L"--raw-only" && !rawOnly) rawOnly = true;
        else if (arg == L"--continuous" && !continuous) continuous = true;
        else if (arg == L"--frames" && a + 1 < argc && maxFrames == 0) {
            const std::wstring value(argv[++a]);
            try {
                std::size_t consumed = 0;
                maxFrames = std::stoull(value, &consumed);
                if (consumed != value.size() || maxFrames == 0 || maxFrames > 10000000ULL)
                    throw std::invalid_argument("invalid frame count");
            } catch (...) {
                std::wcerr << L"AVCAM_INVALID_FRAME_COUNT\n";
                return 9;
            }
        }
        else {
            std::wcerr << L"USAGE: AvCamRouterPhysicalRawObs.exe [--raw-only] [--continuous]\n";
            return 9;
        }
    }
    if (maxFrames && !continuous) {
        std::wcerr << L"AVCAM_FRAMES_REQUIRES_CONTINUOUS\n";
        return 9;
    }
    constexpr std::size_t frameBytes = 1280u * 720u * 4u;
    const auto nowMs = [] {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    };
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com)) {
        std::cerr << "AVCAM_COM_INIT_FAILED\n";
        return 10;
    }
    struct Cleanup { ~Cleanup() { CoUninitialize(); } } cleanup;
    MediaFoundationFrameSource physical(L"Logi C270", L"VID_046D&PID_0825");
    if (!physical.Start()) {
        std::cout << "AVCAM_PHYSICAL_RAW_SOURCE_FAILED\n";
        return 8;
    }
    ObsSharedMemorySource obs;
    bool obsConnected = false;
    if (!rawOnly) {
        obsConnected = obs.Start();
        if (!obsConnected) {
            std::cout << "AVCAM_OBS_SOURCE_FAILED code="
                      << obs.LastErrorCode() << "\n";
            if (!continuous) {
                std::cout << "Run OBS Virtual Camera or use --raw-only explicitly.\n";
                return 7;
            }
            std::cout << "AVCAM_OBS_WAITING_RAW_FALLBACK\n";
        }
    }
    avcam::dual::Router router(2000);
    std::vector<unsigned char> raw(frameBytes), processed(frameBytes);
    std::uint64_t rawFrames = 0, obsFrames = 0, reconnects = 0;
    std::uint64_t lastReconnectAttemptMs = 0;
    std::uint64_t lastStatusMs = nowMs();
    std::uint64_t obsSequence = 0;
    std::cout << "AVCAM_ROUTER_MODE=" << (continuous ? "CONTINUOUS" : "SMOKE") << "\n";
    for (std::uint64_t i = 1; (continuous && (maxFrames == 0 || i <= maxFrames)) || (!continuous && i <= 30); ++i) {
        if (continuous && !rawOnly && !obsConnected &&
            (nowMs() - lastReconnectAttemptMs >= 2000)) {
            lastReconnectAttemptMs = nowMs();
            if (obs.Start()) {
                obsConnected = true;
                obsSequence = 0;
                ++reconnects;
                std::cout << "AVCAM_OBS_RECONNECTED count=" << reconnects << "\n";
            }
        }
        std::uint64_t timestamp = 0;
        if (!physical.ReadFrame(raw.data(), raw.size(), timestamp)) {
            std::cout << "AVCAM_PHYSICAL_READ_FAILED frame=" << i << "\n";
            return 6;
        }
        router.PushRaw({raw, i, nowMs()});
        if (obsConnected) {
            if (obs.ReadFrame(processed.data(), processed.size(), timestamp)) {
                router.PushObs({processed, ++obsSequence, nowMs()});
                ++obsFrames;
            } else {
                obsConnected = false;
                obs.Stop();
                router.DisconnectObs();
                std::cout << "AVCAM_OBS_DISCONNECTED_RAW_FALLBACK frame="
                          << i << " code=" << obs.LastErrorCode() << "\n";
            }
        }
        const auto producer = router.ReadForProducer();
        const auto output = router.ReadForOutput(nowMs());
        if (producer.source != avcam::dual::Source::PhysicalRaw ||
            producer.frame.bytes != raw ||
            (obsConnected &&
                (output.source != avcam::dual::Source::ObsProcessed ||
                 output.frame.bytes != processed)) ||
            (!obsConnected &&
                (output.source != avcam::dual::Source::PhysicalRaw ||
                 output.frame.bytes != raw))) {
            std::cout << "AVCAM_ROUTER_ISOLATION_FAILED\n";
            return 4;
        }
        ++rawFrames;
        if (continuous && nowMs() - lastStatusMs >= 5000) {
            lastStatusMs = nowMs();
            std::cout << "AVCAM_ROUTER_STATUS raw=" << rawFrames
                      << " obs=" << obsFrames
                      << " obs_connected=" << (obsConnected ? 1 : 0)
                      << " reconnects=" << reconnects << "\n";
        }
    }
    router.DisconnectObs();
    const auto fallback = router.ReadForOutput(nowMs());
    if (fallback.source != avcam::dual::Source::PhysicalRaw ||
        fallback.frame.bytes != raw) {
        std::cout << "AVCAM_ROUTER_FALLBACK_FAILED\n";
        return 3;
    }
    // RAW fallback is correct runtime behavior, but dual-source acceptance
    // must not pass unless all expected OBS frames were actually received.
    if (!continuous && !rawOnly && obsFrames != rawFrames) {
        std::cout << "AVCAM_ROUTER_OBS_INCOMPLETE frames="
                  << obsFrames << " expected=" << rawFrames << "\n";
        return 5;
    }
    if (continuous) {
        obs.Stop();
        physical.Stop();
        std::cout << "AVCAM_ROUTER_CLEAN_SHUTDOWN=1\n";
    }
    std::cout << "AVCAM_PHYSICAL_RAW_FRAMES=" << rawFrames << "\n"
              << "AVCAM_OBS_PROCESSED_FRAMES=" << obsFrames << "\n"
              << "AVCAM_ROUTER_RAW_PROTECTED=1\n"
              << "AVCAM_ROUTER_OBS_FALLBACK_PASS\n"
              << "AVCAM_PHYSICAL_C270_ROUTER_PASS\n"
              << "AVCAM_CHROME_PHYSICAL_SUBSTITUTION=NOT_IMPLEMENTED\n";
    return 0;
}
