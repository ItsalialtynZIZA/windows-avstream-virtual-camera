#include "AvCamIpcFrameSource.h"
#include "../../research/physical-c270/AvCamPreviewSelection.h"
#include <chrono>
#include <cstring>

namespace {
std::uint64_t NowMs() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}
} // namespace

const wchar_t* AvCamIpcFrameSource::Name() const noexcept {
    switch (mode_) {
    case Mode::ObsOnly: return L"AvCam IPC V2 / OBS processed only";
    case Mode::RawOnly: return L"AvCam IPC V2 / physical RAW only";
    case Mode::OutputWithRawFallback:
        return L"AvCam IPC V2 / OBS processed with RAW fallback";
    }
    return L"AvCam IPC V2 / invalid mode";
}

bool AvCamIpcFrameSource::Start() {
    Stop();
    if (mode_ == Mode::ObsOnly) return obs_.Start(avcam::ipc::Channel::Obs);
    if (mode_ == Mode::RawOnly) return raw_.Start(avcam::ipc::Channel::Raw);
    // Both channels can start separately. RAW capture remains usable while
    // OBS is absent and vice versa. There is no implicit RAW leak in obs-only.
    const bool raw = raw_.Start(avcam::ipc::Channel::Raw);
    const bool obs = obs_.Start(avcam::ipc::Channel::Obs);
    return raw || obs;
}

void AvCamIpcFrameSource::Stop() noexcept {
    raw_.Stop();
    obs_.Stop();
    rawCursor_ = {};
    obsCursor_ = {};
}

bool AvCamIpcFrameSource::ReadChannel(avcam::ipc::Channel channel,
                                      unsigned char* dst,
                                      std::uint64_t& timestamp100ns) {
    auto& reader = channel == avcam::ipc::Channel::Obs ? obs_ : raw_;
    auto& cursor = channel == avcam::ipc::Channel::Obs ? obsCursor_ : rawCursor_;
    if (!reader.Attached() && !reader.Start(channel)) return false;
    if (!reader.IsActive()) return false;
    avcam::ipc::Snapshot frame;
    if (!reader.TryRead(frame, cursor)) return false;
    cursor = frame.Position();
    const auto now = NowMs();
    if (frame.bgra.size() != avcam::ipc::FrameBytes ||
        !avcam::preview::Fresh(now, frame.capturedMs)) return false;
    std::memcpy(dst, frame.bgra.data(), avcam::ipc::FrameBytes);
    // AvCam KS feeder expects a 100-nanosecond timestamp.
    timestamp100ns = frame.capturedMs * 10000ull;
    return true;
}

bool AvCamIpcFrameSource::ReadFrame(unsigned char* dst, std::size_t bytes,
                                   std::uint64_t& timestamp100ns) {
    timestamp100ns = 0;
    if (!dst || bytes < avcam::ipc::FrameBytes) return false;
    if (mode_ != Mode::RawOnly &&
        ReadChannel(avcam::ipc::Channel::Obs, dst, timestamp100ns))
        return true;
    if (mode_ == Mode::ObsOnly) return false;
    return ReadChannel(avcam::ipc::Channel::Raw, dst, timestamp100ns);
}
