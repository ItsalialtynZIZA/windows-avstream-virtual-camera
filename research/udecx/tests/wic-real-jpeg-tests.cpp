/*
 * Windows-only integration acceptance: WIC creates and decodes a genuine
 * 1920x1080 JPEG; the same bytes must survive MJPEG source publication and
 * UVC ISO packet reconstruction. No USB controller is installed or used.
 */
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "../AvCamUdeUploadProtocol.h"
#include "../AvCamUdeFramePump.h"

using Microsoft::WRL::ComPtr;

static void Expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static void Check(HRESULT hr, const char* message) {
    if (FAILED(hr)) throw std::runtime_error(message);
}

struct ComLifetime {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~ComLifetime() {
        if (SUCCEEDED(hr)) CoUninitialize();
    }
};

static std::vector<unsigned char> MakeRealJpeg(IWICImagingFactory* factory) {
    ComPtr<IStream> memory;
    Check(CreateStreamOnHGlobal(nullptr, TRUE, &memory),
          "Cannot create in-memory stream");

    ComPtr<IWICBitmapEncoder> encoder;
    Check(factory->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &encoder),
          "JPEG encoder unavailable");
    Check(encoder->Initialize(memory.Get(), WICBitmapEncoderNoCache),
          "JPEG encoder init failed");
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> properties;
    Check(encoder->CreateNewFrame(&frame, &properties),
          "JPEG frame initialization failed");
    Check(frame->Initialize(properties.Get()), "JPEG frame init failed");
    constexpr UINT width = AVCAM_UDE_UPLOAD_WIDTH;
    constexpr UINT height = AVCAM_UDE_UPLOAD_HEIGHT;
    Check(frame->SetSize(width, height), "Unable to set JPEG dimensions");
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    Check(frame->SetPixelFormat(&format), "Unable to select JPEG pixel format");
    Expect(IsEqualGUID(format, GUID_WICPixelFormat24bppBGR) != FALSE,
           "WIC changed the requested pixel format");

    const UINT stride = width * 3u;
    std::vector<BYTE> original(static_cast<size_t>(stride) * height);
    for (UINT y = 0; y < height; ++y) {
        for (UINT x = 0; x < width; ++x) {
            const auto pos = (static_cast<size_t>(y) * width + x) * 3u;
            original[pos] = static_cast<BYTE>(x & 255u);
            original[pos + 1] = static_cast<BYTE>(y & 255u);
            original[pos + 2] = static_cast<BYTE>((x / 8u + y / 8u) & 255u);
        }
    }
    Check(frame->WritePixels(height, stride,
         static_cast<UINT>(original.size()), original.data()),
         "Cannot encode JPEG pixel rows");
    Check(frame->Commit(), "JPEG frame commit failed");
    Check(encoder->Commit(), "JPEG encoder commit failed");

    STATSTG stat{};
    Check(memory->Stat(&stat, STATFLAG_NONAME), "Cannot determine JPEG size");
    Expect(stat.cbSize.HighPart == 0 &&
           stat.cbSize.LowPart >= 16u &&
           stat.cbSize.LowPart <= AVCAM_UVC_FRAME_MAX_BYTES,
           "Encoded image exceeds advertised frame bounds");
    LARGE_INTEGER origin{};
    Check(memory->Seek(origin, STREAM_SEEK_SET, nullptr), "JPEG rewind failed");
    std::vector<unsigned char> jpeg(stat.cbSize.LowPart);
    ULONG read = 0;
    Check(memory->Read(jpeg.data(), static_cast<ULONG>(jpeg.size()), &read),
          "JPEG stream read failed");
    Expect(read == jpeg.size(), "JPEG stream unexpectedly truncated");
    return jpeg;
}

static void VerifyDecodable(IWICImagingFactory* factory,
                            const std::vector<unsigned char>& jpeg) {
    ComPtr<IStream> stream;
    Check(CreateStreamOnHGlobal(nullptr, TRUE, &stream),
          "Cannot allocate decoder stream");
    ULONG written = 0;
    Check(stream->Write(jpeg.data(), static_cast<ULONG>(jpeg.size()), &written),
          "Decoder input write failed");
    Expect(written == jpeg.size(), "Decoder input write truncated");
    LARGE_INTEGER origin{};
    Check(stream->Seek(origin, STREAM_SEEK_SET, nullptr),
          "Decoder input seek failed");

    ComPtr<IWICBitmapDecoder> decoder;
    Check(factory->CreateDecoderFromStream(
        stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder),
        "JPEG decoder refused stream");
    ComPtr<IWICBitmapFrameDecode> frame;
    Check(decoder->GetFrame(0, &frame), "JPEG decode frame missing");
    UINT width = 0;
    UINT height = 0;
    Check(frame->GetSize(&width, &height), "JPEG dimensions unavailable");
    Expect(width == AVCAM_UDE_UPLOAD_WIDTH &&
           height == AVCAM_UDE_UPLOAD_HEIGHT,
           "JPEG decoder dimensions disagree with UVC descriptors");

    ComPtr<IWICFormatConverter> converter;
    Check(factory->CreateFormatConverter(&converter),
          "Cannot create decoder conversion");
    Check(converter->Initialize(
        frame.Get(), GUID_WICPixelFormat24bppBGR,
        WICBitmapDitherTypeNone, nullptr, 0,
        WICBitmapPaletteTypeCustom), "JPEG pixel conversion failed");
    const UINT stride = width * 3u;
    std::vector<BYTE> pixels(static_cast<size_t>(stride) * height);
    Check(converter->CopyPixels(
        nullptr, stride, static_cast<UINT>(pixels.size()), pixels.data()),
        "JPEG entropy payload could not be decoded");
    Expect(std::any_of(pixels.begin(), pixels.end(),
        [](BYTE b) { return b != 0u; }), "Decoded JPEG unexpectedly empty");
}

