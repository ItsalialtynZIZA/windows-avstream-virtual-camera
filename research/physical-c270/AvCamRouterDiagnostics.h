#pragma once
#include <cstdint>
#include <limits>

namespace avcam::diagnostics {

// All timestamps use the same process-local steady_clock epoch.
// A missing timestamp and a future timestamp must never be called fresh.
constexpr std::uint64_t AgeMs(std::uint64_t now, std::uint64_t last) noexcept {
    return !last || now < last
        ? (std::numeric_limits<std::uint64_t>::max)()
        : now - last;
}

constexpr bool IsFresh(std::uint64_t now, std::uint64_t last,
                       std::uint64_t maxAge = 2000) noexcept {
    return AgeMs(now, last) <= maxAge;
}

// Tenths of FPS, rounded to nearest; avoids locale-dependent floating output.
// Zero elapsed time is not evidence of a nonzero capture rate.
constexpr std::uint64_t RateTenths(std::uint64_t frames,
                                   std::uint64_t elapsedMs) noexcept {
    if (!elapsedMs) return 0;
    constexpr std::uint64_t scale = 10000;
    if (frames > ((std::numeric_limits<std::uint64_t>::max)() - elapsedMs / 2) / scale)
        return (std::numeric_limits<std::uint64_t>::max)();
    return (frames * scale + elapsedMs / 2) / elapsedMs;
}

} // namespace avcam::diagnostics
