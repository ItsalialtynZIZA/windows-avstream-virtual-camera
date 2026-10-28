// Portable, allocation-free pin-link audit for isolated Device MFT tests.
// Stream IDs are opaque: predecessor output ID need not match successor input ID.
// This does NOT inspect real Sonix, open a camera, register COM or alter registry.
#pragma once
#include "AvCamDmftMediaContract.h"
#include <cstddef>
#include <cstdint>

namespace avcam::dmft {
struct PinLink {
    std::uint32_t upstreamOutputId = 0;
    std::uint32_t downstreamInputId = 0;
};
enum class PinMapGate {
    MissingEvidence,
    CountOutOfRange,
    CountMismatch,
    DuplicateStreamIds,
    IncompleteLinks,
    DuplicateLinks,
    LinkReferencesUnknownStream,
    UniqueCompleteMappingOnly
};

constexpr bool IdInList(const std::uint32_t* ids, std::size_t count,
                        std::uint32_t id) noexcept {
    for (std::size_t i = 0; i < count; ++i)
        if (ids[i] == id) return true;
    return false;
}

// Verify a declared ONE-TO-ONE set of connections before attempting any
// synthetic MJPEG type negotiation. Verify actual connected COM pin relations
// separately: passing IDs from a camera presentation descriptor is not proof.
// Every upstream output and successor input must appear in exactly one link.
constexpr PinMapGate CheckConnectedPinMap(
    const std::uint32_t* upstreamOutputIds, std::size_t upstreamCount,
    const std::uint32_t* downstreamInputIds, std::size_t downstreamCount,
    const PinLink* links, std::size_t linkCount) noexcept {
    if (!upstreamOutputIds || !downstreamInputIds || !links)
        return PinMapGate::MissingEvidence;
    if (upstreamCount == 0 || downstreamCount == 0 ||
        upstreamCount > kMaxObservedStreams ||
        downstreamCount > kMaxObservedStreams ||
        linkCount > kMaxObservedStreams)
        return PinMapGate::CountOutOfRange;
    if (upstreamCount != downstreamCount)
        return PinMapGate::CountMismatch;
    if (AuditStreamIds(upstreamOutputIds, upstreamCount) !=
            StreamIdAudit::Unique ||
        AuditStreamIds(downstreamInputIds, downstreamCount) !=
            StreamIdAudit::Unique)
        return PinMapGate::DuplicateStreamIds;
    if (linkCount != upstreamCount)
        return PinMapGate::IncompleteLinks;

    for (std::size_t i = 0; i < linkCount; ++i) {
        if (!IdInList(upstreamOutputIds, upstreamCount,
                      links[i].upstreamOutputId) ||
            !IdInList(downstreamInputIds, downstreamCount,
                      links[i].downstreamInputId))
            return PinMapGate::LinkReferencesUnknownStream;
        for (std::size_t j = 0; j < i; ++j) {
            if (links[i].upstreamOutputId == links[j].upstreamOutputId ||
                links[i].downstreamInputId == links[j].downstreamInputId)
                return PinMapGate::DuplicateLinks;
        }
    }
    return PinMapGate::UniqueCompleteMappingOnly;
}

// A complete synthetic pin map is format-negotiation evidence only.
// It must NEVER become an installation permission for a physical OEM chain.
constexpr bool PermitsSonixChainInstall(PinMapGate) noexcept { return false; }
} // namespace avcam::dmft
