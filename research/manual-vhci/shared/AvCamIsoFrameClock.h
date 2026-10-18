#pragma once
/*
 * Host-independent ISO microframe clock helpers. All time units are 100 ns.
 * Preserve the negotiated frame phase; never restart from an URB boundary.
 */
#define AVCAM_ISO_MICROFRAME_100NS 1250ull
#define AVCAM_ISO_USB_FRAME_100NS 10000ull
#define AVCAM_ISO_USB_FRAME_MODULUS 2048ull

/* Report the USB frame at the beginning of an ASAP isochronous URB.
 * USB frame numbers count milliseconds and wrap after 2048 frames. */
static __inline unsigned long AvCamIsoUsbFrameNumber(
    unsigned long long interruptTime100ns
    )
{
    return (unsigned long)(
        (interruptTime100ns / AVCAM_ISO_USB_FRAME_100NS) %
        AVCAM_ISO_USB_FRAME_MODULUS);
}

static __inline unsigned long long AvCamIsoAdvanceFrameDue(
    unsigned long long previousDue100ns,
    unsigned long long emittedAt100ns,
    unsigned long long interval100ns
    )
{
    if (interval100ns == 0ull) {
        return emittedAt100ns;
    }
    if (previousDue100ns == 0ull || emittedAt100ns < previousDue100ns) {
        return emittedAt100ns + interval100ns;
    }
    return previousDue100ns +
        ((emittedAt100ns - previousDue100ns) / interval100ns + 1ull) *
        interval100ns;
}
