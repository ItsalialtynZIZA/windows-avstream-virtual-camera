#include "MediaFoundationFrameSource.h"
#include "MediaFoundationDeviceSelector.h"
#include "VirtualCameraProtocol.h"

#include <mfapi.h>
#include <mferror.h>

#include <cstddef>
#include <cstring>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
bool CopyRgb32BufferTopDown(
    IMFMediaBuffer* buffer,
    unsigned char* destination,
    std::size_t destinationBytes,
    LONG defaultStride)
{
    if (buffer == nullptr || destination == nullptr ||
        destinationBytes < AVCAM_FRAME_BYTES_BGRA32)
    {
        return false;
    }

    ComPtr<IMF2DBuffer> buffer2d;
    if (SUCCEEDED(buffer->QueryInterface(IID_PPV_ARGS(&buffer2d))))
    {
        BYTE* scanline0 = nullptr;
        LONG pitch = 0;

        if (SUCCEEDED(buffer2d->Lock2D(&scanline0, &pitch)))
        {
            const LONG absolutePitch = (pitch < 0) ? -pitch : pitch;
            const bool valid =
                scanline0 != nullptr &&
                absolutePitch >= static_cast<LONG>(AVCAM_FRAME_STRIDE_BGRA32);

            if (valid)
            {
                for (unsigned long y = 0; y < AVCAM_FRAME_HEIGHT; ++y)
                {
                    const BYTE* sourceRow =
                        scanline0 + static_cast<std::ptrdiff_t>(y) * pitch;
                    unsigned char* destinationRow =
                        destination + static_cast<std::size_t>(y) *
                            AVCAM_FRAME_STRIDE_BGRA32;
                    std::memcpy(
                        destinationRow,
                        sourceRow,
                        AVCAM_FRAME_STRIDE_BGRA32);
                }
            }

            buffer2d->Unlock2D();
            return valid;
        }
    }

    BYTE* bytes = nullptr;
    DWORD maxLength = 0;
    DWORD currentLength = 0;
    if (FAILED(buffer->Lock(&bytes, &maxLength, &currentLength)))
    {
        return false;
    }

    const LONG stride =
        (defaultStride != 0)
            ? defaultStride
            : static_cast<LONG>(AVCAM_FRAME_STRIDE_BGRA32);
    const LONG absoluteStride = (stride < 0) ? -stride : stride;
    const std::size_t requiredBytes =
        static_cast<std::size_t>(absoluteStride) * AVCAM_FRAME_HEIGHT;

    const bool valid =
        bytes != nullptr &&
        absoluteStride >= static_cast<LONG>(AVCAM_FRAME_STRIDE_BGRA32) &&
        currentLength >= requiredBytes;

    if (valid)
    {
        const BYTE* scanline0 = bytes;
        if (stride < 0)
        {
            scanline0 +=
                static_cast<std::size_t>(AVCAM_FRAME_HEIGHT - 1u) *
                static_cast<std::size_t>(absoluteStride);
        }

        for (unsigned long y = 0; y < AVCAM_FRAME_HEIGHT; ++y)
        {
            const BYTE* sourceRow =
                scanline0 + static_cast<std::ptrdiff_t>(y) * stride;
            unsigned char* destinationRow =
                destination + static_cast<std::size_t>(y) *
                    AVCAM_FRAME_STRIDE_BGRA32;
            std::memcpy(
                destinationRow,
                sourceRow,
                AVCAM_FRAME_STRIDE_BGRA32);
        }
    }

    buffer->Unlock();
    return valid;
}

std::vector<std::wstring> EnumerateVideoSourceNamesInternal()
{
    std::vector<std::wstring> names;

    ComPtr<IMFAttributes> attributes;
    if (FAILED(MFCreateAttributes(&attributes, 1)) ||
        FAILED(attributes->SetGUID(
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID)))
    {
        return names;
    }

    IMFActivate** devices = nullptr;
    UINT32 deviceCount = 0;
    if (FAILED(MFEnumDeviceSources(attributes.Get(), &devices, &deviceCount)))
    {
        return names;
    }

    for (UINT32 index = 0; index < deviceCount; ++index)
    {
        WCHAR* friendlyName = nullptr;
        UINT32 nameLength = 0;
        if (SUCCEEDED(devices[index]->GetAllocatedString(
                MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
                &friendlyName,
                &nameLength)) &&
            friendlyName != nullptr)
        {
            names.emplace_back(friendlyName, nameLength);
            CoTaskMemFree(friendlyName);
        }
    }

    for (UINT32 index = 0; index < deviceCount; ++index)
    {
        if (devices[index] != nullptr)
        {
            devices[index]->Release();
        }
    }
    CoTaskMemFree(devices);
    return names;
}
} // namespace

