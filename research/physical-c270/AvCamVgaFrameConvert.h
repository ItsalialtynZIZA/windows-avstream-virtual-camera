#pragma once
// Pixel-only conversion for the opt-in two-camera Device MFT laboratory.
// OBS publishes 1280x720 NV12; some Chrome camera sessions negotiate VGA.
// Crop the centre of the 16:9 image to 4:3 before resampling to 640x480.
// No camera identity, COM, registry, driver or browser modifications here.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace avcam::vga {
constexpr std::size_t kSrcWidth = 1280;
constexpr std::size_t kSrcHeight = 720;
constexpr std::size_t kSrcNv12Bytes = kSrcWidth * kSrcHeight * 3 / 2;
constexpr std::size_t kWidth = 640;
constexpr std::size_t kHeight = 480;
constexpr std::size_t kCropLeft = 160;
constexpr std::size_t kCropWidth = 960;
constexpr std::size_t kNv12Bytes = kWidth * kHeight * 3 / 2;
constexpr std::size_t kYuy2Bytes = kWidth * kHeight * 2;
enum class Format { Nv12, Yuy2 };

// Preserve a valid physical frame if the source is missing/malformed.
// Output vectors are filled completely, with no uninitialized pixel bytes.
inline bool ConvertFrom720pNv12(const std::uint8_t* src,
                               std::size_t bytes,
                               Format format,
                               std::vector<std::uint8_t>& dst) {
    if (!src || bytes != kSrcNv12Bytes) return false;
    if (format != Format::Nv12 && format != Format::Yuy2) return false;

    const auto sampleX = [](std::size_t x) {
        return kCropLeft + (x * kCropWidth) / kWidth;
    };
    const auto sampleY = [](std::size_t y) {
        return (y * kSrcHeight) / kHeight;
    };
    const auto sampleUv = [&](std::size_t x, std::size_t y,
                              bool v) -> std::uint8_t {
        const std::size_t sx = sampleX(x) & ~std::size_t(1);
        const std::size_t sy = sampleY(y) / 2;
        return src[kSrcWidth * kSrcHeight + sy * kSrcWidth + sx + (v ? 1 : 0)];
    };

    if (format == Format::Nv12) {
        dst.resize(kNv12Bytes);
        for (std::size_t y = 0; y < kHeight; ++y) {
            for (std::size_t x = 0; x < kWidth; ++x) {
                dst[y * kWidth + x] = src[sampleY(y) * kSrcWidth + sampleX(x)];
            }
        }
        for (std::size_t y = 0; y < kHeight / 2; ++y) {
            for (std::size_t x = 0; x < kWidth; x += 2) {
                const std::size_t i = kWidth * kHeight + y * kWidth + x;
                dst[i] = sampleUv(x, y * 2, false);
                dst[i + 1] = sampleUv(x, y * 2, true);
            }
        }
    } else {
        dst.resize(kYuy2Bytes);
        for (std::size_t y = 0; y < kHeight; ++y) {
            for (std::size_t x = 0; x < kWidth; x += 2) {
                const std::size_t i = (y * kWidth + x) * 2;
                dst[i] = src[sampleY(y) * kSrcWidth + sampleX(x)];
                dst[i + 1] = sampleUv(x, y, false);
                dst[i + 2] = src[sampleY(y) * kSrcWidth + sampleX(x + 1)];
                dst[i + 3] = sampleUv(x, y, true);
            }
        }
    }
    return true;
}
}  // namespace avcam::vga
