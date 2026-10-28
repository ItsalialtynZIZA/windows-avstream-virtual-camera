// Pure C++17 regression checks: never opens a camera or modifies registry.
#include "AvCamDmftMediaContract.h"
#include <array>
#include <iostream>
#include <cstdint>

using namespace avcam::dmft;

constexpr std::array<std::uint32_t, 3> distinct{{0u, 27u, 0xFFFFFFFFu}};
constexpr std::array<std::uint32_t, 3> duplicate{{0u, 27u, 0u}};
constexpr std::array<std::uint32_t, 33> tooMany{};

static_assert(IsExact30Fps(30, 1), "30/1 should match");
static_assert(IsExact30Fps(30000, 1000), "equivalent rational should match");
static_assert(IsExact30Fps(60000, 2000), "equivalent rational should match");
static_assert(!IsExact30Fps(30000, 1001), "29.97 fps should not match");
static_assert(!IsExact30Fps(0, 1), "zero fps should not match");
static_assert(!IsExact30Fps(30, 0), "zero denominator should not match");
static_assert(!IsExact30Fps(0xFFFFFFFFu, 0xFFFFFFFFu), "overflow guard");
static_assert(Is720p30(1280, 720, 30000, 1000), "required format");
static_assert(!Is720p30(1920, 1080, 30, 1), "wrong resolution");
static_assert(AuditStreamIds(distinct.data(), distinct.size()) ==
              StreamIdAudit::Unique, "IDs need not be sequential");
static_assert(AuditStreamIds(duplicate.data(), duplicate.size()) ==
              StreamIdAudit::Duplicate, "duplicate pin ID");
static_assert(AuditStreamIds(distinct.data(), 0) ==
              StreamIdAudit::CountOutOfRange, "zero pins");
static_assert(AuditStreamIds(tooMany.data(), tooMany.size()) ==
              StreamIdAudit::CountOutOfRange, "overlarge pin count");
static_assert(AuditStreamIds(static_cast<const std::uint32_t*>(nullptr), 1) ==
              StreamIdAudit::Missing, "null pin list");
static_assert(!PermitsSonixChainInstall(StreamIdAudit::Unique),
              "valid stream IDs never authorize OEM MFT installation");

// Synthetic adjacent MFT evidence: IDs intentionally differ. This is
// NOT evidence from the Lenovo Sonix camera or a running OEM chain.
constexpr PinMediaEvidence upstream{
    true, true, true, 17, MediaSubtype::Mjpg, 1280, 720, 30000, 1000
};
constexpr PinMediaEvidence downstream{
    true, true, true, 44, MediaSubtype::Mjpg, 1280, 720, 30, 1
};
constexpr PinMediaEvidence missing{
    false, true, true, 17, MediaSubtype::Mjpg, 1280, 720, 30, 1
};
constexpr PinMediaEvidence sourceOnly{
    true, false, true, 17, MediaSubtype::Mjpg, 1280, 720, 30, 1
};
constexpr PinMediaEvidence truncated{
    true, true, false, 17, MediaSubtype::Mjpg, 1280, 720, 30, 1
};
constexpr PinMediaEvidence nv12{
    true, true, true, 17, MediaSubtype::Nv12, 1280, 720, 30, 1
};
constexpr PinMediaEvidence fractionalFps{
    true, true, true, 17, MediaSubtype::Mjpg, 1280, 720, 30000, 1001
};
constexpr PinMediaEvidence wrongSize{
    true, true, true, 17, MediaSubtype::Mjpg, 1920, 1080, 30, 1
};
static_assert(CheckAdjacentMjpeg(upstream, downstream, true) ==
              AdjacentMediaGate::FormatMatchOnly);
static_assert(CheckAdjacentMjpeg(missing, downstream, true) ==
              AdjacentMediaGate::MissingEvidence);
static_assert(CheckAdjacentMjpeg(sourceOnly, downstream, true) ==
              AdjacentMediaGate::NotFromConnectedTransforms);
static_assert(CheckAdjacentMjpeg(truncated, downstream, true) ==
              AdjacentMediaGate::IncompleteEnumeration);
static_assert(CheckAdjacentMjpeg(upstream, downstream, false) ==
              AdjacentMediaGate::MappingUnverified);
static_assert(CheckAdjacentMjpeg(nv12, downstream, true) ==
              AdjacentMediaGate::UnsupportedType);
static_assert(CheckAdjacentMjpeg(fractionalFps, downstream, true) ==
              AdjacentMediaGate::FrameFormatMismatch);
static_assert(CheckAdjacentMjpeg(wrongSize, downstream, true) ==
              AdjacentMediaGate::FrameFormatMismatch);
static_assert(!PermitsSonixChainInstall(AdjacentMediaGate::FormatMatchOnly));

int main() {
    if (!IsExact30Fps(30000, 1000) ||
        IsExact30Fps(30000, 1001) ||
        AuditStreamIds(distinct.data(), distinct.size()) != StreamIdAudit::Unique ||
        AuditStreamIds(duplicate.data(), duplicate.size()) != StreamIdAudit::Duplicate ||
        PermitsSonixChainInstall(StreamIdAudit::Unique)) return 1;

    std::cout << "AVCAM_DMFT_ADJACENT_MJPEG_FORMAT_MATCH_SIMULATION_PASS\n"
                 "AVCAM_DMFT_ADJACENT_PIN_MAPPING_REQUIRED_PASS\n"
                 "AVCAM_DMFT_ADJACENT_OEM_UNVERIFIED_INSTALL_BLOCKED_PASS\n"
                 "AVCAM_DMFT_EXACT_30_FPS_RATIONAL_PASS\n"
                 "AVCAM_DMFT_DISTINCT_STREAM_IDS_PASS\n"
                 "AVCAM_DMFT_DUPLICATE_STREAM_IDS_REJECTED_PASS\n"
                 "AVCAM_DMFT_STREAM_COUNT_BOUND_PASS\n"
                 "AVCAM_DMFT_OEM_CHAIN_INSTALL_BLOCKED_PASS\n";
    return 0;
}
