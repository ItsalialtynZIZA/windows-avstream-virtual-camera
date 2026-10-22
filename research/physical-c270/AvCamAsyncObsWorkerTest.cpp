#include "AvCamAsyncObsWorker.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>

struct SlowObs {
    unsigned reads = 0;
    bool Start() { reads = 0; return true; }
    void Stop() noexcept {}
    bool ReadFrame(unsigned char* dst, std::size_t size, std::uint64_t& timestamp) {
        std::this_thread::sleep_for(std::chrono::milliseconds(90));
        if (++reads == 3) return false; // simulate OBS source loss
        if (size != avcam::ipc::FrameBytes) return false;
        std::fill(dst, dst + size, static_cast<unsigned char>(0xa4));
        timestamp = reads;
        return true;
    }
};

int main() {
    avcam::dual::Router router(3000);
    avcam::async_obs::Worker<SlowObs> worker(router, nullptr);
    worker.Start();

    // Publish many RAW frames while OBS is deliberately blocking ReadFrame.
    // A synchronous OBS read inside this loop would make it run much slower.
    const auto start = std::chrono::steady_clock::now();
    for (std::uint64_t seq = 1; seq <= 300; ++seq) {
        router.PushRaw({{0x11, 0x22, 0x33}, seq, seq});
        const auto producer = router.ReadForProducer();
        if (producer.source != avcam::dual::Source::PhysicalRaw ||
            producer.frame.bytes != avcam::dual::Bytes({0x11, 0x22, 0x33}) ||
            producer.frame.sequence != seq) return 1;
    }
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    if (elapsedMs > 2000) return 2;

    // One processed frame should arrive independently, without altering RAW.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (worker.Frames() == 0 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    if (worker.Frames() == 0 || !worker.Connected()) return 3;
    if (router.ReadForProducer().frame.bytes !=
        avcam::dual::Bytes({0x11, 0x22, 0x33})) return 4;

    // Fake OBS closes after two frames and reconnects after a finite backoff.
    const auto reconnectDeadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(6);
    while (worker.Reconnects() == 0 &&
           std::chrono::steady_clock::now() < reconnectDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    if (worker.Reconnects() == 0) return 5;
    worker.Stop();
    if (worker.Connected() || worker.PublishFailures() ||
        router.ReadForOutput(0).source != avcam::dual::Source::PhysicalRaw ||
        router.ReadForProducer().frame.bytes !=
            avcam::dual::Bytes({0x11, 0x22, 0x33})) return 6;
    std::cout << "AVCAM_ASYNC_OBS_RAW_INDEPENDENT_PASS\n"
              << "AVCAM_ASYNC_OBS_RECONNECT_PASS\n"
              << "AVCAM_ASYNC_OBS_CLEAN_STOP_PASS\n";
    return 0;
}
