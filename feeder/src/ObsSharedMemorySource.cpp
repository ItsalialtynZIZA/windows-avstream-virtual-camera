#include "ObsSharedMemorySource.h"
#include "VirtualCameraProtocol.h"
#include <algorithm>
#include <cstring>
#include <limits>

namespace {
constexpr wchar_t kObsQueueName[] = L"OBSVirtualCamVideo";
constexpr std::uint32_t kReady = 2;
constexpr std::size_t kFramePrefix = 32;

// Wire layout from OBS Studio plugins/win-dshow/shared-memory-queue.c.
// No OBS code or library is linked into AvCam: only the published IPC layout.
struct QueueHeader {
    volatile std::uint32_t writeIndex;
    volatile std::uint32_t readIndex;
    volatile std::uint32_t state;
    std::uint32_t offsets[3];
    std::uint32_t type;
    std::uint32_t width;
    std::uint32_t height;
    std::uint64_t interval;
    std::uint32_t reserved[8];
};
static_assert(sizeof(QueueHeader) == 80, "Unexpected OBS queue ABI layout");

unsigned char ClipToByte(int value) {
    return static_cast<unsigned char>(std::clamp(value, 0, 255));
}
void Nv12ToBgra(const unsigned char* nv12, std::uint32_t srcWidth,
                std::uint32_t srcHeight, unsigned char* bgra) {
    const unsigned char* yPlane = nv12;
    const unsigned char* uvPlane = nv12 + static_cast<std::size_t>(srcWidth) * srcHeight;
    for (unsigned dy = 0; dy < AVCAM_FRAME_HEIGHT; ++dy) {
        const std::uint32_t sy = static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(dy) * srcHeight) / AVCAM_FRAME_HEIGHT);
        const unsigned char* yRow = yPlane + static_cast<std::size_t>(sy) * srcWidth;
        const unsigned char* uvRow = uvPlane + static_cast<std::size_t>(sy / 2) * srcWidth;
        unsigned char* dstRow = bgra + static_cast<std::size_t>(dy) * AVCAM_FRAME_STRIDE_BGRA32;
        for (unsigned dx = 0; dx < AVCAM_FRAME_WIDTH; ++dx) {
            const std::uint32_t sx = static_cast<std::uint32_t>(
                (static_cast<std::uint64_t>(dx) * srcWidth) / AVCAM_FRAME_WIDTH);
            const int yy = std::max(0, static_cast<int>(yRow[sx]) - 16);
            const std::size_t uv = static_cast<std::size_t>(sx & ~1u);
            const int uu = static_cast<int>(uvRow[uv]) - 128;
            const int vv = static_cast<int>(uvRow[uv + 1]) - 128;
            // Limited-range BT.709, matching the normal OBS HD NV12 output.
            dstRow[dx * 4 + 0] = ClipToByte((298 * yy + 541 * uu + 128) >> 8);
            dstRow[dx * 4 + 1] = ClipToByte((298 * yy - 55 * uu - 136 * vv + 128) >> 8);
            dstRow[dx * 4 + 2] = ClipToByte((298 * yy + 459 * vv + 128) >> 8);
            dstRow[dx * 4 + 3] = 255;
        }
    }
}
bool CheckDimensions(std::uint32_t width, std::uint32_t height) {
    return width >= 16 && height >= 16 && width <= 3840 &&
           height <= 2160 && (width % 2 == 0) && (height % 2 == 0);
}
} // namespace

