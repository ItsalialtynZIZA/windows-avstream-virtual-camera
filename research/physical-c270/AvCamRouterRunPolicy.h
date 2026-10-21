#pragma once
#include <cstdint>

namespace avcam::run {
// One-based sequence; limit 0 means unbounded only in continuous mode.
constexpr bool ShouldRead(std::uint64_t nextFrame, bool continuous,
                          std::uint64_t limit) noexcept {
    if (nextFrame == 0) return false;
    if (!continuous) return nextFrame <= 30;
    return limit == 0 || nextFrame <= limit;
}
}
