// Camera-free integration test for the real AVStream feeder's IPC source.
#include "../../feeder/src/AvCamIpcFrameSource.h"
#include "AvCamRouterIpc.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
std::uint64_t NowMs() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}
bool All(const std::vector<unsigned char>& frame, unsigned char pixel) {
    return frame.front() == pixel &&
           frame[frame.size() / 2] == pixel && frame.back() == pixel;
}
}

int main() {
    using avcam::ipc::Channel;
    using avcam::ipc::FrameBytes;
    avcam::ipc::Publisher raw, obs;
    if (!raw.Start(Channel::Raw) || !obs.Start(Channel::Obs)) return 1;
    AvCamIpcFrameSource strict(AvCamIpcFrameSource::Mode::ObsOnly);
    AvCamIpcFrameSource output(AvCamIpcFrameSource::Mode::OutputWithRawFallback);
    AvCamIpcFrameSource original(AvCamIpcFrameSource::Mode::RawOnly);
    if (!strict.Start() || !output.Start() || !original.Start()) return 2;

    std::vector<unsigned char> rawPixels(FrameBytes, 0x17);
    std::vector<unsigned char> obsPixels(FrameBytes, 0xB4);
    std::vector<unsigned char> received(FrameBytes, 0);
    std::uint64_t stamp = 0;
    const auto time1 = NowMs();
    if (!raw.Publish(rawPixels.data(), rawPixels.size(), 1, time1) ||
        !obs.Publish(obsPixels.data(), obsPixels.size(), 1, time1)) return 3;

    if (!strict.ReadFrame(received.data(), received.size(), stamp) ||
        !All(received, 0xB4) || stamp != time1 * 10000ull) return 4;
    if (!output.ReadFrame(received.data(), received.size(), stamp) ||
        !All(received, 0xB4)) return 5;
    if (!original.ReadFrame(received.data(), received.size(), stamp) ||
        !All(received, 0x17)) return 6;

    // Critical invariant: faster feeder polling must NOT cause a single RAW
    // frame between valid OBS frames, even though RAW is already published.
    if (output.ReadFrame(received.data(), received.size(), stamp) ||
        strict.ReadFrame(received.data(), received.size(), stamp)) return 7;

    // Disconnect OBS: strict OBS reader cannot leak raw; output falls back.
    obs.Invalidate();
    if (strict.ReadFrame(received.data(), received.size(), stamp) ||
        !output.ReadFrame(received.data(), received.size(), stamp) ||
        !All(received, 0x17)) return 8;
    if (output.ReadFrame(received.data(), received.size(), stamp)) return 9;

    // The same retained subscriber must follow the writer's new session,
    // even when its sequence counter resets to one.
    obs.Stop();
    avcam::ipc::Publisher replacement;
    if (!replacement.Start(Channel::Obs)) return 11;
    std::fill(obsPixels.begin(), obsPixels.end(), 0xC7);
    const auto time2 = NowMs();
    if (!replacement.Publish(obsPixels.data(), obsPixels.size(), 1, time2) ||
        !strict.ReadFrame(received.data(), received.size(), stamp) ||
        !All(received, 0xC7) ||
        !output.ReadFrame(received.data(), received.size(), stamp) ||
        !All(received, 0xC7)) return 12;

    // An active but non-refreshing processed writer is not considered fresh.
    // A new RAW sample should become visible in output fallback mode.
    std::fill(rawPixels.begin(), rawPixels.end(), 0x35);
    if (!replacement.Publish(obsPixels.data(), obsPixels.size(), 2, NowMs() - 3000) ||
        !raw.Publish(rawPixels.data(), rawPixels.size(), 2, NowMs())) return 13;
    if (!output.ReadFrame(received.data(), received.size(), stamp) ||
        !All(received, 0x35)) return 14;
    if (strict.ReadFrame(received.data(), received.size(), stamp)) return 15;
    if (strict.ReadFrame(nullptr, FrameBytes, stamp) ||
        strict.ReadFrame(received.data(), 17, stamp)) return 16;

    strict.Stop();
    output.Stop();
    original.Stop();
    std::cout << "AVCAM_FEEDER_IPC_OBS_ONLY_ISOLATION_PASS\n"
              << "AVCAM_FEEDER_IPC_OUTPUT_FALLBACK_PASS\n"
              << "AVCAM_FEEDER_IPC_NO_INTERFRAME_RAW_FLASH_PASS\n"
              << "AVCAM_FEEDER_IPC_SESSION_RESTART_PASS\n";
    return 0;
}
