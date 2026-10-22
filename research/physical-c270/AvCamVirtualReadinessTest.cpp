#include "../../feeder/src/AvCamVirtualReadiness.h"
#include <iostream>
using avcam::readiness::Result;
static_assert(avcam::readiness::Evaluate(0, 0) == Result::Absent);
static_assert(avcam::readiness::Evaluate(1, 0) == Result::Absent);
static_assert(avcam::readiness::Evaluate(0, 1) == Result::Absent);
static_assert(avcam::readiness::Evaluate(1, 1) == Result::Enumerated);
static_assert(avcam::readiness::Evaluate(2, 1) == Result::Ambiguous);
static_assert(avcam::readiness::Evaluate(1, 2) == Result::Ambiguous);
static_assert(avcam::readiness::Evaluate(2, 2) == Result::Ambiguous);
static_assert(avcam::readiness::ExitCode(Result::Enumerated) == 0);
static_assert(avcam::readiness::ExitCode(Result::Absent) == 6);
static_assert(avcam::readiness::ExitCode(Result::Ambiguous) == 7);
int main() {
    std::cout << "AVCAM_VIRTUAL_ENDPOINT_READINESS_POLICY_PASS\n";
    return 0;
}
