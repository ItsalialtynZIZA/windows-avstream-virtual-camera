#include "AvCamDmftTopologyGate.h"
#include <iostream>

using avcam::dmft::PinCountEvidence;
using avcam::dmft::EvidenceOrigin;
using avcam::dmft::PinCountGate;
using avcam::dmft::CheckAdjacentPins;
using avcam::dmft::SupportsPhysicalSonixInstallation;

constexpr PinCountEvidence sonix{
    EvidenceOrigin::InitializedSonixDeviceTransform, true, true, 1};
constexpr PinCountEvidence avcamInput{
    EvidenceOrigin::InitializedAvCamDeviceTransform, true, true, 1};
constexpr PinCountEvidence sourceOnly{
    EvidenceOrigin::CameraSourcePresentationDescriptor, true, true, 1};
constexpr PinCountEvidence absent{
    EvidenceOrigin::Unknown, false, false, 0};
constexpr PinCountEvidence uninitialized{
    EvidenceOrigin::InitializedSonixDeviceTransform, false, true, 1};
constexpr PinCountEvidence zero{
    EvidenceOrigin::InitializedSonixDeviceTransform, true, true, 0};
constexpr PinCountEvidence large{
    EvidenceOrigin::InitializedSonixDeviceTransform, true, true, 33};
constexpr PinCountEvidence two{
    EvidenceOrigin::InitializedSonixDeviceTransform, true, true, 2};

static_assert(CheckAdjacentPins(sonix, avcamInput) == PinCountGate::CountMatchOnly);
static_assert(CheckAdjacentPins(sourceOnly, avcamInput) == PinCountGate::WrongEvidenceOrigin);
static_assert(CheckAdjacentPins(absent, avcamInput) == PinCountGate::MissingEvidence);
static_assert(CheckAdjacentPins(uninitialized, avcamInput) == PinCountGate::NotInitialized);
static_assert(CheckAdjacentPins(zero, avcamInput) == PinCountGate::InvalidPinCount);
static_assert(CheckAdjacentPins(large, avcamInput) == PinCountGate::InvalidPinCount);
static_assert(CheckAdjacentPins(two, avcamInput) == PinCountGate::CountMismatch);
static_assert(!SupportsPhysicalSonixInstallation(PinCountGate::CountMatchOnly));

int main() {
    if (CheckAdjacentPins(sonix, avcamInput) != PinCountGate::CountMatchOnly)
        return 1;
    if (CheckAdjacentPins(sourceOnly, avcamInput) == PinCountGate::CountMatchOnly)
        return 2;
    std::cout << "AVCAM_DMFT_TOPOLOGY_SOURCE_IS_NOT_OEM_PINS_PASS\n";
    std::cout << "AVCAM_DMFT_TOPOLOGY_UNKNOWN_EVIDENCE_FAIL_CLOSED_PASS\n";
    std::cout << "AVCAM_DMFT_TOPOLOGY_MISMATCH_FAIL_CLOSED_PASS\n";
    std::cout << "AVCAM_DMFT_TOPOLOGY_INSTALL_REMAINS_BLOCKED_PASS\n";
    return 0;
}
