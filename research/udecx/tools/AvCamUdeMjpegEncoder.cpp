#include "AvCamUdeMjpegEncoder.h"
#include "../AvCamUdeUploadProtocol.h"

#include <limits>

using Microsoft::WRL::ComPtr;

namespace AvCamUde {

bool EncodeBgraToMjpeg(
    IWICImagingFactory* factory,
    const unsigned char* bgra,
    unsigned int width,
    unsigned int height,
    unsigned int stride,
    std::vector<unsigned char>& jpeg)
{
    jpeg.clear();
    if (factory == nullptr || bgra == nullptr ||
        width == 0 || height == 0 || width > 4096 || height > 2160 ||
        width > std::numeric_limits<unsigned int>::max() / 4u ||
        stride < width * 4u ||
        height > std::numeric_limits<unsigned int>::max() / stride) {
        return false;
    }

    ComPtr<IWICBitmap> source;
    HRESULT hr = factory->CreateBitmapFromMemory(
        width, height, GUID_WICPixelFormat32bppBGRA,
        stride, stride * height, const_cast<BYTE*>(bgra), &source);
    if (FAILED(hr)) return false;

    ComPtr<IWICBitmapScaler> scaler;
    hr = factory->CreateBitmapScaler(&scaler);
    if (FAILED(hr)) return false;
    hr = scaler->Initialize(
        source.Get(), AVCAM_UDE_UPLOAD_WIDTH, AVCAM_UDE_UPLOAD_HEIGHT,
        WICBitmapInterpolationModeFant);
    if (FAILED(hr)) return false;

    ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr)) return false;
    hr = converter->Initialize(
        scaler.Get(), GUID_WICPixelFormat24bppBGR,
        WICBitmapDitherTypeNone, nullptr, 0.0,
        WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) return false;

    ComPtr<IStream> stream;
    hr = CreateStreamOnHGlobal(nullptr, TRUE, &stream);
    if (FAILED(hr)) return false;
    ComPtr<IWICBitmapEncoder> encoder;
    hr = factory->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &encoder);
    if (FAILED(hr)) return false;
    hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) return false;

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> properties;
    hr = encoder->CreateNewFrame(&frame, &properties);
    if (FAILED(hr)) return false;
    hr = frame->Initialize(properties.Get());
    if (FAILED(hr)) return false;
    hr = frame->SetSize(AVCAM_UDE_UPLOAD_WIDTH, AVCAM_UDE_UPLOAD_HEIGHT);
    if (FAILED(hr)) return false;
    WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat24bppBGR;
    hr = frame->SetPixelFormat(&pixelFormat);
    if (FAILED(hr) ||
        !IsEqualGUID(pixelFormat, GUID_WICPixelFormat24bppBGR)) {
        return false;
    }
    hr = frame->WriteSource(converter.Get(), nullptr);
    if (FAILED(hr)) return false;
    hr = frame->Commit();
    if (FAILED(hr)) return false;
    hr = encoder->Commit();
    if (FAILED(hr)) return false;

    STATSTG info{};
    hr = stream->Stat(&info, STATFLAG_NONAME);
    if (FAILED(hr) || info.cbSize.HighPart != 0 ||
        info.cbSize.LowPart < 16u ||
        info.cbSize.LowPart > AVCAM_UVC_FRAME_MAX_BYTES) {
        return false;
    }
    LARGE_INTEGER beginning{};
    hr = stream->Seek(beginning, STREAM_SEEK_SET, nullptr);
    if (FAILED(hr)) return false;
    std::vector<unsigned char> candidate(info.cbSize.LowPart);
    ULONG received = 0;
    hr = stream->Read(
        candidate.data(), static_cast<ULONG>(candidate.size()), &received);
    if (FAILED(hr) || received != candidate.size() ||
        AvCamUdeValidateJpegHeader(
            candidate.data(), static_cast<unsigned int>(candidate.size()),
            AVCAM_UDE_UPLOAD_WIDTH, AVCAM_UDE_UPLOAD_HEIGHT) !=
            AVCAM_UDE_UPLOAD_OK) {
        return false;
    }
    jpeg.swap(candidate);
    return true;
}
} // namespace AvCamUde