static void TestEndToEnd(IWICImagingFactory* factory) {
    const auto jpeg = MakeRealJpeg(factory);
    VerifyDecodable(factory, jpeg);

    AVCAM_UDE_UPLOAD_HEADER header{};
    header.Magic = AVCAM_UDE_UPLOAD_MAGIC;
    header.Version = AVCAM_UDE_UPLOAD_VERSION;
    header.Width = AVCAM_UDE_UPLOAD_WIDTH;
    header.Height = AVCAM_UDE_UPLOAD_HEIGHT;
    header.FrameBytes = static_cast<unsigned>(jpeg.size());
    std::vector<unsigned char> envelope(sizeof(header) + jpeg.size());
    std::memcpy(envelope.data(), &header, sizeof(header));
    std::memcpy(envelope.data() + sizeof(header), jpeg.data(), jpeg.size());

    const unsigned char* validated = nullptr;
    unsigned validatedBytes = 0;
    Expect(AvCamUdeValidateUpload(
        envelope.data(), static_cast<unsigned>(envelope.size()),
        &validated, &validatedBytes) == AVCAM_UDE_UPLOAD_OK &&
        validatedBytes == jpeg.size(), "Actual WIC JPEG rejected by protocol");

    std::vector<unsigned char> slotA(AVCAM_UVC_FRAME_MAX_BYTES);
    std::vector<unsigned char> slotB(AVCAM_UVC_FRAME_MAX_BYTES);
    AVCAM_UDE_FRAME_SOURCE source{};
    Expect(AvCamUdeFrameSourceInit(
        &source, slotA.data(), static_cast<unsigned>(slotA.size()),
        slotB.data(), static_cast<unsigned>(slotB.size())) == AVCAM_UDE_FRAME_OK,
        "Frame source refused bounded buffers");
    Expect(AvCamUdeFrameSourcePublish(&source, validated, validatedBytes) ==
           AVCAM_UDE_FRAME_OK, "Real JPEG publication failed");

    AVCAM_UDE_FRAME_PUMP pump{};
    Expect(AvCamUdeFramePumpInit(&pump, &source) == AVCAM_UDE_PUMP_OK,
           "Frame pump init failed");
    constexpr unsigned packets = 128u;
    constexpr unsigned packetCapacity = 3072u;
    constexpr unsigned capacity = packets * packetCapacity;
    std::vector<unsigned char> urb(capacity);
    std::vector<unsigned> offsets(packets);
    std::vector<unsigned> lengths(packets);
    for (unsigned i = 0; i < packets; ++i)
        offsets[i] = i * packetCapacity;
    std::vector<unsigned char> reconstructed;
    bool eof = false;
    for (unsigned frame = 0; frame < 15u && !eof; ++frame) {
        unsigned filled = 0;
        Expect(AvCamUdeFramePumpFill(
            &pump, urb.data(), capacity, offsets.data(), packets,
            static_cast<unsigned long long>(frame) * packets *
                AVCAM_UDE_ISO_MICROFRAME_100NS,
            lengths.data(), &filled) == AVCAM_UDE_PUMP_OK,
            "Cannot packetize real JPEG through ISO frame pump");
        unsigned sum = 0;
        for (unsigned i = 0; i < packets; ++i) {
            sum += lengths[i];
            if (lengths[i] == 0) continue;
            const unsigned char* ptr = urb.data() + offsets[i];
            Expect(lengths[i] >= 3u && ptr[0] == 2u &&
                   (ptr[1] & AVCAM_UVC_PAYLOAD_EOH) != 0,
                   "UVC MJPEG header malformed");
            reconstructed.insert(
                reconstructed.end(), ptr + 2u, ptr + lengths[i]);
            if ((ptr[1] & AVCAM_UVC_PAYLOAD_EOF) != 0) {
                Expect(!eof, "Duplicate EOF");
                eof = true;
                break;
            }
        }
        Expect(sum <= capacity && filled <= capacity,
               "URB byte sum exceeds capacity");
    }
    Expect(eof && reconstructed == jpeg,
           "Real JPEG changed across sparse ISO URBs");
    VerifyDecodable(factory, reconstructed);
    Expect(AvCamUdeFramePumpReset(&pump) == AVCAM_UDE_PUMP_OK,
           "Cannot reset JPEG pump after validated EOF");
    std::cout << "WIC JPEG encoded, ISO reconstructed, decoded, bytes="
              << jpeg.size() << "\n";
}

int main() {
    try {
        ComLifetime com;
        Check(com.hr, "COM MTA initialization failed");
        ComPtr<IWICImagingFactory> factory;
        Check(CoCreateInstance(
            CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory)), "WIC factory unavailable");
        TestEndToEnd(factory.Get());
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << "\n";
        return 1;
    }
}
