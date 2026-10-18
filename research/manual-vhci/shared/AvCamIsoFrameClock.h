#pragma once
/*
 * Host-independent ISO microframe clock helpers. All time units are 100 ns.
 * Preserve the negotiated frame phase; never restart from an URB boundary.
 */
#define AVCAM_ISO_MICROFRAME_100NS 1250ull

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
