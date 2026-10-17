#pragma once
/*
 * Portable UVC 1.10 VideoStreaming Probe/Commit protocol model.
 * The device exposes one MJPEG format (index 1), one frame (index 1),
 * and one 333333 x 100ns frame interval.
 * No USB stack types are used so this can be unit-tested on the host.
 */
#include "AvCamUsbDescriptors.h"

#define AVCAM_UVC_CONTROL_LENGTH 34u
#define AVCAM_UVC_FRAME_MAX_BYTES 2068480u
#define AVCAM_UVC_PAYLOAD_MAX_BYTES 3072u

#define AVCAM_UVC_SET_CUR 0x01u
#define AVCAM_UVC_GET_CUR 0x81u
#define AVCAM_UVC_GET_MIN 0x82u
#define AVCAM_UVC_GET_MAX 0x83u
#define AVCAM_UVC_GET_RES 0x84u
#define AVCAM_UVC_GET_LEN 0x85u
#define AVCAM_UVC_GET_INFO 0x86u
#define AVCAM_UVC_GET_DEF 0x87u

#define AVCAM_UVC_VS_PROBE_CONTROL 0x01u
#define AVCAM_UVC_VS_COMMIT_CONTROL 0x02u

#define AVCAM_UVC_RESULT_OK 0
#define AVCAM_UVC_RESULT_STALL 1
#define AVCAM_UVC_RESULT_INVALID 2

typedef struct _AVCAM_UVC_STREAM_STATE {
    unsigned char Probe[AVCAM_UVC_CONTROL_LENGTH];
    unsigned char Commit[AVCAM_UVC_CONTROL_LENGTH];
    unsigned char Committed;
} AVCAM_UVC_STREAM_STATE;

static __inline void AvCamUvcWriteLe32(unsigned char* destination, unsigned long value)
{
    destination[0] = (unsigned char)value;
    destination[1] = (unsigned char)(value >> 8);
    destination[2] = (unsigned char)(value >> 16);
    destination[3] = (unsigned char)(value >> 24);
}

static __inline unsigned long AvCamUvcReadLe32(const unsigned char* bytes)
{
    return ((unsigned long)bytes[0]) |
        ((unsigned long)bytes[1] << 8) |
        ((unsigned long)bytes[2] << 16) |
        ((unsigned long)bytes[3] << 24);
}

static __inline void AvCamUvcBuildDefault(unsigned char* control)
{
    unsigned int i;
    for (i = 0; i < AVCAM_UVC_CONTROL_LENGTH; ++i) {
        control[i] = 0;
    }
    control[2] = 1; /* MJPEG format index */
    control[3] = 1; /* 1920x1080 frame index */
    AvCamUvcWriteLe32(control + 4, AVCAM_UVC_FRAME_INTERVAL_100NS);
    AvCamUvcWriteLe32(control + 18, AVCAM_UVC_FRAME_MAX_BYTES);
    AvCamUvcWriteLe32(control + 22, AVCAM_UVC_PAYLOAD_MAX_BYTES);
    AvCamUvcWriteLe32(control + 26, 48000000u);
}

static __inline void AvCamUvcInitState(AVCAM_UVC_STREAM_STATE* state)
{
    unsigned int i;
    AvCamUvcBuildDefault(state->Probe);
    for (i = 0; i < AVCAM_UVC_CONTROL_LENGTH; ++i) {
        state->Commit[i] = state->Probe[i];
    }
    state->Committed = 0;
}

/*
 * In-place USB class control handler.
 * OUT SET_CUR reads the caller's data. IN requests write a response.
 * bufferCapacity is the USB transfer length; *actualLength is the returned
 * length (also for successful OUT). Unknown selectors/requests stall.
 */
