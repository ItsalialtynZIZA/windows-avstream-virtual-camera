#pragma once

#include "FrameSource.h"

class TestPatternFrameSource final : public IFrameSource
{
public:
    const wchar_t* Name() const noexcept override;

    bool ReadFrame(
        unsigned char* Destination,
        std::size_t DestinationBytes,
        std::uint64_t& Timestamp100ns) override;

private:
    unsigned long frameIndex_ = 0;
};
