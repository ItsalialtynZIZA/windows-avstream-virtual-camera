// OBS-backed dual-channel router integration smoke test. Never opens or alters
// the physical Logitech camera; synthetic RAW data proves separation only.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>
#include "AvCamDualChannelRouter.h"
#include "../../feeder/src/ObsSharedMemorySource.h"

int main(int argc, char** argv) {
    if (argc != 2 || std::string(argv[1]) != "--obs") {
        std::cerr << "USAGE: AvCamRouterObsSmoke.exe --obs\n";
        return 9;
    }
    constexpr std::size_t frameBytes = 1280u * 720u * 4u;
    avcam::dual::Router router(2000);
    const auto nowMs = [] {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    };
    avcam::dual::Frame raw{{0x11,0x22,0x33,0x44},1,nowMs()};
    router.PushRaw(raw);
    ObsSharedMemorySource obs;
    if (!obs.Start()) {
        std::cerr << "AVCAM_ROUTER_OBS_CONNECT_FAILED code=" << obs.LastErrorCode() << "\n";
        return 8;
    }
    std::vector<unsigned char> bytes(frameBytes);
    unsigned received = 0;
    for (unsigned frame = 1; frame <= 30; ++frame) {
        std::uint64_t sourceTimestamp = 0;
        if (!obs.ReadFrame(bytes.data(), frameBytes, sourceTimestamp)) {
            std::cerr << "AVCAM_ROUTER_OBS_READ_FAILED code=" << obs.LastErrorCode() << "\n";
            return 7;
        }
        // Copy the frame into an independent OBS buffer; never into RAW.
        router.PushObs({bytes,frame,nowMs()});
        const auto producer = router.ReadForProducer();
        const auto output = router.ReadForOutput(nowMs());
        if (producer.source != avcam::dual::Source::PhysicalRaw ||
            producer.frame.bytes != raw.bytes ||
            output.source != avcam::dual::Source::ObsProcessed ||
            output.frame.bytes != bytes) {
            std::cerr << "AVCAM_ROUTER_CHANNEL_ISOLATION_FAILED\n";
            return 6;
        }
        ++received;
    }
    router.DisconnectObs();
    const auto fallback = router.ReadForOutput(nowMs());
    if (fallback.source != avcam::dual::Source::PhysicalRaw ||
        fallback.frame.bytes != raw.bytes) {
        std::cerr << "AVCAM_ROUTER_RAW_FALLBACK_FAILED\n";
        return 5;
    }
    std::cout << "AVCAM_ROUTER_OBS_FRAMES=" << received << "\n"
              << "AVCAM_ROUTER_RAW_PROTECTED=1\n"
              << "AVCAM_ROUTER_OBS_DISCONNECT_FALLBACK=1\n"
              << "AVCAM_ROUTER_OBS_INTEGRATION_PASS\n"
              << "AVCAM_ROUTER_PHYSICAL_CAMERA_CONNECTED=0\n";
    return 0;
}
