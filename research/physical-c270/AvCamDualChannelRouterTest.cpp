#include "AvCamDualChannelRouter.h"
#include <iostream>
using namespace avcam::dual;
int main() {
    Router router(500);
    if (router.ReadForProducer().source != Source::None) return 1;
    if (router.ReadForOutput(100).source != Source::None) return 2;
    router.PushRaw({{1, 2, 3}, 1, 100});
    auto raw = router.ReadForProducer();
    if (raw.source != Source::PhysicalRaw || raw.frame.bytes != Bytes({1,2,3})) return 3;
    router.PushObs({{9, 8, 7}, 1, 150});
    auto processed = router.ReadForOutput(200);
    if (processed.source != Source::ObsProcessed || processed.frame.bytes != Bytes({9,8,7})) return 4;
    if (router.ReadForProducer().frame.bytes != Bytes({1,2,3})) return 5;
    // Caller modifications must not contaminate either channel.
    processed.frame.bytes[0] = 55;
    raw.frame.bytes[0] = 55;
    if (router.ReadForOutput(200).frame.bytes[0] != 9) return 6;
    if (router.ReadForProducer().frame.bytes[0] != 1) return 7;
    router.PushObs({{99}, 1, 201}); // reject out-of-order or repeated OBS seq
    if (router.ReadForOutput(200).frame.bytes[0] != 9) return 8;
    if (router.ReadForOutput(651).source != Source::PhysicalRaw) return 9;
    router.PushObs({{6, 6}, 2, 700});
    if (router.ReadForOutput(701).source != Source::ObsProcessed) return 10;
    router.DisconnectObs();
    if (router.ReadForOutput(702).source != Source::PhysicalRaw) return 11;
    if (router.ReadForProducer().frame.bytes != Bytes({1,2,3})) return 12;
    std::cout << "AVCAM_DUAL_CHANNEL_ROUTER_PASS\n";
    return 0;
}
