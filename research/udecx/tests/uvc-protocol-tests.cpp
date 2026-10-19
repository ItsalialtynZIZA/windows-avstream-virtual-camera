#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "../AvCamUdeControl.h"
#include "../AvCamUdeTestIdentity.h"

static void Assert(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

static void VerifyUsbIdentityIsolation() {
    static_assert(AVCAM_USB_VID == AVCAM_UDE_TEST_VID,
        "Only the research product ID changes");
    static_assert(AVCAM_USB_PID == 0x5678u,
        "Do not mutate the installed legacy AvCamBus identity");
    static_assert(AVCAM_UDE_TEST_PID == 0x5679u,
        "The isolated UdeCx child requires its own research PID");
    static_assert(AVCAM_UDE_TEST_PID != AVCAM_USB_PID,
        "The experimental UdeCx PID must never collide with legacy AvCamBus");
    static_assert(sizeof(g_AvCamUdeIsolatedDeviceDescriptor) ==
        sizeof(g_AvCamUsbDeviceDescriptor),
        "Isolated descriptor must preserve the USB descriptor size");

    const auto* legacy = g_AvCamUsbDeviceDescriptor;
    const auto* isolated = g_AvCamUdeIsolatedDeviceDescriptor;
    for (std::size_t i = 0; i < sizeof(g_AvCamUsbDeviceDescriptor); ++i) {
        if (i != 10u && i != 11u) {
            Assert(isolated[i] == legacy[i],
                "Isolated UdeCx descriptor changed more than the PID");
        }
    }
    Assert(isolated[8] == 0x34u && isolated[9] == 0x12u,
        "Isolated UdeCx USB VID bytes are incorrect");
    Assert(isolated[10] == 0x79u && isolated[11] == 0x56u,
        "Isolated UdeCx USB PID bytes are incorrect");
    Assert(legacy[10] == 0x78u && legacy[11] == 0x56u,
        "Legacy USB PID must remain unchanged");
}

static void VerifyAlternateSettings() {
    const unsigned char* descriptor = g_AvCamUsbConfigurationDescriptor;
    constexpr std::size_t size = sizeof(g_AvCamUsbConfigurationDescriptor);
    Assert(size >= 9u && descriptor[1] == 2u, "Configuration descriptor missing");
    Assert((static_cast<unsigned>(descriptor[2]) |
        (static_cast<unsigned>(descriptor[3]) << 8u)) == size,
        "wTotalLength must exactly match descriptor bytes");

    unsigned alt0 = 0;
    unsigned alt1 = 0;
    unsigned ep0 = 0;
    unsigned ep1 = 0;
    unsigned activeAlt = 255;
    unsigned activeInterface = 255;
    for (std::size_t offset = 0; offset < size;) {
        const unsigned length = descriptor[offset];
        Assert(length >= 2u && offset + length <= size, "Descriptor length overrun");
        const unsigned type = descriptor[offset + 1u];
        if (type == 4u) {
            Assert(length >= 9u, "Invalid interface descriptor");
            activeInterface = descriptor[offset + 2u];
            activeAlt = descriptor[offset + 3u];
            if (activeInterface == 1u && activeAlt == 0u) {
                ++alt0;
                Assert(descriptor[offset + 4u] == 0u,
                    "UVC idle alternate setting must have no endpoints");
            }
            if (activeInterface == 1u && activeAlt == 1u) {
                ++alt1;
                Assert(descriptor[offset + 4u] == 1u,
                    "UVC streaming alternate setting needs one endpoint");
            }
        } else if (type == 5u && activeInterface == 1u) {
            Assert(length >= 7u, "Invalid endpoint descriptor");
            Assert(descriptor[offset + 2u] == 0x81u,
                "UVC streaming endpoint must be 0x81 IN");
            Assert((descriptor[offset + 3u] & 3u) == 1u,
                "UVC endpoint must be isochronous");
            if (activeAlt == 0u) ++ep0;
            if (activeAlt == 1u) ++ep1;
        }
        offset += length;
    }
    Assert(alt0 == 1u && alt1 == 1u && ep0 == 0u && ep1 == 1u,
        "UVC alt0/alt1 requires dynamic endpoint creation");
}

static void VerifyUvcClassControl() {
    AVCAM_UVC_STREAM_STATE state{};
    AvCamUvcInitState(&state);
    unsigned char packet[AVCAM_UVC_CONTROL_LENGTH]{};
    unsigned int written = 123u;

    int status = AvCamUdeHandleUvcInterfaceControl(
        &state, 0xA1, AVCAM_UVC_GET_CUR, 0x0100, 1,
        static_cast<unsigned short>(sizeof(packet)), packet, sizeof(packet), &written);
    Assert(status == AVCAM_UVC_RESULT_OK && written == sizeof(packet),
        "VideoStreaming GET_CUR must return Probe structure");
    Assert(packet[2] == 1u && packet[3] == 1u,
        "UVC mode indexes incorrect");

    // The same supported settings must be accepted by Probe and Commit.
    status = AvCamUdeHandleUvcInterfaceControl(
        &state, 0x21, AVCAM_UVC_SET_CUR, 0x0100, 1,
        static_cast<unsigned short>(sizeof(packet)), packet, sizeof(packet), &written);
    Assert(status == AVCAM_UVC_RESULT_OK && written == sizeof(packet),
        "Probe SET_CUR failed");
    status = AvCamUdeHandleUvcInterfaceControl(
        &state, 0x21, AVCAM_UVC_SET_CUR, 0x0200, 1,
        static_cast<unsigned short>(sizeof(packet)), packet, sizeof(packet), &written);
    Assert(status == AVCAM_UVC_RESULT_OK &&
        state.Committed == 1u, "Commit SET_CUR failed");

    packet[0] = 0;
    status = AvCamUdeHandleUvcInterfaceControl(
        &state, 0xA1, AVCAM_UVC_GET_CUR, 0x0200, 1,
        static_cast<unsigned short>(sizeof(packet)), packet, sizeof(packet), &written);
    Assert(status == AVCAM_UVC_RESULT_OK && packet[2] == 1u,
        "Committed GET_CUR must return valid mode");

    unsigned char shortPacket[2]{};
    status = AvCamUdeHandleUvcInterfaceControl(
        &state, 0xA1, AVCAM_UVC_GET_LEN, 0x0100, 1,
        2u, shortPacket, sizeof(shortPacket), &written);
    Assert(status == AVCAM_UVC_RESULT_OK && written == 2u &&
        shortPacket[0] == AVCAM_UVC_CONTROL_LENGTH && shortPacket[1] == 0,
        "GET_LEN must return the UVC control length");

    // Mismatched setup wLength vs URB capacity must fail closed.
    written = 123u;
    status = AvCamUdeHandleUvcInterfaceControl(
        &state, 0xA1, AVCAM_UVC_GET_CUR, 0x0100, 1,
        34u, shortPacket, sizeof(shortPacket), &written);
    Assert(status == AVCAM_UVC_RESULT_INVALID && written == 0u,
        "Oversized wLength must not write past the USB buffer");

    // Wrong recipient/type and invalid class selector must stall.
    status = AvCamUdeHandleUvcInterfaceControl(
        &state, 0xA0, AVCAM_UVC_GET_CUR, 0x0100, 1,
        2u, shortPacket, sizeof(shortPacket), &written);
    Assert(status == AVCAM_UVC_RESULT_STALL && written == 0u,
        "Non-interface request must not impersonate a UVC control");

    status = AvCamUdeHandleUvcInterfaceControl(
        &state, 0xA1, AVCAM_UVC_GET_CUR, 0x0100, 0,
        2u, shortPacket, sizeof(shortPacket), &written);
    Assert(status == AVCAM_UVC_RESULT_STALL && written == 0u,
        "UVC controls must target VideoStreaming interface number 1");

    status = AvCamUdeHandleUvcInterfaceControl(
        &state, 0xA1, AVCAM_UVC_GET_CUR, 0x0100, 1,
        0u, shortPacket, sizeof(shortPacket), &written);
    Assert(status == AVCAM_UVC_RESULT_INVALID && written == 0u,
        "Zero-length class control must be rejected");
}

int main() {
    try {
        VerifyUsbIdentityIsolation();
        VerifyAlternateSettings();
        VerifyUvcClassControl();
        std::cout << "UdeCx UVC descriptors and Probe/Commit contracts passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
