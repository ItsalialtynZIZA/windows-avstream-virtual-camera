#pragma once
/*
 * Portable UVC 1.10 MJPEG payload packetizer, independent of the Windows WDK.
 * This is a byte-stream encoder; USB URB scheduling and buffer ownership
 * remain the responsibility of the driver.
 */
#include "AvCamUvcControls.h"

#define AVCAM_UVC_PAYLOAD_HEADER_BYTES 2u
#define AVCAM_UVC_PAYLOAD_EOH 0x80u
#define AVCAM_UVC_PAYLOAD_FID 0x01u
#define AVCAM_UVC_PAYLOAD_EOF 0x02u

#define AVCAM_UVC_PACKET_OK 0
#define AVCAM_UVC_PACKET_INVALID 1

/*
 * Spread MJPEG bytes across the negotiated frame interval instead of
 * exhausting a complete frame in the first few USB microframes.
 *
 * Each packet still contains a two-byte UVC header. The USB host's
 * existing ISO offsets and 3072-byte packet windows remain unchanged.
 * This is a target packet size, not a change to endpoint descriptors.
 */
static __inline unsigned int AvCamUvcPacedPacketCapacity(
    unsigned int frameBytes,
    unsigned int physicalCapacity,
    unsigned int frameInterval100ns,
    unsigned int microframeInterval100ns
    )
{
    unsigned int microframes;
    unsigned int payloadBytes;

    if (frameBytes == 0 || physicalCapacity <= AVCAM_UVC_PAYLOAD_HEADER_BYTES ||
        frameInterval100ns == 0 || microframeInterval100ns == 0) {
        return 0;
    }
    if (physicalCapacity > AVCAM_UVC_PAYLOAD_MAX_BYTES) {
        physicalCapacity = AVCAM_UVC_PAYLOAD_MAX_BYTES;
    }

    /* Use ceiling division, avoiding unsigned multiplication overflow. */
    microframes = frameInterval100ns / microframeInterval100ns;
    if (frameInterval100ns % microframeInterval100ns != 0) {
        ++microframes;
    }
    if (microframes == 0) {
        return physicalCapacity;
    }

    payloadBytes = frameBytes / microframes;
    if (frameBytes % microframes != 0) {
        ++payloadBytes;
    }
    if (payloadBytes > physicalCapacity - AVCAM_UVC_PAYLOAD_HEADER_BYTES) {
        payloadBytes = physicalCapacity - AVCAM_UVC_PAYLOAD_HEADER_BYTES;
    }

    return payloadBytes + AVCAM_UVC_PAYLOAD_HEADER_BYTES;
}


typedef struct _AVCAM_UVC_PACKETIZER {
    unsigned int Offset;
    unsigned int FramesCompleted;
    unsigned char FrameId;
} AVCAM_UVC_PACKETIZER;

static __inline void AvCamUvcPacketizerReset(AVCAM_UVC_PACKETIZER* state)
{
    if (state == 0) {
        return;
    }
    state->Offset = 0;
    state->FramesCompleted = 0;
    state->FrameId = 0;
}

/*
 * Produces exactly one USB payload packet for an MJPEG frame.
 * On success, 2 <= *actualLength <= packetCapacity <= 3072.
 * The EOF bit is set only on the final packet, FID flips only after EOF.
 * Callers must keep the input frame immutable until EOF.
 */
static __inline int AvCamUvcPacketizeMjpeg(
    AVCAM_UVC_PACKETIZER* state,
    const unsigned char* frame,
    unsigned int frameBytes,
    unsigned char* packet,
    unsigned int packetCapacity,
    unsigned int* actualLength
    )
{
    unsigned int dataAvailable;
    unsigned int dataLength;
    unsigned int i;
    unsigned char flags;

    if (actualLength == 0) {
        return AVCAM_UVC_PACKET_INVALID;
    }
    *actualLength = 0;
    if (state == 0 || frame == 0 || packet == 0 ||
        frameBytes == 0 || frameBytes > AVCAM_UVC_FRAME_MAX_BYTES ||
        state->Offset >= frameBytes ||
        packetCapacity <= AVCAM_UVC_PAYLOAD_HEADER_BYTES ||
        packetCapacity > AVCAM_UVC_PAYLOAD_MAX_BYTES) {
        return AVCAM_UVC_PACKET_INVALID;
    }

    dataAvailable = frameBytes - state->Offset;
    dataLength = packetCapacity - AVCAM_UVC_PAYLOAD_HEADER_BYTES;
    if (dataLength > dataAvailable) {
        dataLength = dataAvailable;
    }

    packet[0] = (unsigned char)AVCAM_UVC_PAYLOAD_HEADER_BYTES;
    flags = (unsigned char)(AVCAM_UVC_PAYLOAD_EOH |
                             (state->FrameId & AVCAM_UVC_PAYLOAD_FID));
    if (dataLength == dataAvailable) {
        flags = (unsigned char)(flags | AVCAM_UVC_PAYLOAD_EOF);
    }
    packet[1] = flags;
    for (i = 0; i < dataLength; ++i) {
        packet[AVCAM_UVC_PAYLOAD_HEADER_BYTES + i] = frame[state->Offset + i];
    }
    *actualLength = dataLength + AVCAM_UVC_PAYLOAD_HEADER_BYTES;
    state->Offset += dataLength;
    if (state->Offset == frameBytes) {
        state->Offset = 0;
        state->FrameId ^= AVCAM_UVC_PAYLOAD_FID;
        ++state->FramesCompleted;
    }
    return AVCAM_UVC_PACKET_OK;
}
