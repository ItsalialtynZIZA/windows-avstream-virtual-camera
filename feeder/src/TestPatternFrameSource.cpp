#include "TestPatternFrameSource.h"
#include "VirtualCameraProtocol.h"

#include <windows.h>

const wchar_t* TestPatternFrameSource::Name() const noexcept
{
    return L"moving-test-pattern";
}

bool TestPatternFrameSource::ReadFrame(
    unsigned char* Destination,
    std::size_t DestinationBytes,
    std::uint64_t& Timestamp100ns)
{
    if (Destination == nullptr || DestinationBytes < AVCAM_FRAME_BYTES_BGRA32)
    {
        return false;
    }

    Timestamp100ns = GetTickCount64() * 10000ull;

    const unsigned long movingBar =
        (frameIndex_++ * 12u) % AVCAM_FRAME_WIDTH;

    for (unsigned long y = 0; y < AVCAM_FRAME_HEIGHT; ++y)
    {
        auto* row = Destination + y * AVCAM_FRAME_STRIDE_BGRA32;

        for (unsigned long x = 0; x < AVCAM_FRAME_WIDTH; ++x)
        {
            const unsigned long offset = x * 4u;
            const unsigned char horizontal =
                static_cast<unsigned char>((x * 255u) / (AVCAM_FRAME_WIDTH - 1u));
            const unsigned char vertical =
                static_cast<unsigned char>((y * 255u) / (AVCAM_FRAME_HEIGHT - 1u));

            const unsigned long distance =
                (x > movingBar) ? (x - movingBar) : (movingBar - x);
            const bool highlight = distance < 24u;

            row[offset + 0] = highlight ? 0x20 : horizontal;
            row[offset + 1] = highlight ? 0xE0 : vertical;
            row[offset + 2] = highlight ? 0xFF : 0x40;
            row[offset + 3] = 0xFF;
        }
    }

    return true;
}
