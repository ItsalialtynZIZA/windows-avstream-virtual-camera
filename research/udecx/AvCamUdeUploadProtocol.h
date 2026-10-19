#pragma once
/*
 * Versioned METHOD_BUFFERED transport for MJPEG ingestion on the research
 * UdeCx controller. It deliberately does not claim that JPEG scan data is
 * decodable: the destination must still be checked with a real decoder.
 *
 * Portable for kernel/user-mode builds. All fields are 32-bit little-endian.
 * No pointer, IOCTL code, GUID or native HANDLE is ever accepted as payload.
 */
#include "../manual-vhci/shared/AvCamUvcControls.h"

/* Kernel and Win32 SDK define CTL_CODE, FILE_DEVICE_UNKNOWN, METHOD_BUFFERED
 * and FILE_WRITE_ACCESS. The portable validator never expands this macro.
 */
#define IOCTL_AVCAM_UDE_SUBMIT_MJPEG \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x950u, METHOD_BUFFERED, FILE_WRITE_ACCESS)

#define AVCAM_UDE_UPLOAD_MAGIC 0x314A5641u /* "AVJ1" little-endian */
#define AVCAM_UDE_UPLOAD_VERSION 1u
#if defined(AVCAM_UDE_C270_COMPAT_PROFILE)
#define AVCAM_UDE_UPLOAD_WIDTH 1280u
#define AVCAM_UDE_UPLOAD_HEIGHT 720u
#else
#define AVCAM_UDE_UPLOAD_WIDTH 1920u
#define AVCAM_UDE_UPLOAD_HEIGHT 1080u
#endif
#define AVCAM_UDE_UPLOAD_OK 0
#define AVCAM_UDE_UPLOAD_INVALID 1

/* Requires matching 20-byte layout in user/kernel compilers. */
typedef struct _AVCAM_UDE_UPLOAD_HEADER {
    unsigned int Magic;
    unsigned int Version;
    unsigned int Width;
    unsigned int Height;
    unsigned int FrameBytes;
} AVCAM_UDE_UPLOAD_HEADER;

/* Caller has checked input buffer byte count. Scan all bounded segment
 * lengths up to SOS. Enforce advertised frame dimensions and SOI/SOF/SOS/EOI
 * order. This detects malformed wrappers, NOT truncated Huffman bitstreams,
 * and does not substitute for independent MJPEG decoding.
 */
static __inline int
AvCamUdeValidateJpegHeader(
    const unsigned char* data,
    unsigned int bytes,
    unsigned int expectedWidth,
    unsigned int expectedHeight)
{
    unsigned int cursor;
    int sawSof = 0;
    if (data == 0 || bytes < 16u ||
        bytes > AVCAM_UVC_FRAME_MAX_BYTES ||
        data[0] != 0xffu || data[1] != 0xd8u ||
        data[bytes - 2u] != 0xffu || data[bytes - 1u] != 0xd9u) {
        return AVCAM_UDE_UPLOAD_INVALID;
    }
    cursor = 2u;
    while (cursor < bytes - 2u) {
        unsigned char marker;
        unsigned int length;
        if (data[cursor++] != 0xffu) {
            return AVCAM_UDE_UPLOAD_INVALID;
        }
        while (cursor < bytes - 2u && data[cursor] == 0xffu) ++cursor;
        if (cursor >= bytes - 2u) return AVCAM_UDE_UPLOAD_INVALID;
        marker = data[cursor++];

        /* Restart markers belong to entropy data, not header segments. */
        if (marker == 0u || marker == 0xd8u || marker == 0xd9u ||
            marker == 0x01u || (marker >= 0xd0u && marker <= 0xd7u) ||
            cursor + 2u > bytes - 2u) {
            return AVCAM_UDE_UPLOAD_INVALID;
        }
        length = ((unsigned int)data[cursor] << 8) |
            (unsigned int)data[cursor + 1u];
        if (length < 2u || length > bytes - 2u - cursor) {
            return AVCAM_UDE_UPLOAD_INVALID;
        }
        if (marker == 0xc0u || marker == 0xc1u || marker == 0xc2u) {
            unsigned int width, height;
            if (sawSof || length < 8u) return AVCAM_UDE_UPLOAD_INVALID;
            height = ((unsigned int)data[cursor + 3u] << 8) |
                (unsigned int)data[cursor + 4u];
            width = ((unsigned int)data[cursor + 5u] << 8) |
                (unsigned int)data[cursor + 6u];
            if (width != expectedWidth || height != expectedHeight) {
                return AVCAM_UDE_UPLOAD_INVALID;
            }
            sawSof = 1;
        }
        if (marker == 0xdau) {
            /* SOS segment must include component selector data and at least
             * one entropy byte before the terminal EOI marker.
             */
            if (!sawSof || length < 6u ||
                cursor + length >= bytes - 2u) {
                return AVCAM_UDE_UPLOAD_INVALID;
            }
            return AVCAM_UDE_UPLOAD_OK;
        }
        cursor += length;
    }
    return AVCAM_UDE_UPLOAD_INVALID;
}

static __inline int
AvCamUdeValidateUpload(
    const unsigned char* input,
    unsigned int inputBytes,
    const unsigned char** jpeg,
    unsigned int* jpegBytes)
{
    const AVCAM_UDE_UPLOAD_HEADER* header;
    if (jpeg == 0 || jpegBytes == 0) return AVCAM_UDE_UPLOAD_INVALID;
    *jpeg = 0;
    *jpegBytes = 0;
    if (input == 0 || inputBytes < sizeof(AVCAM_UDE_UPLOAD_HEADER) ||
        sizeof(AVCAM_UDE_UPLOAD_HEADER) != 20u) {
        return AVCAM_UDE_UPLOAD_INVALID;
    }
    header = (const AVCAM_UDE_UPLOAD_HEADER*)input;
    if (header->Magic != AVCAM_UDE_UPLOAD_MAGIC ||
        header->Version != AVCAM_UDE_UPLOAD_VERSION ||
        header->Width != AVCAM_UDE_UPLOAD_WIDTH ||
        header->Height != AVCAM_UDE_UPLOAD_HEIGHT ||
        header->FrameBytes == 0 ||
        header->FrameBytes > AVCAM_UVC_FRAME_MAX_BYTES ||
        header->FrameBytes !=
            inputBytes - (unsigned int)sizeof(AVCAM_UDE_UPLOAD_HEADER)) {
        return AVCAM_UDE_UPLOAD_INVALID;
    }
    if (AvCamUdeValidateJpegHeader(
            input + sizeof(AVCAM_UDE_UPLOAD_HEADER),
            header->FrameBytes, header->Width, header->Height) !=
        AVCAM_UDE_UPLOAD_OK) {
        return AVCAM_UDE_UPLOAD_INVALID;
    }
    *jpeg = input + sizeof(AVCAM_UDE_UPLOAD_HEADER);
    *jpegBytes = header->FrameBytes;
    return AVCAM_UDE_UPLOAD_OK;
}
