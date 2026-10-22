#include "AvCamRouterDiagnostics.h"
#include <cstdint>
#include <iostream>
#include <limits>

int main() {
    using avcam::diagnostics::AgeMs;
    using avcam::diagnostics::IsFresh;
    using avcam::diagnostics::RateTenths;
    constexpr auto unknown = (std::numeric_limits<std::uint64_t>::max)();
    static_assert(AgeMs(200, 0) == unknown, "missing OBS sample");
    static_assert(AgeMs(100, 101) == unknown, "future timestamp");
    static_assert(AgeMs(3000, 1000) == 2000, "freshness boundary");
    static_assert(IsFresh(3000, 1000), "inclusive window");
    static_assert(!IsFresh(3001, 1000), "expired frame");
    static_assert(!IsFresh(100, 0), "no sample must be stale");
    static_assert(!IsFresh(100, 101), "future sample must be stale");
    static_assert(RateTenths(151, 5000) == 302, "30.2 FPS");
    static_assert(RateTenths(151, 0) == 0, "no duration");
    static_assert(RateTenths(0, 5000) == 0, "zero frames");
    static_assert(RateTenths(302, 10000) == 302, "30.2 FPS interval");
    static_assert(RateTenths(3, 2000) == 15, "1.5 FPS");
    static_assert(RateTenths(unknown, 1) == unknown, "no overflow");
    std::cout << "AVCAM_ROUTER_DIAGNOSTICS_PASS\n";
    return 0;
}
