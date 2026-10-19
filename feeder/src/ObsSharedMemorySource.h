#pragma once
#include "FrameSource.h"
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <vector>

// Reads the native OBS Studio virtual-camera NV12 frame queue without DirectShow.
// The queue is published as the per-session OBSVirtualCamVideo file mapping.
class ObsSharedMemorySource final : public IFrameSource {
public:
    ~ObsSharedMemorySource() override { Stop(); }
    const wchar_t* Name() const noexcept override { return L"OBS Virtual Camera (shared memory)"; }
    bool Start() override;
    void Stop() noexcept override;
    bool ReadFrame(unsigned char* dst, std::size_t size, std::uint64_t& timestamp) override;
    DWORD LastErrorCode() const noexcept { return errorCode_; }
    const wchar_t* FailureStage() const noexcept { return failureStage_; }
private:
    HANDLE mapping_ = nullptr;
    const unsigned char* view_ = nullptr;
    std::size_t mappedBytes_ = 0;
    std::vector<unsigned char> nv12_;
    std::uint32_t lastIndex_ = 0;
    bool haveLastIndex_ = false;
    DWORD errorCode_ = 0;
    const wchar_t* failureStage_ = L"";
};