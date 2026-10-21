// Read-only physical Logitech C270 + OBS application-space router diagnostic.
// No registration, driver changes, Chrome interception or virtual endpoint.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
#include "AvCamDualChannelRouter.h"
#include "../../feeder/src/MediaFoundationFrameSource.h"
#include "../../feeder/src/ObsSharedMemorySource.h"

int wmain(int argc, wchar_t** argv) {
    const bool rawOnly = argc == 2 && std::wstring(argv[1]) == L"--raw-only";
    if (argc != 1 && !rawOnly) {
        std::wcerr << L"USAGE: AvCamRouterPhysicalRawObs.exe [--raw-only]\n";
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
            std::cout << "Run OBS Virtual Camera or use --raw-only explicitly.\n";
            return 7;
        }
    }
    avcam::dual::Router router(2000);
    std::vector<unsigned char> raw(frameBytes), processed(frameBytes);
    unsigned rawFrames = 0, obsFrames = 0;
    for (std::uint64_t i = 1; i <= 30; ++i) {
        std::uint64_t timestamp = 0;
        if (!physical.ReadFrame(raw.data(), raw.size(), timestamp)) {
            std::cout << "AVCAM_PHYSICAL_READ_FAILED frame=" << i << "\n";
            return 6;
        }
        router.PushRaw({raw, i, nowMs()});
        if (obsConnected) {
            if (obs.ReadFrame(processed.data(), processed.size(), timestamp)) {
                router.PushObs({processed, i, nowMs()});
                ++obsFrames;
            } else {
                obsConnected = false;
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
    }
    router.DisconnectObs();
    const auto fallback = router.ReadForOutput(nowMs());
    if (fallback.source != avcam::dual::Source::PhysicalRaw ||
        fallback.frame.bytes != raw) {
        std::cout << "AVCAM_ROUTER_FALLBACK_FAILED\n";
        return 3;
    }
    std::cout << "AVCAM_PHYSICAL_RAW_FRAMES=" << rawFrames << "\n"
              << "AVCAM_OBS_PROCESSED_FRAMES=" << obsFrames << "\n"
              << "AVCAM_ROUTER_RAW_PROTECTED=1\n"
              << "AVCAM_ROUTER_OBS_FALLBACK_PASS\n"
              << "AVCAM_PHYSICAL_C270_ROUTER_PASS\n"
              << "AVCAM_CHROME_PHYSICAL_SUBSTITUTION=NOT_IMPLEMENTED\n";
    return 0;
}
