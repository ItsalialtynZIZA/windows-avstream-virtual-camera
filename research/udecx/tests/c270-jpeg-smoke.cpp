#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <vector>
#include <cstdio>
#include <cstdint>
#include "../tools/AvCamUdeMjpegEncoder.h"
#include "../AvCamUdeUploadProtocol.h"
#include "../../../shared/VirtualCameraProtocol.h"

using Microsoft::WRL::ComPtr;
int main() {
    static_assert(AVCAM_UDE_UPLOAD_WIDTH == AVCAM_FRAME_WIDTH, "C270 encoder must use source width");
    static_assert(AVCAM_UDE_UPLOAD_HEIGHT == AVCAM_FRAME_HEIGHT, "C270 encoder must use source height");
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com)) return 1;
    int result = 2;
    do {
        ComPtr<IWICImagingFactory> factory;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) break;
        std::vector<unsigned char> frame(AVCAM_FRAME_BYTES_BGRA32, 128);
        for (std::size_t i = 3; i < frame.size(); i += 4) frame[i] = 255;
        std::vector<unsigned char> jpeg;
        if (!AvCamUde::EncodeBgraToMjpeg(
                factory.Get(), frame.data(), AVCAM_FRAME_WIDTH,
                AVCAM_FRAME_HEIGHT, AVCAM_FRAME_STRIDE_BGRA32, jpeg)) break;
        if (AvCamUdeValidateJpegHeader(jpeg.data(),
                static_cast<unsigned int>(jpeg.size()),
                AVCAM_UDE_UPLOAD_WIDTH, AVCAM_UDE_UPLOAD_HEIGHT) != AVCAM_UDE_UPLOAD_OK) break;
        ComPtr<IStream> stream;
        if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) break;
        ULONG written = 0;
        if (FAILED(stream->Write(jpeg.data(), static_cast<ULONG>(jpeg.size()), &written)) ||
            written != jpeg.size()) break;
        LARGE_INTEGER origin{};
        if (FAILED(stream->Seek(origin, STREAM_SEEK_SET, nullptr))) break;
        ComPtr<IWICBitmapDecoder> decoder;
        if (FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr,
                WICDecodeMetadataCacheOnDemand, &decoder))) break;
        ComPtr<IWICBitmapFrameDecode> decoded;
        if (FAILED(decoder->GetFrame(0, &decoded))) break;
        UINT width = 0, height = 0;
        if (FAILED(decoded->GetSize(&width, &height)) ||
            width != AVCAM_UDE_UPLOAD_WIDTH || height != AVCAM_UDE_UPLOAD_HEIGHT) break;
        std::puts("AVCAM_C270_720P_JPEG_ENCODE_DECODE_PASSED");
        result = 0;
    } while (false);
    CoUninitialize();
    return result;
}
