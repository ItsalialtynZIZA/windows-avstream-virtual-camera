#pragma once
// Camera-independent preview selection. Never routes to a physical endpoint.
#include <cstdint>

namespace avcam::preview {
enum class Mode { Output, Compare, Raw, Obs };
enum class Source { None, Raw, Obs };

constexpr bool Fresh(std::uint64_t nowMs, std::uint64_t frameMs,
                     std::uint64_t maxAgeMs = 2000) noexcept {
    return frameMs != 0 && nowMs >= frameMs && nowMs - frameMs <= maxAgeMs;
}
constexpr Source Select(bool rawFresh, bool obsFresh, Mode mode) noexcept {
    switch (mode) {
    case Mode::Raw: return rawFresh ? Source::Raw : Source::None;
    case Mode::Obs: return obsFresh ? Source::Obs : Source::None;
    case Mode::Output: return obsFresh ? Source::Obs :
                              rawFresh ? Source::Raw : Source::None;
    case Mode::Compare: return Source::None; // Two independent panes.
    }
    return Source::None;
}
}
