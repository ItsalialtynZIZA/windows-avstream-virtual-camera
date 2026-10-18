#include <iostream>
#include <stdexcept>
#include <vector>
#include "../AvCamUdeIsoUrbLayout.h"

static void Expect(bool actual, const char* message) {
    if (!actual) throw std::runtime_error(message);
}

static void ValidateWellFormedUrbs() {
    const unsigned prefix = 80;
    const unsigned descBytes = 12;
    const unsigned packetCount = 128;
    const unsigned transferBytes = 128 * 3072;
    std::vector<unsigned> offsets(packetCount);
    for (unsigned i = 0; i < packetCount; ++i) offsets[i] = i * 3072;

    Expect(AvCamUdeIsoUrbHeaderValid(
        prefix + packetCount * descBytes,
        prefix, descBytes, packetCount, transferBytes,
        transferBytes, 1) == AVCAM_UDE_ISO_URB_VALID,
        "Full 128 microframe URB must validate");

    for (unsigned i = 0; i < packetCount; ++i) {
        const unsigned next = (i + 1 < packetCount) ?
            offsets[i + 1] : transferBytes;
        Expect(AvCamUdeIsoUrbOffsetValid(
            offsets[i], next, transferBytes) == AVCAM_UDE_ISO_URB_VALID,
            "Expected legal 3072-byte high-speed packet windows");
    }
}

static void RejectInvalidUrbs() {
    const unsigned prefix = 80;
    const unsigned element = 12;
    const unsigned total = prefix + 128 * element;
    const unsigned transfer = 128 * 3072;

    auto validate = [&](unsigned header, unsigned count, unsigned bytes,
                        unsigned mapped, int in) {
        return AvCamUdeIsoUrbHeaderValid(
            header, prefix, element, count, bytes, mapped, in);
    };

    Expect(validate(total, 128, transfer, transfer, 0) ==
            AVCAM_UDE_ISO_URB_INVALID, "OUT isochronous URB must fail");
    Expect(validate(total, 0, transfer, transfer, 1) ==
            AVCAM_UDE_ISO_URB_INVALID, "Empty descriptor array must fail");
    Expect(validate(total, AVCAM_UDE_ISO_MAX_PACKETS + 1u,
                   transfer, transfer, 1) ==
            AVCAM_UDE_ISO_URB_INVALID, "Packet count overflow must fail");
    Expect(validate(prefix - 1u, 1, 3072, 3072, 1) ==
            AVCAM_UDE_ISO_URB_INVALID, "Too-short header must fail");
    Expect(validate(total - 1u, 128, transfer, transfer, 1) ==
            AVCAM_UDE_ISO_URB_INVALID, "Truncated packet descriptor must fail");
    Expect(validate(total, 128, transfer + 1u, transfer, 1) ==
            AVCAM_UDE_ISO_URB_INVALID, "Unmapped bytes must fail");
    Expect(validate(total, 128, 0u, transfer, 1) ==
            AVCAM_UDE_ISO_URB_INVALID, "Zero-byte ISO URB must fail");
    Expect(validate(total, 128, transfer, transfer, 1) ==
            AVCAM_UDE_ISO_URB_VALID, "Valid URB header must pass");
    Expect(AvCamUdeIsoUrbHeaderValid(
            total, prefix, 0u, 128, transfer, transfer, 1) ==
            AVCAM_UDE_ISO_URB_INVALID, "Zero-size descriptor must fail");
    Expect(AvCamUdeIsoUrbOffsetValid(0, 0, 3072) ==
            AVCAM_UDE_ISO_URB_INVALID, "Duplicate offsets must fail");
    Expect(AvCamUdeIsoUrbOffsetValid(3072, 2048, transfer) ==
            AVCAM_UDE_ISO_URB_INVALID, "Out-of-order offsets must fail");
    Expect(AvCamUdeIsoUrbOffsetValid(0, 3073, transfer) ==
            AVCAM_UDE_ISO_URB_INVALID, "Oversize microframe must fail");
    Expect(AvCamUdeIsoUrbOffsetValid(transfer - 2048,
              transfer + 1u, transfer) ==
            AVCAM_UDE_ISO_URB_INVALID, "Out-of-bounds ISO extent must fail");
}

int main() {
    try {
        ValidateWellFormedUrbs();
        RejectInvalidUrbs();
        std::cout << "Native ISO URB metadata preflight tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
