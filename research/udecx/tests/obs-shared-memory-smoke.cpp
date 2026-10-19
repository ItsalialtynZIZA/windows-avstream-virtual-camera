#include "ObsSharedMemorySource.h"
#include "VirtualCameraProtocol.h"
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <vector>
#include <cstdio>

namespace {
struct Header {
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
static_assert(sizeof(Header) == 80, "OBS queue layout changed");
constexpr std::uint32_t width = 1280;
constexpr std::uint32_t height = 720;
constexpr std::size_t nv12Bytes = static_cast<std::size_t>(width) * height * 3 / 2;
constexpr std::size_t baseOffset = 96;
constexpr std::size_t slotBytes = nv12Bytes + 32;
constexpr std::size_t mapBytes = baseOffset + slotBytes * 3;

void Publish(Header* header, unsigned char* base, std::uint32_t serial,
             unsigned char y) {
    const std::size_t offset = header->offsets[serial % 3] + 32u;
    std::memset(base + offset, y, width * height);
    std::memset(base + offset + width * height, 128, nv12Bytes - width * height);
    MemoryBarrier();
    header->writeIndex = serial;
    header->readIndex = serial;
    header->state = 2;
}
} // namespace

int main() {
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                       0, static_cast<DWORD>(mapBytes),
                                       L"OBSVirtualCamVideo");
    if (!mapping || GetLastError() == ERROR_ALREADY_EXISTS) {
        std::puts("OBS_IPC_TEST_CANNOT_CREATE_ISOLATED_QUEUE");
        if (mapping) CloseHandle(mapping);
        return 1;
    }
    auto* bytes = static_cast<unsigned char*>(
        MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0));
    if (!bytes) { CloseHandle(mapping); return 2; }
    std::memset(bytes, 0, mapBytes);
    auto* header = reinterpret_cast<Header*>(bytes);
    header->width = width;
    header->height = height;
    header->interval = 333333;
    for (std::uint32_t i = 0; i < 3; ++i)
        header->offsets[i] = static_cast<std::uint32_t>(baseOffset + i * slotBytes);
    Publish(header, bytes, 1, 16);

    ObsSharedMemorySource source;
    if (!source.Start()) {
        std::printf("OBS_IPC_START_FAILED 0x%08lX\n", static_cast<unsigned long>(source.LastErrorCode()));
        UnmapViewOfFile(bytes); CloseHandle(mapping); return 3;
    }
    std::vector<unsigned char> output(AVCAM_FRAME_BYTES_BGRA32);
    std::uint64_t timestamp = 0;
    if (!source.ReadFrame(output.data(), output.size(), timestamp) ||
        output[0] != 0 || output[1] != 0 || output[2] != 0 || output[3] != 255) {
        std::puts("OBS_IPC_BLACK_NV12_CONVERSION_FAILED");
        source.Stop(); UnmapViewOfFile(bytes); CloseHandle(mapping); return 4;
    }
    Publish(header, bytes, 2, 235);
    if (!source.ReadFrame(output.data(), output.size(), timestamp) ||
        output[0] != 255 || output[1] != 255 || output[2] != 255 ||
        output[3] != 255 || timestamp == 0) {
        std::puts("OBS_IPC_WHITE_NV12_CONVERSION_FAILED");
        source.Stop(); UnmapViewOfFile(bytes); CloseHandle(mapping); return 5;
    }
    source.Stop();
    UnmapViewOfFile(bytes);
    CloseHandle(mapping);
    std::puts("AVCAM_OBS_SHARED_MEMORY_BLACK_WHITE_FRAMES_PASSED");
    return 0;
}
