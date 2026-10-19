#include <iostream>
#include <stdexcept>
#include "../AvCamUdeIsoClock.h"

static void Require(bool ok, const char* reason) {
    if (!ok) throw std::runtime_error(reason);
}
static void ExplicitUsbFrameAndWrap() {
    AVCAM_UDE_ISO_CLOCK c{};
    AVCAM_UDE_ISO_CLOCK next{};
    AvCamUdeIsoClockReset(&c);
    unsigned long long time = 999;
    Require(AvCamUdeIsoClockPlan(&c, 0, 2046, 16, &time, &next) ==
        AVCAM_UDE_CLOCK_OK && time == 0, "Initial anchor failed");
    c = next;
    Require(c.ExpectedUsbFrame == 0 &&
        c.NextMicroframe100ns == 20000, "2048-frame wrap failed");
    Require(AvCamUdeIsoClockPlan(&c, 0, 0, 8, &time, &next) ==
        AVCAM_UDE_CLOCK_OK && time == 20000, "Wrapped continuous URB failed");
    c = next;
    Require(c.ExpectedUsbFrame == 1 &&
        c.NextMicroframe100ns == 30000, "Frame advance failed");
    Require(AvCamUdeIsoClockPlan(&c, 0, 4, 8, &time, &next) ==
        AVCAM_UDE_CLOCK_OK && time == 60000,
        "Three missing USB frames not reflected in stream time");
    c = next;
    Require(c.NextMicroframe100ns == 70000,
        "Host frame gap incorrectly compressed");
    const auto unchanged = c;
    Require(AvCamUdeIsoClockPlan(&c, 0, 3, 8, &time, &next) ==
        AVCAM_UDE_CLOCK_STALE, "Backward StartFrame accepted");
    Require(c.NextMicroframe100ns == unchanged.NextMicroframe100ns,
        "Rejected request must not mutate the USB clock");
}
static void AsapAndExplicitTransitions() {
    AVCAM_UDE_ISO_CLOCK clock{};
    AvCamUdeIsoClockReset(&clock);
    AVCAM_UDE_ISO_CLOCK planned{};
    unsigned long long timestamp = 9;
    for (unsigned i = 0; i < 100; ++i) {
        Require(AvCamUdeIsoClockPlan(
            &clock, 1, 0xffffffffu, 128, &timestamp, &planned) ==
            AVCAM_UDE_CLOCK_OK, "ASAP scheduling failed");
        Require(timestamp ==
            static_cast<unsigned long long>(i) * 160000ull,
            "ASAP timestamp skipped or repeated microframes");
        clock = planned;
        Require(clock.HasExplicitFrame == 0,
                "ASAP must not trust the input StartFrame");
    }
    Require(AvCamUdeIsoClockPlan(&clock, 0, 1234, 8,
        &timestamp, &planned) == AVCAM_UDE_CLOCK_OK,
        "Explicit timestamp must anchor after ASAP");
    Require(timestamp == 16000000ull && planned.HasExplicitFrame == 1,
        "Transition from ASAP lost monotonic time");
    clock = planned;
    Require(AvCamUdeIsoClockPlan(&clock, 1, 1, 8,
        &timestamp, &planned) == AVCAM_UDE_CLOCK_OK,
        "Explicit -> ASAP transition failed");
    Require(planned.HasExplicitFrame == 0,
        "Explicit StartFrame anchor retained in ASAP mode");
}
static void InputBoundsAndOverflow() {
    AVCAM_UDE_ISO_CLOCK clock{};
    AVCAM_UDE_ISO_CLOCK output{};
    unsigned long long start = 55;
    AvCamUdeIsoClockReset(&clock);
    Require(AvCamUdeIsoClockPlan(&clock, 1, 0, 7,
        &start, &output) == AVCAM_UDE_CLOCK_INVALID,
        "Seven microframe packets cannot make a high-speed USB frame");
    Require(AvCamUdeIsoClockPlan(&clock, 1, 0, 1025,
        &start, &output) == AVCAM_UDE_CLOCK_INVALID,
        "Oversize packet count accepted");
    Require(AvCamUdeIsoClockPlan(&clock, 1, 0, 0,
        &start, &output) == AVCAM_UDE_CLOCK_INVALID,
        "Empty URB accepted");
    Require(start == 55 && clock.NextMicroframe100ns == 0,
        "Invalid plan mutated caller");
    clock.NextMicroframe100ns = ~0ull - 1000ull;
    Require(AvCamUdeIsoClockPlan(&clock, 1, 0, 8,
        &start, &output) == AVCAM_UDE_CLOCK_INVALID,
        "Clock overflow accepted");
    AvCamUdeIsoClockReset(&clock);
    Require(clock.NextMicroframe100ns == 0 && !clock.HasExplicitFrame,
        "Clock reset failed");
}
int main() {
    try {
        ExplicitUsbFrameAndWrap();
        AsapAndExplicitTransitions();
        InputBoundsAndOverflow();
        std::cout << "USB StartFrame and ASAP high-speed microframe scheduling passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