static __inline int AvCamUvcProcessClassInterface(
    AVCAM_UVC_STREAM_STATE* state,
    unsigned char request,
    unsigned short value,
    unsigned short index,
    int directionIn,
    unsigned char* buffer,
    unsigned int bufferCapacity,
    unsigned int* actualLength
    )
{
    const unsigned char selector = (unsigned char)(value >> 8);
    unsigned char defaults[AVCAM_UVC_CONTROL_LENGTH];
    const unsigned char* response = 0;
    unsigned int responseSize = 0;
    unsigned int i;
    unsigned char scalar[2];

    if (state == 0 || actualLength == 0) {
        return AVCAM_UVC_RESULT_INVALID;
    }
    *actualLength = 0;
    if ((index & 0x00FFu) != 1 || (index & 0xFF00u) != 0 ||
        (selector != AVCAM_UVC_VS_PROBE_CONTROL &&
         selector != AVCAM_UVC_VS_COMMIT_CONTROL)) {
        return AVCAM_UVC_RESULT_STALL;
    }
    if (buffer == 0) {
        return AVCAM_UVC_RESULT_INVALID;
    }

    if (request == AVCAM_UVC_SET_CUR) {
        if (directionIn || bufferCapacity != AVCAM_UVC_CONTROL_LENGTH) {
            return AVCAM_UVC_RESULT_INVALID;
        }
        if (buffer[2] != 1 || buffer[3] != 1 ||
            AvCamUvcReadLe32(buffer + 4) != AVCAM_UVC_FRAME_INTERVAL_100NS) {
            return AVCAM_UVC_RESULT_STALL;
        }
        /* Only the one advertised mode is accepted. */
        if (selector == AVCAM_UVC_VS_COMMIT_CONTROL) {
            for (i = 0; i < AVCAM_UVC_CONTROL_LENGTH; ++i) {
                state->Commit[i] = buffer[i];
            }
            AvCamUvcWriteLe32(state->Commit + 18, AVCAM_UVC_FRAME_MAX_BYTES);
            AvCamUvcWriteLe32(state->Commit + 22, AVCAM_UVC_PAYLOAD_MAX_BYTES);
            state->Committed = 1;
        } else {
            for (i = 0; i < AVCAM_UVC_CONTROL_LENGTH; ++i) {
                state->Probe[i] = buffer[i];
            }
            AvCamUvcWriteLe32(state->Probe + 18, AVCAM_UVC_FRAME_MAX_BYTES);
            AvCamUvcWriteLe32(state->Probe + 22, AVCAM_UVC_PAYLOAD_MAX_BYTES);
        }
        *actualLength = AVCAM_UVC_CONTROL_LENGTH;
        return AVCAM_UVC_RESULT_OK;
    }

    if (!directionIn) {
        return AVCAM_UVC_RESULT_INVALID;
    }
    switch (request) {
    case AVCAM_UVC_GET_LEN:
        scalar[0] = (unsigned char)AVCAM_UVC_CONTROL_LENGTH;
        scalar[1] = 0;
        response = scalar;
        responseSize = 2;
        break;
    case AVCAM_UVC_GET_INFO:
        scalar[0] = 3; /* supports GET and SET */
        response = scalar;
        responseSize = 1;
        break;
    case AVCAM_UVC_GET_CUR:
        response = selector == AVCAM_UVC_VS_PROBE_CONTROL ?
            state->Probe : state->Commit;
        responseSize = AVCAM_UVC_CONTROL_LENGTH;
        break;
    case AVCAM_UVC_GET_MIN:
    case AVCAM_UVC_GET_MAX:
    case AVCAM_UVC_GET_DEF:
        if (selector != AVCAM_UVC_VS_PROBE_CONTROL) {
            return AVCAM_UVC_RESULT_STALL;
        }
        AvCamUvcBuildDefault(defaults);
        response = defaults;
        responseSize = AVCAM_UVC_CONTROL_LENGTH;
        break;
    case AVCAM_UVC_GET_RES:
        if (selector != AVCAM_UVC_VS_PROBE_CONTROL) {
            return AVCAM_UVC_RESULT_STALL;
        }
        for (i = 0; i < AVCAM_UVC_CONTROL_LENGTH; ++i) {
            defaults[i] = 0;
        }
        response = defaults;
        responseSize = AVCAM_UVC_CONTROL_LENGTH;
        break;
    default:
        return AVCAM_UVC_RESULT_STALL;
    }

    if (bufferCapacity < responseSize) {
        /* Short control IN transfers are valid in USB; copy at most wLength. */
        responseSize = bufferCapacity;
    }
    if (responseSize == 0) {
        return AVCAM_UVC_RESULT_INVALID;
    }
    for (i = 0; i < responseSize; ++i) {
        buffer[i] = response[i];
    }
    *actualLength = responseSize;
    return AVCAM_UVC_RESULT_OK;
}
