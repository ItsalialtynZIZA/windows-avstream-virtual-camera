#pragma once
// Reads only application-space AvCam Router IPC V2 mappings.
// Does not open the original Logitech camera or OBS's DirectShow filter.
#include "FrameSource.h"
#include "../../research/physical-c270/AvCamRouterIpc.h"
#include <cstddef>
#include <cstdint>

class AvCamIpcFrameSource final : public IFrameSource {
public:
    enum class Mode { ObsOnly, OutputWithRawFallback, RawOnly };
    explicit AvCamIpcFrameSource(Mode mode) noexcept : mode_(mode) {}
    const wchar_t* Name() const noexcept override;
    bool Start() override;
    void Stop() noexcept override;
    bool ReadFrame(unsigned char* dst, std::size_t bytes,
                   std::uint64_t& timestamp100ns) override;

private:
    bool ReadChannel(avcam::ipc::Channel channel, unsigned char* dst,
                     std::uint64_t& timestamp100ns);

    Mode mode_;
    avcam::ipc::Subscriber raw_;
    avcam::ipc::Subscriber obs_;
    avcam::ipc::Cursor rawCursor_{};
    avcam::ipc::Cursor obsCursor_{};
    std::uint64_t lastObsFrameMs_ = 0;
};
