#pragma once
// Pure routing policy. This does not imply Windows supports per-app MFTs.
namespace avcam::routing {
enum class Consumer { Unknown, CaptureProducer, Browser };
enum class FrameSource { PhysicalRaw, ObsProcessed };
struct Evidence {
    bool verifiedOriginalUsbEndpoint = false;
    bool provenPerClientPipeline = false;
    bool explicitBrowserSelection = false;
    bool rawCaptureProtected = false;
    bool obsFrameFresh = false;
};
constexpr FrameSource Choose(Consumer consumer, Evidence proof) noexcept {
    if (consumer == Consumer::Browser &&
        proof.verifiedOriginalUsbEndpoint &&
        proof.provenPerClientPipeline &&
        proof.explicitBrowserSelection &&
        proof.rawCaptureProtected &&
        proof.obsFrameFresh)
        return FrameSource::ObsProcessed;
    return FrameSource::PhysicalRaw;
}
} // namespace avcam::routing
