#include "AvCamDmftTopologyGate.h"
#include <iostream>

using avcam::dmft::PinCountEvidence;
using avcam::dmft::EvidenceOrigin;
using avcam::dmft::PinCountGate;
using avcam::dmft::CheckAdjacentPins;
using avcam::dmft::SupportsPhysicalSonixInstallation;

constexpr PinCountEvidence sonix{
    EvidenceOrigin::InitializedSonixDeviceTransform, true, true, 1};
constexpr PinCountEvidence avcam{
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

static_assert(CheckAdjacentPins(sonix, avcam) == PinCountGate::CountMatchOnly);
static_assert(CheckAdjacentPins(sourceOnly, avcam) == PinCountGate::WrongEvidenceOrigin);
static_assert(CheckAdjacentPins(absent, avcam) == PinCountGate::MissingEvidence);
static_assert(CheckAdjacentPins(uninitialized, avcam) == PinCountGate::NotInitialized);
static_assert(CheckAdjacentPins(zero, avcam) == PinCountGate::InvalidPinCount);
static_assert(CheckAdjacentPins(large, avcam) == PinCountGate::InvalidPinCount);
static_assert(CheckAdjacentPins(two, avcam) == PinCountGate::CountMismatch);
static_assert(!SupportsPhysicalSonixInstallation(PinCountGate::CountMatchOnly));

int main() {
    if (CheckAdjacentPins(sonix, avcam) != PinCountGate::CountMatchOnly)
        return 1;
    if (CheckAdjacentPins(sourceOnly, avcam) == PinCountGate::CountMatchOnly)
        return 2;
    std::cout << "AVCAM_DMFT_TOPOLOGY_SOURCE_IS_NOT_OEM_PINS_PASS\n";
    std::cout << "AVCAM_DMFT_TOPOLOGY_UNKNOWN_EVIDENCE_FAIL_CLOSED_PASS\n";
    std::cout << "AVCAM_DMFT_TOPOLOGY_MISMATCH_FAIL_CLOSED_PASS\n";
    std::cout << "AVCAM_DMFT_TOPOLOGY_INSTALL_REMAINS_BLOCKED_PASS\n";
    return 0;
}