std::vector<std::wstring> EnumerateMediaFoundationVideoSources()
{
    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_FULL)))
    {
        return {};
    }

    auto names = EnumerateVideoSourceNamesInternal();
    MFShutdown();
    return names;
}

MediaFoundationFrameSource::MediaFoundationFrameSource(
    std::wstring friendlyNameContains, std::wstring usbVidPidPair)
    : friendlyNameContains_(std::move(friendlyNameContains)),
      usbVidPidPair_(std::move(usbVidPidPair))
{
}

MediaFoundationFrameSource::~MediaFoundationFrameSource()
{
    Stop();
}

const wchar_t* MediaFoundationFrameSource::Name() const noexcept
{
    return selectedName_.empty()
        ? friendlyNameContains_.c_str()
        : selectedName_.c_str();
}

bool MediaFoundationFrameSource::Start()
{
    Stop();

    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_FULL)))
    {
        return false;
    }
    mediaFoundationStarted_ = true;

    if (!FindAndActivateDevice() || !ConfigureReader())
    {
        Stop();
        return false;
    }

    return true;
}

void MediaFoundationFrameSource::Stop() noexcept
{
    reader_.Reset();
    defaultStride_ = 0;

    if (mediaSource_)
    {
        mediaSource_->Shutdown();
        mediaSource_.Reset();
    }

    selectedName_.clear();

    if (mediaFoundationStarted_)
    {
        MFShutdown();
        mediaFoundationStarted_ = false;
    }
}

bool MediaFoundationFrameSource::FindAndActivateDevice()
{
    ComPtr<IMFAttributes> attributes;
    if (FAILED(MFCreateAttributes(&attributes, 1)) ||
        FAILED(attributes->SetGUID(
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID)))
    {
        return false;
    }

    IMFActivate** devices = nullptr;
    UINT32 deviceCount = 0;
    if (FAILED(MFEnumDeviceSources(attributes.Get(), &devices, &deviceCount)))
    {
        return false;
    }

    ComPtr<IMFActivate> matched;
    std::wstring matchedName;
    unsigned matches = 0;

    for (UINT32 index = 0; index < deviceCount; ++index)
    {
        WCHAR* friendlyName = nullptr;
        UINT32 nameLength = 0;
        const HRESULT nameResult = devices[index]->GetAllocatedString(
            MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
            &friendlyName, &nameLength);

        std::wstring name;
        if (SUCCEEDED(nameResult) && friendlyName != nullptr)
        {
            name.assign(friendlyName, nameLength);
        }
        CoTaskMemFree(friendlyName);

        if (name.empty())
        {
            continue;
        }

        std::wstring symbolicLink;
        if (!usbVidPidPair_.empty())
        {
            WCHAR* rawLink = nullptr;
            UINT32 linkLength = 0;
            if (SUCCEEDED(devices[index]->GetAllocatedString(
                    MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
                    &rawLink, &linkLength)) && rawLink != nullptr)
            {
                symbolicLink.assign(rawLink, linkLength);
            }
            CoTaskMemFree(rawLink);
        }

        if (AvCamSourceSelection::MatchesSource(
                name, symbolicLink, friendlyNameContains_, usbVidPidPair_))
        {
            ++matches;
            if (matches == 1u)
            {
                matched = devices[index];
                matchedName = name;
            }
        }
    }

    for (UINT32 index = 0; index < deviceCount; ++index)
    {
        if (devices[index] != nullptr)
        {
            devices[index]->Release();
        }
    }
    CoTaskMemFree(devices);

    /* Fail closed if two physical devices have the same name or filter.
     * Opening the first one is not a reliable source selection strategy.
     */
    if (matches != 1u || !matched)
    {
        return false;
    }
    ComPtr<IMFMediaSource> source;
    if (FAILED(matched->ActivateObject(IID_PPV_ARGS(&source))))
    {
        return false;
    }
    mediaSource_ = source;
    selectedName_ = matchedName;
    return true;
}

