#include <iostream>
#include <stdexcept>
#include "../AvCamUdeIsoUrbLayout.h"

static void Check(bool ok, const char* explanation) {
    if (!ok) throw std::runtime_error(explanation);
}

static void TestSparseCompletion() {
    const unsigned offsets[4] = {0u, 3072u, 6144u, 9216u};
    const unsigned actual[4] = {20u, 0u, 50u, 7u};
    const unsigned zero[4] = {0u, 0u, 0u, 0u};
    Check(AvCamUdeIsoCompletionValid(
        offsets, actual, 4u, 12288u, 77u) ==
        AVCAM_UDE_ISO_URB_VALID,
        "Sparse packet byte sum was rejected");
    Check(AvCamUdeIsoCompletionValid(
        offsets, zero, 4u, 12288u, 0u) ==
        AVCAM_UDE_ISO_URB_VALID,
        "Empty scheduled microframes should transfer zero bytes");
    Check(AvCamUdeIsoCompletionValid(
        offsets, actual, 4u, 12288u, 12288u) ==
        AVCAM_UDE_ISO_URB_INVALID,
        "Requested byte capacity is not the actual transfer count");
    Check(AvCamUdeIsoCompletionValid(
        offsets, actual, 4u, 12288u, 76u) ==
        AVCAM_UDE_ISO_URB_INVALID,
        "Underreported completion byte sum accepted");
    Check(AvCamUdeIsoCompletionValid(
        offsets, actual, 4u, 12288u, 78u) ==
        AVCAM_UDE_ISO_URB_INVALID,
        "Overreported completion byte sum accepted");
    Check(AvCamUdeIsoCompletionValid(
        offsets, actual, 4u, 12288u, 12289u) ==
        AVCAM_UDE_ISO_URB_INVALID,
        "Reported bytes can never exceed mapped transfer size");
}

static void TestMalformedCompletion() {
    const unsigned offsets[4] = {0u, 3072u, 6144u, 9216u};
    const unsigned overflowLength[4] = {20u, 0u, 3073u, 7u};
    const unsigned overlapping[4] = {0u, 3072u, 3000u, 9216u};
    const unsigned validLengths[4] = {20u, 0u, 50u, 7u};
    Check(AvCamUdeIsoCompletionValid(
        offsets, overflowLength, 4u, 12288u, 3100u) ==
        AVCAM_UDE_ISO_URB_INVALID,
        "Payload length exceeded host's individual ISO packet window");
    Check(AvCamUdeIsoCompletionValid(
        overlapping, validLengths, 4u, 12288u, 77u) ==
        AVCAM_UDE_ISO_URB_INVALID,
        "Overlapping host packet offsets accepted");
    Check(AvCamUdeIsoCompletionValid(
        nullptr, validLengths, 4u, 12288u, 77u) ==
        AVCAM_UDE_ISO_URB_INVALID,
        "Null offsets were accepted");
    Check(AvCamUdeIsoCompletionValid(
        offsets, nullptr, 4u, 12288u, 77u) ==
        AVCAM_UDE_ISO_URB_INVALID,
        "Null lengths were accepted");
    Check(AvCamUdeIsoCompletionValid(
        offsets, validLengths, 0u, 12288u, 77u) ==
        AVCAM_UDE_ISO_URB_INVALID,
        "Empty URB packet array accepted");
    Check(AvCamUdeIsoCompletionValid(
        offsets, validLengths, AVCAM_UDE_ISO_MAX_PACKETS + 1u,
        12288u, 77u) == AVCAM_UDE_ISO_URB_INVALID,
        "Unbounded packet descriptor count accepted");
}

int main() {
    try {
        TestSparseCompletion();
        TestMalformedCompletion();
        std::cout << "Sparse native ISO completion byte counts validated\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
