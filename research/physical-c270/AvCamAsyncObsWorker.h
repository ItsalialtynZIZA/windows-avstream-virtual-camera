#pragma once
// Optional OBS capture worker: slow OBS reads must never delay RAW acquisition.
// No driver registration, Chrome changes or camera device hooks.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>
#include "AvCamDualChannelRouter.h"
#include "AvCamRouterIpc.h"
#include "../../feeder/src/ObsSharedMemorySource.h"

namespace avcam::async_obs {

// Source is injected for deterministic tests. Production uses the existing
// OBS shared-memory source. Its Start/ReadFrame/Stop run only on this thread.
template <class Source>
class Worker final {
public:
    Worker(avcam::dual::Router& router, avcam::ipc::Publisher* publisher)
        : router_(router), publisher_(publisher) {}
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;
    ~Worker() { Stop(); }

    void Start() {
        if (thread_.joinable()) return;
        stop_.store(false);
        thread_ = std::thread([this] { Run(); });
    }

    void Stop() noexcept {
        stop_.store(true);
        if (thread_.joinable()) thread_.join();
        connected_.store(false);
        lastFrameMs_.store(0);
    }

    bool Connected() const noexcept { return connected_.load(); }
    std::uint64_t Frames() const noexcept { return frames_.load(); }
    std::uint64_t Reconnects() const noexcept { return reconnects_.load(); }
    std::uint64_t PublishFailures() const noexcept { return failures_.load(); }
    // Time of the most recently completed OBS frame in steady-clock milliseconds.
    // Zero means disconnected, stopped, or no frame has arrived yet.
    std::uint64_t LastFrameMs() const noexcept { return lastFrameMs_.load(); }

private:
    static std::uint64_t NowMs() noexcept {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    void Backoff() noexcept {
        // Interruptible so a disconnected OBS does not slow normal shutdown.
        for (int n = 0; n < 20 && !stop_.load(); ++n)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    void Run() {
        Source source;
        std::vector<unsigned char> frame(avcam::ipc::FrameBytes);
        bool everConnected = false;
        std::uint64_t sourceSequence = 0;
        while (!stop_.load()) {
            if (!connected_.load()) {
                if (!source.Start()) {
                    Backoff();
                    continue;
                }
                // A restarted OBS source can reset its own frame sequence.
                sourceSequence = 0;
                if (everConnected) ++reconnects_;
                everConnected = true;
                connected_.store(true);
            }

            std::uint64_t sourceTimestamp = 0;
            if (!source.ReadFrame(frame.data(), frame.size(), sourceTimestamp)) {
                connected_.store(false);
                lastFrameMs_.store(0);
                source.Stop();
                router_.DisconnectObs();
                if (publisher_) publisher_->Invalidate();
                Backoff();
                continue;
            }
            // The worker is the only OBS writer. RAW has a separate publisher
            // and remains unaffected even if this source blocks or disappears.
            const auto when = NowMs();
            router_.PushObs({frame, ++sourceSequence, when});
            const auto frameNumber = ++frames_;
            if (publisher_ && !publisher_->Publish(
                    frame.data(), frame.size(), frameNumber, when)) {
                ++failures_;
                break;
            }
            lastFrameMs_.store(when);
        }

        connected_.store(false);
        lastFrameMs_.store(0);
        source.Stop();
        router_.DisconnectObs();
        if (publisher_) publisher_->Invalidate();
    }

    avcam::dual::Router& router_;
    avcam::ipc::Publisher* publisher_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> connected_{false};
    std::atomic<std::uint64_t> frames_{0};
    std::atomic<std::uint64_t> reconnects_{0};
    std::atomic<std::uint64_t> lastFrameMs_{0};
    std::atomic<std::uint64_t> failures_{0};
    std::thread thread_;
};

using ObsWorker = Worker<ObsSharedMemorySource>;
} // namespace avcam::async_obs