bool ObsSharedMemorySource::Start() {
    Stop();
    errorCode_ = 0;
    failureStage_ = L"";
    // OBS can publish the mapping shortly after the virtual camera is enabled.
    DWORD failure = ERROR_FILE_NOT_FOUND;
    for (int attempt = 0; attempt < 60; ++attempt) {
        mapping_ = OpenFileMappingW(FILE_MAP_READ, FALSE, kObsQueueName);
        if (mapping_) break;
        failure = GetLastError();
        if (failure == ERROR_ACCESS_DENIED) break;
        Sleep(50);
    }
    if (!mapping_) {
        failureStage_ = L"OBS virtual camera queue missing/access denied";
        errorCode_ = static_cast<DWORD>(HRESULT_FROM_WIN32(failure));
        return false;
    }
    view_ = static_cast<const unsigned char*>(
        MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, 0));
    if (!view_) {
        const DWORD failureCode = GetLastError();
        failureStage_ = L"OBS queue mapping failed";
        errorCode_ = static_cast<DWORD>(HRESULT_FROM_WIN32(failureCode));
        Stop();
        return false;
    }
    MEMORY_BASIC_INFORMATION memory{};
    if (!VirtualQuery(view_, &memory, sizeof(memory))) {
        const DWORD failureCode = GetLastError();
        failureStage_ = L"OBS queue view length unavailable";
        errorCode_ = static_cast<DWORD>(HRESULT_FROM_WIN32(failureCode));
        Stop();
        return false;
    }
    mappedBytes_ = memory.RegionSize;
    if (mappedBytes_ < sizeof(QueueHeader)) {
        failureStage_ = L"OBS queue header too short";
        errorCode_ = static_cast<DWORD>(HRESULT_FROM_WIN32(ERROR_BAD_FORMAT));
        Stop();
        return false;
    }
    return true;
}

void ObsSharedMemorySource::Stop() noexcept {
    if (view_) UnmapViewOfFile(view_);
    if (mapping_) CloseHandle(mapping_);
    view_ = nullptr;
    mapping_ = nullptr;
    mappedBytes_ = 0;
    nv12_.clear();
    lastIndex_ = 0;
    haveLastIndex_ = false;
}

bool ObsSharedMemorySource::ReadFrame(unsigned char* dst, std::size_t size,
                                     std::uint64_t& timestamp) {
    if (!view_ || !dst || size < AVCAM_FRAME_BYTES_BGRA32) {
        failureStage_ = L"OBS queue read prerequisites";
        errorCode_ = static_cast<DWORD>(E_INVALIDARG);
        return false;
    }
    auto* header = reinterpret_cast<const QueueHeader*>(view_);
    for (int attempt = 0; attempt < 300; ++attempt) {
        if (header->state != kReady) {
            Sleep(10);
            continue;
        }
        MemoryBarrier();
        const std::uint32_t width = header->width;
        const std::uint32_t height = header->height;
        if (!CheckDimensions(width, height)) {
            failureStage_ = L"OBS queue invalid NV12 dimensions";
            errorCode_ = static_cast<DWORD>(HRESULT_FROM_WIN32(ERROR_BAD_FORMAT));
            return false;
        }
        const std::size_t pixels = static_cast<std::size_t>(width) * height;
        const std::size_t frameBytes = pixels + pixels / 2;
        const std::uint32_t index = header->readIndex;
        if (haveLastIndex_ && index == lastIndex_) {
            Sleep(10);
            continue;
        }
        const std::uint32_t frameSlot = index % 3u;
        const std::size_t offset = static_cast<std::size_t>(header->offsets[frameSlot]);
        if (offset > mappedBytes_ || kFramePrefix > mappedBytes_ - offset ||
            frameBytes > mappedBytes_ - offset - kFramePrefix) {
            failureStage_ = L"OBS queue frame outside mapped view";
            errorCode_ = static_cast<DWORD>(HRESULT_FROM_WIN32(ERROR_BAD_FORMAT));
            return false;
        }
        nv12_.resize(frameBytes);
        const unsigned char* source = view_ + offset + kFramePrefix;
        std::memcpy(nv12_.data(), source, frameBytes);
        MemoryBarrier();
        if (index != header->readIndex || header->state != kReady) {
            // A new frame became available while copying: retry the latest one.
            continue;
        }
        Nv12ToBgra(nv12_.data(), width, height, dst);
        lastIndex_ = index;
        haveLastIndex_ = true;
        timestamp = static_cast<std::uint64_t>(GetTickCount64()) * 10000ull;
        return true;
    }
    failureStage_ = L"OBS queue has no new frames";
    errorCode_ = static_cast<DWORD>(HRESULT_FROM_WIN32(ERROR_TIMEOUT));
    return false;
}
