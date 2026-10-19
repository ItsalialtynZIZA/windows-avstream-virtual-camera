#pragma once
// AvCam Physical C270 research bridge ABI, revision 1.
// This cross-session page-file mapping transports JPEG only; it does not
// emulate USB topology or change the physical camera's USB descriptors.
#include <windows.h>
#include <cstdint>

namespace AvCamPhysical {
inline constexpr wchar_t kMappingName[] = L"Global\\AvCamPhysicalMjpegV1";
inline constexpr std::uint32_t kMagic = 0x50435641u;  // AVCP
inline constexpr std::uint32_t kVersion = 1u;
inline constexpr std::uint32_t kWidth = 1280u;
inline constexpr std::uint32_t kHeight = 720u;
inline constexpr std::uint32_t kCapacity = 2u * 1024u * 1024u;
struct FrameHeader {
    volatile LONG sequence;  // odd = writer active; even = stable
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t length;
    std::uint64_t timestamp;
};
static_assert(sizeof(FrameHeader) == 32, "Unexpected physical bridge ABI");
inline constexpr std::uint32_t kTotalBytes =
    static_cast<std::uint32_t>(sizeof(FrameHeader)) + kCapacity;
}  // namespace AvCamPhysical
