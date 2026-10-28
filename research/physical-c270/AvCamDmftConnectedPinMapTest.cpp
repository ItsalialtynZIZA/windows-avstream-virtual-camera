// Isolated C++17 pin map + media contract simulation.
// Does not load Sonix, activate a physical camera, or register an MFT.
#include "AvCamDmftConnectedPinMap.h"
#include <array>
#include <cstdint>
#include <iostream>

using namespace avcam::dmft;

constexpr std::array<std::uint32_t, 3> upstream{{17u, 22u, 0xFFFFFFFFu}};
constexpr std::array<std::uint32_t, 3> downstream{{0u, 6u, 13u}};
constexpr std::array<PinLink, 3> mapped{{
    {17u, 6u}, {22u, 13u}, {0xFFFFFFFFu, 0u}
}};
constexpr std::array<PinLink, 3> duplicate{{
    {17u, 6u}, {17u, 13u}, {0xFFFFFFFFu, 0u}
}};
constexpr std::array<PinLink, 3> duplicateTarget{{
    {17u, 6u}, {22u, 6u}, {0xFFFFFFFFu, 0u}
}};
constexpr std::array<PinLink, 3> foreignId{{
    {17u, 6u}, {22u, 13u}, {100u, 0u}
}};
constexpr std::array<std::uint32_t, 3> duplicateIds{{17u, 17u, 22u}};
constexpr std::array<std::uint32_t, 33> oversized{};
constexpr PinMediaEvidence mockUpstream{
    true, true, true, 17u, MediaSubtype::Mjpg, 1280, 720, 30000, 1000
};
constexpr PinMediaEvidence mockDownstream{
    true, true, true, 6u, MediaSubtype::Mjpg, 1280, 720, 30, 1
};

constexpr auto valid = CheckConnectedPinMap(
    upstream.data(), upstream.size(), downstream.data(), downstream.size(),
    mapped.data(), mapped.size());
constexpr auto invalid = CheckConnectedPinMap(
    upstream.data(), upstream.size(), downstream.data(), downstream.size(),
    duplicate.data(), duplicate.size());
static_assert(valid == PinMapGate::UniqueCompleteMappingOnly);
static_assert(invalid == PinMapGate::DuplicateLinks);
static_assert(CheckConnectedPinMap(
    upstream.data(), upstream.size(), downstream.data(), downstream.size(),
    duplicateTarget.data(), duplicateTarget.size()) == PinMapGate::DuplicateLinks);
static_assert(CheckConnectedPinMap(
    upstream.data(), upstream.size(), downstream.data(), downstream.size(),
    foreignId.data(), foreignId.size()) ==
    PinMapGate::LinkReferencesUnknownStream);
static_assert(CheckConnectedPinMap(
    duplicateIds.data(), duplicateIds.size(), downstream.data(), downstream.size(),
    mapped.data(), mapped.size()) == PinMapGate::DuplicateStreamIds);
static_assert(CheckConnectedPinMap(
    upstream.data(), upstream.size(), downstream.data(), downstream.size(),
    mapped.data(), 2) == PinMapGate::IncompleteLinks);
static_assert(CheckConnectedPinMap(
    upstream.data(), upstream.size(), downstream.data(), 2,
    mapped.data(), mapped.size()) == PinMapGate::CountMismatch);
static_assert(CheckConnectedPinMap(
    oversized.data(), oversized.size(), downstream.data(), downstream.size(),
    mapped.data(), mapped.size()) == PinMapGate::CountOutOfRange);
static_assert(CheckConnectedPinMap(
    static_cast<const std::uint32_t*>(nullptr), upstream.size(),
    downstream.data(), downstream.size(),
    mapped.data(), mapped.size()) == PinMapGate::MissingEvidence);
static_assert(CheckAdjacentMjpeg(mockUpstream, mockDownstream,
    valid == PinMapGate::UniqueCompleteMappingOnly) ==
    AdjacentMediaGate::FormatMatchOnly);
static_assert(CheckAdjacentMjpeg(mockUpstream, mockDownstream,
    invalid == PinMapGate::UniqueCompleteMappingOnly) ==
    AdjacentMediaGate::MappingUnverified);
static_assert(!PermitsSonixChainInstall(valid));

int main() {
    if (valid != PinMapGate::UniqueCompleteMappingOnly ||
        invalid != PinMapGate::DuplicateLinks ||
        PermitsSonixChainInstall(valid))
        return 1;
    std::cout << "AVCAM_DMFT_MOCK_EXPLICIT_PIN_LINKS_PASS\n"
                 "AVCAM_DMFT_MOCK_NONIDENTICAL_STREAM_IDS_PASS\n"
                 "AVCAM_DMFT_MOCK_DUPLICATE_AND_UNKNOWN_LINKS_REJECTED_PASS\n"
                 "AVCAM_DMFT_MOCK_INCOMPLETE_LINKS_REJECTED_PASS\n"
                 "AVCAM_DMFT_MOCK_MEDIA_NEGOTIATION_FAIL_CLOSED_PASS\n"
                 "AVCAM_DMFT_MOCK_SONIX_INSTALL_BLOCKED_PASS\n";
    return 0;
}
