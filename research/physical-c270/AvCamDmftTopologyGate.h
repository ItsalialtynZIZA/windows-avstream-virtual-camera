// Pure, platform-independent fail-closed pin-count gate for Device MFT chaining.
// Camera SOURCE descriptors do not establish OEM Device MFT output topology.
// No code path in this header installs or registers any Device MFT.
#pragma once
#include <cstdint>

namespace avcam::dmft {
enum class EvidenceOrigin {
    Unknown,
    CameraSourcePresentationDescriptor,
    InitializedSonixDeviceTransform,
    InitializedAvCamDeviceTransform
};
struct PinCountEvidence {
    EvidenceOrigin origin = EvidenceOrigin::Unknown;
    bool initializedWithRealUpstream = false;
    bool available = false;
    std::uint32_t count = 0;
};
enum class PinCountGate {
    MissingEvidence,
    WrongEvidenceOrigin,
    NotInitialized,
    InvalidPinCount,
    CountMismatch,
    CountMatchOnly
};
constexpr PinCountGate CheckAdjacentPins(const PinCountEvidence& sonixOutputs,
                                        const PinCountEvidence& avcamInputs) {
    if (!sonixOutputs.available || !avcamInputs.available)
        return PinCountGate::MissingEvidence;
    if (sonixOutputs.origin != EvidenceOrigin::InitializedSonixDeviceTransform ||
        avcamInputs.origin != EvidenceOrigin::InitializedAvCamDeviceTransform)
        return PinCountGate::WrongEvidenceOrigin;
    if (!sonixOutputs.initializedWithRealUpstream ||
        !avcamInputs.initializedWithRealUpstream)
        return PinCountGate::NotInitialized;
    if (sonixOutputs.count == 0 || avcamInputs.count == 0 ||
        sonixOutputs.count > 32 || avcamInputs.count > 32)
        return PinCountGate::InvalidPinCount;
    if (sonixOutputs.count != avcamInputs.count)
        return PinCountGate::CountMismatch;
    return PinCountGate::CountMatchOnly;
}
// A verified adjacent pin count is necessary, but NOT sufficient to install:
// types, pin mappings, OEM state, 64-bit/32-bit registration, stability and
// rollback have separate requirements. This experimental build has no
// verified production-install mode.
constexpr bool SupportsPhysicalSonixInstallation(PinCountGate) { return false; }
} // namespace avcam::dmft
