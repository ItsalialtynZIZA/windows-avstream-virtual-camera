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

// Device MFT predecessor topology comes from a COM interface and must be
// bounded BEFORE allocating its ID arrays. Zero upstream inputs is valid
// for a source transform; zero upstream outputs cannot form our input pins.
enum class UpstreamCountGate {
    InputOutOfRange,
    MissingOutput,
    OutputOutOfRange,
    BoundedForEnumeration
};
constexpr UpstreamCountGate CheckUpstreamPinCounts(
    std::uint32_t inputStreams, std::uint32_t outputStreams) noexcept {
    if (inputStreams > kMaxObservedStreams)
        return UpstreamCountGate::InputOutOfRange;
    if (outputStreams == 0)
        return UpstreamCountGate::MissingOutput;
    if (outputStreams > kMaxObservedStreams)
        return UpstreamCountGate::OutputOutOfRange;
    return UpstreamCountGate::BoundedForEnumeration;
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

// Adjacent MFT pins need explicit mapping evidence. The upstream output and
// downstream input may have DIFFERENT stream IDs, so matching numeric IDs
// is neither required nor sufficient. This pure check is for isolated tests:
// no real OEM identity or pin mapping can be inferred from a source descriptor.
enum class MediaSubtype {
    Unknown,
    Mjpg,
    Nv12
};
struct PinMediaEvidence {
    bool available = false;
    bool fromConnectedTransform = false;
    bool enumerationComplete = false;
    std::uint32_t streamId = 0;
    MediaSubtype subtype = MediaSubtype::Unknown;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t fpsNumerator = 0;
    std::uint32_t fpsDenominator = 0;
};
enum class AdjacentMediaGate {
    MissingEvidence,
    NotFromConnectedTransforms,
    IncompleteEnumeration,
    MappingUnverified,
    UnsupportedType,
    FrameFormatMismatch,
    FormatMatchOnly
};

// For the current MJPEG-only replacement path, the connected upstream and
// downstream pins must both expose compatible MJPG 1280x720 at exactly 30 fps.
// Numerical stream IDs need not be identical. This does NOT inspect frames,
// establish Sonix COM identity, or authorize registration on a physical camera.
constexpr AdjacentMediaGate CheckAdjacentMjpeg(
    const PinMediaEvidence& upstream,
    const PinMediaEvidence& downstream,
    bool mappingVerified) noexcept {
    if (!upstream.available || !downstream.available)
        return AdjacentMediaGate::MissingEvidence;
    if (!upstream.fromConnectedTransform || !downstream.fromConnectedTransform)
        return AdjacentMediaGate::NotFromConnectedTransforms;
    if (!upstream.enumerationComplete || !downstream.enumerationComplete)
        return AdjacentMediaGate::IncompleteEnumeration;
    if (!mappingVerified)
        return AdjacentMediaGate::MappingUnverified;
    if (upstream.subtype != MediaSubtype::Mjpg ||
        downstream.subtype != MediaSubtype::Mjpg)
        return AdjacentMediaGate::UnsupportedType;
    if (!Is720p30(upstream.width, upstream.height,
                   upstream.fpsNumerator, upstream.fpsDenominator) ||
        !Is720p30(downstream.width, downstream.height,
                   downstream.fpsNumerator, downstream.fpsDenominator))
        return AdjacentMediaGate::FrameFormatMismatch;
    return AdjacentMediaGate::FormatMatchOnly;
}
constexpr bool PermitsSonixChainInstall(AdjacentMediaGate) noexcept {
    return false;
}

// A valid numeric/rate match is only observational evidence. Installation
// always remains disabled until actual connected OEM compatibility is proven.
constexpr bool PermitsSonixChainInstall(StreamIdAudit) noexcept { return false; }
} // namespace avcam::dmft
