#pragma once
/*
 * High-speed USB 2.0 isochronous URB ordering model, bInterval = 1.
 *
 * The USB StartFrame field counts 1 ms frames; IsoPacket entries count
 * 125 us microframes. Eight ISO packets therefore occupy one USB frame.
 * This model never treats StartFrame as a microframe number.
 *
 * An explicit StartFrame anchors frame ordering on the low 11 bits of the
 * USB frame counter (modulo 2048). An ASAP URB has no meaningful input
 * StartFrame until completion, so it follows the local monotonic timeline
 * and invalidates the explicit-frame anchor. The emulator cannot establish
 * whether an ASAP URB has actually been scheduled late without observing
 * the USB stack's live host clock; this requires isolated VM validation.
 *
 * Pure, non-thread-safe helper: plan into a COPY, commit only after the URB
 * was successfully encoded and sealed. On purge/reset/cancel reset the clock.
 */
#include <limits.h>

#define AVCAM_UDE_CLOCK_OK 0
#define AVCAM_UDE_CLOCK_INVALID 1
#define AVCAM_UDE_CLOCK_STALE 2

#define AVCAM_UDE_CLOCK_USB_FRAME_MASK 2047u
#define AVCAM_UDE_CLOCK_MICROFRAMES_PER_FRAME 8u
#define AVCAM_UDE_CLOCK_MICROFRAME_100NS 1250ull
#define AVCAM_UDE_CLOCK_USB_FRAME_100NS 10000ull
#define AVCAM_UDE_CLOCK_MAX_PACKETS 1024u

typedef struct _AVCAM_UDE_ISO_CLOCK {
    unsigned long long NextMicroframe100ns;
    unsigned int ExpectedUsbFrame;
    unsigned char HasExplicitFrame;
} AVCAM_UDE_ISO_CLOCK;

static __inline void
AvCamUdeIsoClockReset(AVCAM_UDE_ISO_CLOCK* clock)
{
    if (clock == 0) return;
    clock->NextMicroframe100ns = 0;
    clock->ExpectedUsbFrame = 0;
    clock->HasExplicitFrame = 0;
}

/* Caller supplies TRUE for USBD_START_ISO_TRANSFER_ASAP, FALSE for an
 * explicit StartFrame. startFrame is ignored in ASAP mode.
 *
 * Only a forward window of up to 1024 USB frames can be distinguished
 * from stale/backward frames after applying the 11-bit wrap. The tighter
 * runtime scheduling deadline belongs to the Windows USB stack.
 */
static __inline int
AvCamUdeIsoClockPlan(
    const AVCAM_UDE_ISO_CLOCK* current,
    int asap,
    unsigned int startFrame,
    unsigned int packets,
    unsigned long long* firstMicroframe100ns,
    AVCAM_UDE_ISO_CLOCK* next)
{
    AVCAM_UDE_ISO_CLOCK candidate;
    unsigned long long start;
    unsigned long long duration;
    if (current == 0 || next == 0 || firstMicroframe100ns == 0 ||
        packets == 0 || packets > AVCAM_UDE_CLOCK_MAX_PACKETS ||
        packets % AVCAM_UDE_CLOCK_MICROFRAMES_PER_FRAME != 0) {
        return AVCAM_UDE_CLOCK_INVALID;
    }

    candidate = *current;
    start = current->NextMicroframe100ns;
    duration = (unsigned long long)packets *
        AVCAM_UDE_CLOCK_MICROFRAME_100NS;

    if (asap) {
        candidate.HasExplicitFrame = 0;
    } else {
        const unsigned int normalized =
            startFrame & AVCAM_UDE_CLOCK_USB_FRAME_MASK;
        if (current->HasExplicitFrame) {
            const unsigned int delta = (normalized -
                current->ExpectedUsbFrame) &
                AVCAM_UDE_CLOCK_USB_FRAME_MASK;
            if (delta > 1024u) {
                return AVCAM_UDE_CLOCK_STALE;
            }
            if (start > ULLONG_MAX -
                (unsigned long long)delta *
                    AVCAM_UDE_CLOCK_USB_FRAME_100NS) {
                return AVCAM_UDE_CLOCK_INVALID;
            }
            start += (unsigned long long)delta *
                AVCAM_UDE_CLOCK_USB_FRAME_100NS;
        }
        candidate.HasExplicitFrame = 1;
        candidate.ExpectedUsbFrame =
            (normalized + packets /
                AVCAM_UDE_CLOCK_MICROFRAMES_PER_FRAME) &
            AVCAM_UDE_CLOCK_USB_FRAME_MASK;
    }
    if (start > ULLONG_MAX - duration) {
        return AVCAM_UDE_CLOCK_INVALID;
    }
    candidate.NextMicroframe100ns = start + duration;
    *firstMicroframe100ns = start;
    *next = candidate;
    return AVCAM_UDE_CLOCK_OK;
}
