#include "MediaFoundationFrameSource.h"
#include "VirtualCameraProtocol.h"

#include <mfapi.h>
#include <mferror.h>

#include <algorithm>
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
} // namespace

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

    // Windows 11 XVP path: color conversion + resize + frame-rate conversion.
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
            std::memcpy(Destination, bytes, AVCAM_FRAME_BYTES_BGRA32);
            Timestamp100ns = static_cast<std::uint64_t>(timestamp);
        }

        buffer->Unlock();
        return valid;
    }

    return false;
}
