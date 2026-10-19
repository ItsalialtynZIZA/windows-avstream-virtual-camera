#pragma once
/*
 * Isolated PnP identity for the experimental UdeCx USB child.
 *
 * Never modify research/manual-vhci/shared/AvCamUsbDescriptors.h here:
 * the installed legacy AvCamBus uses VID_1234&PID_5678. The UdeCx child
 * must enumerate with a different test PID so existing PnP ghost devices,
 * INF associations and diagnostics cannot be confused with the new stack.
 *
 * VID_1234 is a legacy research placeholder, not a USB-IF-assigned
 * production identity. This file is FOR LOCAL TESTING ONLY.
 */
#include "../manual-vhci/shared/AvCamUsbDescriptors.h"

#define AVCAM_UDE_TEST_VID AVCAM_USB_VID
#define AVCAM_UDE_TEST_PID 0x5679u

/* Exactly one full-speed/high-speed USB device descriptor (18 bytes).
 * All class/config/endpoint descriptors are retained unchanged from the
 * existing shared model. Only the product ID is isolated here.
 */
static const unsigned char g_AvCamUdeIsolatedDeviceDescriptor[] = {
    0x12, 0x01, 0x00, 0x02,
    0xEF, 0x02, 0x01, 0x40,
    (unsigned char)(AVCAM_UDE_TEST_VID & 0xffu),
    (unsigned char)((AVCAM_UDE_TEST_VID >> 8) & 0xffu),
    (unsigned char)(AVCAM_UDE_TEST_PID & 0xffu),
    (unsigned char)((AVCAM_UDE_TEST_PID >> 8) & 0xffu),
    0x00, 0x01,
    0x01, 0x02, 0x03,
    0x01
};
