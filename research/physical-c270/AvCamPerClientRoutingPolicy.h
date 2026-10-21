#pragma once
// Pure routing policy, NOT a Windows camera interception mechanism.
// A per-device Device MFT or a per-pin MFT0 is not evidence of a
// distinct Chrome consumer. No caller may infer client isolation from
// an OBS IPC connection, format, process name, or FrameServer activity.
namespace avcam::routing {
enum class Consumer { Unknown, CaptureProducer, Browser };
enum class FrameSource { PhysicalRaw, ObsProcessed };
enum class PipelineScope { Unknown, SharedDevice, PerCameraPin, VerifiedPerClient };
struct Evidence {
    bool verifiedOriginalUsbEndpoint = false;
    // Retained to avoid silently treating existing test inputs as proof.
    bool provenPerClientPipeline = false;
    bool explicitBrowserSelection = false;
    bool rawCaptureProtected = false;
    bool obsFrameFresh = false;
    PipelineScope scope = PipelineScope::Unknown;
};
constexpr FrameSource Choose(Consumer consumer, Evidence proof) noexcept {
    if (consumer == Consumer::Browser &&
        proof.scope == PipelineScope::VerifiedPerClient &&
        proof.verifiedOriginalUsbEndpoint &&
        proof.provenPerClientPipeline &&
        proof.explicitBrowserSelection &&
        proof.rawCaptureProtected &&
        proof.obsFrameFresh)
        return FrameSource::ObsProcessed;
    return FrameSource::PhysicalRaw;
}
} // namespace avcam::routing