bool MediaFoundationFrameSource::ConfigureReader()
{
    if (!mediaSource_)
    {
        return false;
    }

    ComPtr<IMFAttributes> readerAttributes;
    if (FAILED(MFCreateAttributes(&readerAttributes, 3)))
    {
        return false;
    }

    if (FAILED(readerAttributes->SetUINT32(
            MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING,
            TRUE)) ||
        FAILED(readerAttributes->SetUINT32(
            MF_READWRITE_DISABLE_CONVERTERS,
            FALSE)))
    {
        return false;
    }

    if (FAILED(MFCreateSourceReaderFromMediaSource(
            mediaSource_.Get(),
            readerAttributes.Get(),
            &reader_)))
    {
        return false;
    }

    reader_->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    if (FAILED(reader_->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), TRUE)))
    {
        return false;
    }

    ComPtr<IMFMediaType> outputType;
    if (FAILED(MFCreateMediaType(&outputType)))
    {
        return false;
    }

    if (FAILED(outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
        FAILED(outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32)) ||
        FAILED(MFSetAttributeSize(
            outputType.Get(),
            MF_MT_FRAME_SIZE,
            AVCAM_FRAME_WIDTH,
            AVCAM_FRAME_HEIGHT)) ||
        FAILED(MFSetAttributeRatio(outputType.Get(), MF_MT_FRAME_RATE, 30, 1)) ||
        FAILED(MFSetAttributeRatio(outputType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1)) ||
        FAILED(outputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive)))
    {
        return false;
    }

    if (FAILED(reader_->SetCurrentMediaType(
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
            nullptr,
            outputType.Get())))
    {
        return false;
    }

    return ValidateCurrentOutputType();
}

bool MediaFoundationFrameSource::ValidateCurrentOutputType()
{
    if (!reader_)
    {
        return false;
    }

    ComPtr<IMFMediaType> currentType;
    if (FAILED(reader_->GetCurrentMediaType(
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
            &currentType)))
    {
        return false;
    }

    GUID majorType{};
    GUID subtype{};
    UINT32 width = 0;
    UINT32 height = 0;
    UINT32 frameRateNumerator = 0;
    UINT32 frameRateDenominator = 0;

    if (FAILED(currentType->GetGUID(MF_MT_MAJOR_TYPE, &majorType)) ||
        FAILED(currentType->GetGUID(MF_MT_SUBTYPE, &subtype)) ||
        FAILED(MFGetAttributeSize(
            currentType.Get(),
            MF_MT_FRAME_SIZE,
            &width,
            &height)))
    {
        return false;
    }

    const HRESULT frameRateResult = MFGetAttributeRatio(
        currentType.Get(),
        MF_MT_FRAME_RATE,
        &frameRateNumerator,
        &frameRateDenominator);

    const bool frameRateAcceptable =
        FAILED(frameRateResult) || frameRateDenominator == 0 ||
        (frameRateNumerator == 30u * frameRateDenominator);

    if (!IsEqualGUID(majorType, MFMediaType_Video) ||
        !IsEqualGUID(subtype, MFVideoFormat_RGB32) ||
        width != AVCAM_FRAME_WIDTH ||
        height != AVCAM_FRAME_HEIGHT ||
        !frameRateAcceptable)
    {
        return false;
    }

    UINT32 strideValue = 0;
    if (SUCCEEDED(currentType->GetUINT32(MF_MT_DEFAULT_STRIDE, &strideValue)))
    {
        defaultStride_ = static_cast<LONG>(strideValue);
    }
    else
    {
        defaultStride_ = static_cast<LONG>(AVCAM_FRAME_STRIDE_BGRA32);
    }

    const LONG absoluteStride =
        (defaultStride_ < 0) ? -defaultStride_ : defaultStride_;
    return absoluteStride >= static_cast<LONG>(AVCAM_FRAME_STRIDE_BGRA32);
}

bool MediaFoundationFrameSource::ReadFrame(
    unsigned char* Destination,
    std::size_t DestinationBytes,
    std::uint64_t& Timestamp100ns)
{
    if (!reader_ || Destination == nullptr ||
        DestinationBytes < AVCAM_FRAME_BYTES_BGRA32)
    {
        return false;
    }

    for (int attempt = 0; attempt < 8; ++attempt)
    {
        DWORD actualStream = 0;
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        ComPtr<IMFSample> sample;

        const HRESULT readResult = reader_->ReadSample(
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
            0,
            &actualStream,
            &flags,
            &timestamp,
            &sample);

        if (FAILED(readResult) ||
            (flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0 ||
            (flags & MF_SOURCE_READERF_ERROR) != 0)
        {
            return false;
        }

        if ((flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) != 0 &&
            !ValidateCurrentOutputType())
        {
            return false;
        }

        if (!sample)
        {
            continue;
        }

        ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(sample->ConvertToContiguousBuffer(&buffer)))
        {
            return false;
        }

        if (!CopyRgb32BufferTopDown(
                buffer.Get(),
                Destination,
                DestinationBytes,
                defaultStride_))
        {
            return false;
        }

        Timestamp100ns = static_cast<std::uint64_t>(timestamp);
        return true;
    }

    return false;
}
