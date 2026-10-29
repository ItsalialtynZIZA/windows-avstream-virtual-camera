#pragma once

#include "FrameSource.h"

#include <wrl/client.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <string>
#include <vector>

struct MediaFoundationVideoSourceInfo
{
    std::wstring friendlyName;
    std::wstring symbolicLink;
};

std::vector<MediaFoundationVideoSourceInfo>
EnumerateMediaFoundationVideoSourceDetails();
std::vector<std::wstring> EnumerateMediaFoundationVideoSources();

class MediaFoundationFrameSource final : public IFrameSource
{
public:
    explicit MediaFoundationFrameSource(
        std::wstring friendlyNameContains,
        std::wstring usbVidPidPair = L"");
    ~MediaFoundationFrameSource() override;

    const wchar_t* Name() const noexcept override;
    bool Start() override;
    void Stop() noexcept override;

    bool ReadFrame(
        unsigned char* Destination,
        std::size_t DestinationBytes,
        std::uint64_t& Timestamp100ns) override;

private:
    bool FindAndActivateDevice();
    bool ConfigureReader();
    bool ValidateCurrentOutputType();

    std::wstring friendlyNameContains_;
    std::wstring usbVidPidPair_;
    std::wstring selectedName_;
    bool mediaFoundationStarted_ = false;
    LONG defaultStride_ = 0;

    Microsoft::WRL::ComPtr<IMFMediaSource> mediaSource_;
    Microsoft::WRL::ComPtr<IMFSourceReader> reader_;
};
