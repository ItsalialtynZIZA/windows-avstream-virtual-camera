#include "AvCamPerClientRoutingPolicy.h"
#include <iostream>
using namespace avcam::routing;
int main() {
    Evidence all{true, true, true, true, true, PipelineScope::VerifiedPerClient};
    if (Choose(Consumer::CaptureProducer, all) != FrameSource::PhysicalRaw) return 1;
    if (Choose(Consumer::Unknown, all) != FrameSource::PhysicalRaw) return 2;
    if (Choose(Consumer::Browser, all) != FrameSource::ObsProcessed) return 3;
    // A fully populated boolean proof must NOT authorize OBS routing from
    // the device-scoped MFT, per-pin MFT0, or an unidentified pipeline.
    for (auto scope : {PipelineScope::Unknown, PipelineScope::SharedDevice,
                       PipelineScope::PerCameraPin}) {
        Evidence unscoped = all;
        unscoped.scope = scope;
        if (Choose(Consumer::Browser, unscoped) != FrameSource::PhysicalRaw) return 10;
    }
    for (unsigned i = 0; i < 5; ++i) {
        Evidence incomplete = all;
        switch (i) {
            case 0: incomplete.verifiedOriginalUsbEndpoint = false; break;
            case 1: incomplete.provenPerClientPipeline = false; break;
            case 2: incomplete.explicitBrowserSelection = false; break;
            case 3: incomplete.rawCaptureProtected = false; break;
            default: incomplete.obsFrameFresh = false; break;
        }
        if (Choose(Consumer::Browser, incomplete) != FrameSource::PhysicalRaw) return 4 + i;
    }
    if (Choose(Consumer::Browser, {}) != FrameSource::PhysicalRaw) return 9;
    std::cout << "AVCAM_PER_CLIENT_ROUTING_POLICY_PASS\n";
    return 0;
}
