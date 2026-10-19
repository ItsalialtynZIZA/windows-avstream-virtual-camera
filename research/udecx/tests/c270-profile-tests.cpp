#include "../AvCamUdeC270Profile.h"
#include "../AvCamUdeTestIdentity.h"
#include "../AvCamUdeUploadProtocol.h"
#include <cstddef>
#include <cstdio>

int main() {
    static_assert(AVCAM_UDE_UPLOAD_WIDTH == 1280u, "C270 output must be native 720p");
    static_assert(AVCAM_UDE_UPLOAD_HEIGHT == 720u, "C270 height must be 720");
    static_assert(AVCAM_UDE_TEST_PID == 0x5679u, "Never spoof Logitech or installed legacy identity");
    static_assert(AVCAM_UVC_FRAME_MAX_BYTES >= 1280u * 720u, "Advertised bound too small");
    unsigned char derived[sizeof(g_AvCamUsbConfigurationDescriptor)]{};
    constexpr std::size_t length = sizeof(derived);
    if (!AvCamUdeBuildC270Configuration(
            g_AvCamUsbConfigurationDescriptor, length, derived, length)) return 1;
    std::size_t changed = 0;
    bool foundFrame = false;
    for (std::size_t offset = 0; offset < length;) {
        const unsigned part = derived[offset];
        if (part < 2u || part > length - offset) return 2;
        if (part >= 30u && derived[offset + 1] == 0x24u &&
            derived[offset + 2] == 0x07u) {
            if (foundFrame) return 3;
            foundFrame = true;
            const unsigned width = derived[offset + 5] | (derived[offset + 6] << 8);
            const unsigned height = derived[offset + 7] | (derived[offset + 8] << 8);
            if (width != AVCAM_UDE_UPLOAD_WIDTH || height != AVCAM_UDE_UPLOAD_HEIGHT) return 4;
            if (g_AvCamUsbConfigurationDescriptor[offset + 5] != 0x80u ||
                g_AvCamUsbConfigurationDescriptor[offset + 6] != 0x07u) return 5;
        }
        offset += part;
    }
    if (!foundFrame) return 6;
    for (std::size_t i = 0; i < length; ++i) {
        if (derived[i] != g_AvCamUsbConfigurationDescriptor[i]) ++changed;
    }
    if (changed != 4) return 7;
    if (AvCamUdeBuildC270Configuration(
            g_AvCamUsbConfigurationDescriptor, length, derived, length - 1)) return 8;
    unsigned char invalid[sizeof(g_AvCamUsbConfigurationDescriptor)]{};
    for (std::size_t i = 0; i < length; ++i)
        invalid[i] = g_AvCamUsbConfigurationDescriptor[i];
    invalid[2] = 0;
    if (AvCamUdeBuildC270Configuration(invalid, length, derived, length)) return 9;
    std::puts("AVCAM_C270_COMPAT_720P_PROFILE_PASSED");
    return 0;
}
