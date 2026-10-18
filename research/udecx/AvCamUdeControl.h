#pragma once
/*
 * Portable UVC control-transfer validation for the UdeCx controller.
 * This header is independent of WDF/WDK, making setup boundaries testable.
 *
 * The URB handler must provide the real UdeCx-owned transfer buffer, enforce
 * its length, and complete the request using the asynchronous UdeCx contract.
 */
#include "../manual-vhci/shared/AvCamUvcControls.h"

#define AVCAM_UDE_UVC_INTERFACE_CLASS_OUT 0x21u
#define AVCAM_UDE_UVC_INTERFACE_CLASS_IN  0xA1u

static __inline int
AvCamUdeHandleUvcInterfaceControl(
    AVCAM_UVC_STREAM_STATE* state,
    unsigned char requestType,
    unsigned char request,
    unsigned short value,
    unsigned short index,
    unsigned short setupLength,
    unsigned char* transfer,
    unsigned int transferCapacity,
    unsigned int* actualLength)
{
    if (actualLength == 0) {
        return AVCAM_UVC_RESULT_INVALID;
    }
    *actualLength = 0;

    /* UVC VideoStreaming class-specific interface requests only.
     * Standard USB enumeration/configuration stays owned by UdeCx.
     */
    if (requestType != AVCAM_UDE_UVC_INTERFACE_CLASS_IN &&
        requestType != AVCAM_UDE_UVC_INTERFACE_CLASS_OUT) {
        return AVCAM_UVC_RESULT_STALL;
    }

    if (setupLength == 0 || (unsigned int)setupLength > transferCapacity ||
        transfer == 0 || state == 0) {
        return AVCAM_UVC_RESULT_INVALID;
    }

    return AvCamUvcProcessClassInterface(
        state, request, value, index,
        (requestType & 0x80u) != 0u,
        transfer, (unsigned int)setupLength, actualLength);
}
