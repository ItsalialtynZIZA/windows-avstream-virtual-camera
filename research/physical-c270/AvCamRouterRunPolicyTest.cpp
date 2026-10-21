#include "AvCamRouterRunPolicy.h"
#include <iostream>
int main() {
    using avcam::run::ShouldRead;
    if (ShouldRead(0, false, 0) || ShouldRead(0, true, 0)) return 1;
    if (!ShouldRead(1, false, 0) || !ShouldRead(30, false, 0) ||
        ShouldRead(31, false, 0)) return 2;
    if (!ShouldRead(1, true, 300) || !ShouldRead(300, true, 300) ||
        ShouldRead(301, true, 300)) return 3;
    if (!ShouldRead(100000, true, 0)) return 4;
    std::cout << "AVCAM_ROUTER_BOUNDED_RUN_POLICY_PASS\n";
    return 0;
}
