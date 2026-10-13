#include "MediaFoundationFrameSource.h"
#include "VirtualCameraProtocol.h"

#include <mfapi.h>
#include <mferror.h>

#include <algorithm>
#include <cstddef>
#include <cwctype>
#include <cstring>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
bool ContainsInsensitive(const std::wstring& value, const std::wstring& fragment)
{
    if (fragment.empty())
    {
        return true;
    }

    std::wstring lhs = value;
    std::wstring rhs = fragment;

    std::transform(lhs.begin(), lhs.end(), lhs.begin(),
        [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    std::transform(rhs.begin(), rhs.end(), rhs.begin(),
        [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });

    return lhs.find(rhs) != std::wstring::npos;
}

bool CopyRgb32BufferTopDown(
    IMFMediaBuffer* buffer,
    unsigned char* destination,
    std::size_t destinationBytes)
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

    const bool valid =
        bytes != nullptr && currentLength >= AVCAM_FRAME_BYTES_BGRA32;

    if (valid)
    {
        std::memcpy(destination, bytes, AVCAM_FRAME_BYTES_BGRA32);
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

MediaFoundationFrameSource::MediaFoundationFrameSource(std::wstring friendlyNameContains)
    : friendlyNameContains_(std::move(friendlyNameContains))
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
    if (FAILED(MFCreateAttributes(&attributes, 1)))
    {
        return false;
    }

    if (FAILED(attributes->SetGUID(
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID)))
    {
        return false;
    }

    IMFActivate** devices = nullptr;
    UINT32 deviceCount = 0;
    const HRESULT enumResult =
        MFEnumDeviceSources(attributes.Get(), &devices, &deviceCount);

    if (FAILED(enumResult))
    {
        return false;
    }

    bool found = false;

    for (UINT32 index = 0; index < deviceCount; ++index)
    {
        WCHAR* friendlyName = nullptr;
        UINT32 nameLength = 0;

        const HRESULT nameResult = devices[index]->GetAllocatedString(
            MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
            &friendlyName,
            &nameLength);

        std::wstring name;
        if (SUCCEEDED(nameResult) && friendlyName != nullptr)
        {
            name.assign(friendlyName, nameLength);
            CoTaskMemFree(friendlyName);
        }

        if (!name.empty() && ContainsInsensitive(name, friendlyNameContains_))
        {
            ComPtr<IMFMediaSource> source;
            if (SUCCEEDED(devices[index]->ActivateObject(IID_PPV_ARGS(&source))))
            {
                mediaSource_ = source;
                selectedName_ = name;
                found = true;
                break;
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

    return found;
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
            TRUE)))
    {
        return false;
    }

    if (FAILED(readerAttributes->SetUINT32(
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

    reader_->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    if (FAILED(reader_->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE)))
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

    return SUCCEEDED(reader_->SetCurrentMediaType(
        MF_SOURCE_READER_FIRST_VIDEO_STREAM,
        nullptr,
        outputType.Get()));
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
            MF_SOURCE_READER_FIRST_VIDEO_STREAM,
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

        if (!sample)
        {
            continue;
        }

        ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(sample->ConvertToContiguousBuffer(&buffer)))
        {
            return false;
        }

        if (!CopyRgb32BufferTopDown(buffer.Get(), Destination, DestinationBytes))
        {
            return false;
        }

        Timestamp100ns = static_cast<std::uint64_t>(timestamp);
        return true;
    }

    return false;
}
