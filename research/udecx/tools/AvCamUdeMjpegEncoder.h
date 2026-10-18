#pragma once
/*
 * User-mode only. Converts existing 1280x720 BGRA32 AvCam source frames into
 * a real 1920x1080 JPEG for the UdeCx upload protocol. No driver installation.
 * COM must be initialized on the calling thread.
 */
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <vector>

namespace AvCamUde {
bool EncodeBgraToMjpeg(
    IWICImagingFactory* factory,
    const unsigned char* bgra,
    unsigned int width,
    unsigned int height,
    unsigned int stride,
    std::vector<unsigned char>& jpeg);
}
