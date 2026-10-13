#pragma once

#include <cstddef>
#include <cstdint>

class IFrameSource
{
public:
    virtual ~IFrameSource() = default;

    virtual const wchar_t* Name() const noexcept = 0;

    // Produces one 1280x720 BGRA32 frame into Destination.
    // Timestamp100ns uses 100 ns units and may be zero when unavailable.
    virtual bool ReadFrame(
        unsigned char* Destination,
        std::size_t DestinationBytes,
        std::uint64_t& Timestamp100ns) = 0;
};
