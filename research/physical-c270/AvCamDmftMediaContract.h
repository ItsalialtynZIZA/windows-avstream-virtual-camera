// Offline, platform-independent guard for connected Device MFT stream evidence.
// A native camera format or stream ID does NOT prove an OEM Sonix->AvCam chain.
// This file performs no COM, driver, camera, or registry operations.
#pragma once
#include <cstddef>
#include <cstdint>

namespace avcam::dmft {
constexpr std::size_t kMaxObservedStreams = 32;
constexpr std::uint32_t kMaxObservedTypesPerStream = 64;

// Video frame-rate attributes are rational numbers; 30000/1000 is exactly
// 30 fps, but 30000/1001 is not. 64-bit multiplication avoids overflow.
constexpr bool IsExact30Fps(std::uint32_t numerator,
                            std::uint32_t denominator) noexcept {
    return denominator != 0 && numerator != 0 &&
           static_cast<std::uint64_t>(numerator) ==
               30ull * static_cast<std::uint64_t>(denominator);
}
constexpr bool Is720p30(std::uint32_t width, std::uint32_t height,
                        std::uint32_t numerator,
                        std::uint32_t denominator) noexcept {
    return width == 1280 && height == 720 &&
           IsExact30Fps(numerator, denominator);
}

enum class StreamIdAudit {
    Missing,
    CountOutOfRange,
    Duplicate,
    Unique
};

// The IDs are opaque numeric keys, not sequential indices; never assume
// upstream stream N has ID N. Count must be bounded before dereferencing.
template <typename T>
constexpr StreamIdAudit AuditStreamIds(const T* streamIds,
                                      std::size_t count) noexcept {
    if (!streamIds) return StreamIdAudit::Missing;
    if (count == 0 || count > kMaxObservedStreams)
        return StreamIdAudit::CountOutOfRange;
    for (std::size_t i = 0; i < count; ++i)
        for (std::size_t j = 0; j < i; ++j)
            if (streamIds[i] == streamIds[j]) return StreamIdAudit::Duplicate;
    return StreamIdAudit::Unique;
}

// A valid numeric/rate match is only observational evidence. Installation
// always remains disabled until actual connected OEM compatibility is proven.
constexpr bool PermitsSonixChainInstall(StreamIdAudit) noexcept { return false; }
} // namespace avcam::dmft
