#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "../tools/AvCamUdeMjpegEncoder.h"
#include "../AvCamUdeUploadProtocol.h"

using Microsoft::WRL::ComPtr;
static void Check(bool success, const char* message) {
    if (!success) throw std::runtime_error(message);
}
static void CheckHr(HRESULT result, const char* message) {
    Check(SUCCEEDED(result), message);
}
int main() {
    HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com)) return 2;
    int result = 0;
    try {
        ComPtr<IWICImagingFactory> factory;
        CheckHr(CoCreateInstance(
            CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory)), "WIC unavailable");

        constexpr unsigned width = 1280u;
        constexpr unsigned height = 720u;
        constexpr unsigned stride = width * 4u;
        std::vector<unsigned char> bgra(
            static_cast<size_t>(stride) * height, 0xff);
        for (unsigned y = 0; y < height; ++y) {
            for (unsigned x = 0; x < width; ++x) {
                const size_t offset = static_cast<size_t>(y) * stride + x * 4u;
                bgra[offset] = static_cast<unsigned char>((x / 8u) & 255u);
                bgra[offset + 1u] = static_cast<unsigned char>((y / 8u) & 255u);
                bgra[offset + 2u] = static_cast<unsigned char>((x / 16u + y / 16u) & 255u);
            }
        }
        std::vector<unsigned char> jpeg;
        Check(AvCamUde::EncodeBgraToMjpeg(
            factory.Get(), bgra.data(), width, height, stride, jpeg),
            "WIC BGRA32 -> scaled JPEG conversion failed");
        Check(jpeg.size() >= 16u && jpeg.size() <= AVCAM_UVC_FRAME_MAX_BYTES,
            "JPEG exceeds negotiated UVC frame size");
        Check(AvCamUdeValidateJpegHeader(
            jpeg.data(), static_cast<unsigned>(jpeg.size()),
            AVCAM_UDE_UPLOAD_WIDTH, AVCAM_UDE_UPLOAD_HEIGHT) ==
            AVCAM_UDE_UPLOAD_OK, "JPEG SOF dimensions rejected");

        ComPtr<IStream> stream;
        CheckHr(CreateStreamOnHGlobal(nullptr, TRUE, &stream),
            "JPEG decoder stream unavailable");
        ULONG written = 0;
        CheckHr(stream->Write(
            jpeg.data(), static_cast<ULONG>(jpeg.size()), &written),
            "JPEG memory stream write failed");
        Check(written == jpeg.size(), "JPEG memory write truncated");
        LARGE_INTEGER zero{};
        CheckHr(stream->Seek(zero, STREAM_SEEK_SET, nullptr),
            "JPEG memory stream rewind failed");
        ComPtr<IWICBitmapDecoder> decoder;
        CheckHr(factory->CreateDecoderFromStream(
            stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder),
            "WIC decoder rejected JPEG");
        ComPtr<IWICBitmapFrameDecode> frame;
        CheckHr(decoder->GetFrame(0, &frame), "JPEG frame missing");
        UINT decodedWidth = 0, decodedHeight = 0;
        CheckHr(frame->GetSize(&decodedWidth, &decodedHeight),
            "Decoded dimensions unavailable");
        Check(decodedWidth == AVCAM_UDE_UPLOAD_WIDTH &&
              decodedHeight == AVCAM_UDE_UPLOAD_HEIGHT,
            "Encoded JPEG has wrong output dimensions");
        ComPtr<IWICFormatConverter> converter;
        CheckHr(factory->CreateFormatConverter(&converter),
            "Cannot create JPEG format converter");
        CheckHr(converter->Initialize(
            frame.Get(), GUID_WICPixelFormat24bppBGR,
            WICBitmapDitherTypeNone, nullptr, 0.0,
            WICBitmapPaletteTypeCustom), "JPEG pixel conversion failed");
        const UINT outStride = decodedWidth * 3u;
        std::vector<unsigned char> decoded(
            static_cast<size_t>(outStride) * decodedHeight);
        CheckHr(converter->CopyPixels(
            nullptr, outStride, static_cast<UINT>(decoded.size()),
            decoded.data()), "JPEG entropy payload cannot be decoded");
        Check(std::any_of(decoded.begin(), decoded.end(),
            [](unsigned char b) { return b != 0; }),
            "Decoded pixel buffer unexpectedly empty");

        std::vector<unsigned char> ignored(4u, 0u);
        Check(!AvCamUde::EncodeBgraToMjpeg(
            factory.Get(), ignored.data(), 1280u, 720u, 4u, jpeg),
            "Invalid BGRA stride accepted");
        Check(jpeg.empty(), "Failed encode must clear output");
        std::cout << "OBS-style BGRA1280x720 -> WIC MJPEG1920x1080 decoded successfully\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        result = 1;
    }
    CoUninitialize();
    return result;
}
