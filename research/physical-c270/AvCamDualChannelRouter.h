#pragma once
// AvCam Router v1: application-space frame separation only.
// This does NOT attach to the physical Logitech endpoint or Chrome.
#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

namespace avcam::dual {
using Bytes = std::vector<std::uint8_t>;
struct Frame {
    Bytes bytes;
    std::uint64_t sequence = 0;
    std::uint64_t capturedMs = 0;
};
enum class Source { None, PhysicalRaw, ObsProcessed };
struct Output { Frame frame; Source source = Source::None; };
class Router final {
public:
    explicit Router(std::uint64_t obsMaxAgeMs = 2000) : maxAge_(obsMaxAgeMs) {}

    // The producer must provide a deep copy of the real sensor frame.
    void PushRaw(Frame frame) {
        std::lock_guard<std::mutex> guard(lock_);
        if (frame.bytes.empty()) return;
        if (!raw_.bytes.empty() && frame.sequence <= raw_.sequence) return;
        raw_ = std::move(frame);
    }
    // OBS frames never enter the raw buffer.
    void PushObs(Frame frame) {
        std::lock_guard<std::mutex> guard(lock_);
        if (frame.bytes.empty()) return;
        if (!obs_.bytes.empty() && frame.sequence <= obs_.sequence) return;
        obs_ = std::move(frame);
    }
    Output ReadForProducer() const {
        std::lock_guard<std::mutex> guard(lock_);
        return {raw_, raw_.bytes.empty() ? Source::None : Source::PhysicalRaw};
    }
    Output ReadForOutput(std::uint64_t nowMs) const {
        std::lock_guard<std::mutex> guard(lock_);
        if (!obs_.bytes.empty() && nowMs >= obs_.capturedMs &&
            nowMs - obs_.capturedMs <= maxAge_)
            return {obs_, Source::ObsProcessed};
        return {raw_, raw_.bytes.empty() ? Source::None : Source::PhysicalRaw};
    }
    // Release the processed source immediately. A reconnect starts a new
    // sequence epoch, so a restarted OBS sender can begin at frame 1.
    void DisconnectObs() {
        std::lock_guard<std::mutex> guard(lock_);
        obs_ = {};
    }
private:
    mutable std::mutex lock_;
    Frame raw_;
    Frame obs_;
    std::uint64_t maxAge_;
};
} // namespace avcam::dual
