#pragma once
// AvCam v2: bounded pixel conversion only. No COM, webcam, registry or driver
// operations. Produces a tightly packed destination from fresh 1280x720 NV12.
// Used by an explicitly opt-in laboratory MFT; never changes the stable v1.
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace avcam::adaptive {
constexpr std::uint32_t kSourceWidth = 1280;
constexpr std::uint32_t kSourceHeight = 720;
constexpr std::size_t kSourceBytes = std::size_t(kSourceWidth) * kSourceHeight * 3 / 2;
constexpr std::uint32_t kMaxWidth = 1920;
constexpr std::uint32_t kMaxHeight = 1080;
enum class Format { Nv12, I420, Yuy2, Uyvy, Bgr24, Bgr32 };

struct Spec {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    Format format = Format::Nv12;
    // RGB DIB consumers may request bottom-up rows. False = top-down.
    bool bottomUp = false;
};

inline bool RequiredBytes(const Spec& spec, std::size_t& result) {
    result = 0;
    if (spec.width < 2 || spec.height < 2 ||
        spec.width > kMaxWidth || spec.height > kMaxHeight ||
        (spec.width & 1u) || (spec.height & 1u))
        return false;
    const std::size_t pixels = std::size_t(spec.width) * spec.height;
    switch (spec.format) {
        case Format::Nv12: case Format::I420: result = pixels * 3 / 2; break;
        case Format::Yuy2: case Format::Uyvy: result = pixels * 2; break;
        case Format::Bgr24: result = pixels * 3; break;
        case Format::Bgr32: result = pixels * 4; break;
        default: return false;
    }
    return result > 0 && result <= std::size_t(kMaxWidth) * kMaxHeight * 4;
}
inline std::uint8_t Clamp(int value) {
    return static_cast<std::uint8_t>(value < 0 ? 0 : (value > 255 ? 255 : value));
}
inline void ToBgr(std::uint8_t y, std::uint8_t u, std::uint8_t v,
                  std::uint8_t* dest) {
    // Limited-range BT.709 YUV coefficients; bounded integer intermediate.
    const int c = (int(y) - 16) > 0 ? (int(y) - 16) : 0;
    const int d = int(u) - 128, e = int(v) - 128;
    dest[0] = Clamp((298 * c + 541 * d + 128) >> 8);
    dest[1] = Clamp((298 * c - 55 * d - 136 * e + 128) >> 8);
    dest[2] = Clamp((298 * c + 459 * e + 128) >> 8);
}
inline bool Convert(const std::uint8_t* source, std::size_t sourceBytes,
                    const Spec& spec, std::vector<std::uint8_t>& destination) {
    std::size_t size = 0;
    if (!source || sourceBytes != kSourceBytes || !RequiredBytes(spec, size))
        return false;
    const std::size_t sw = kSourceWidth, sh = kSourceHeight;
    const std::size_t dw = spec.width, dh = spec.height;
    std::size_t cropW = sw, cropH = sh;
    // Preserve target aspect ratio with a central even-aligned crop.
    if (sw * dh > sh * dw) {
        cropW = ((sh * dw / dh) & ~std::size_t(1));
    } else if (sw * dh < sh * dw) {
        cropH = ((sw * dh / dw) & ~std::size_t(1));
    }
    if (cropW < 2 || cropH < 2) return false;
    const std::size_t cropX = ((sw - cropW) / 2) & ~std::size_t(1);
    const std::size_t cropY = ((sh - cropH) / 2) & ~std::size_t(1);
    auto sx = [&](std::size_t x) { return cropX + x * cropW / dw; };
    auto sy = [&](std::size_t y) { return cropY + y * cropH / dh; };
    const auto uv = source + sw * sh;
    auto sampleU = [&](std::size_t x, std::size_t y) {
        const std::size_t xx = sx(x) & ~std::size_t(1);
        const std::size_t yy = sy(y) & ~std::size_t(1);
        return uv[(yy / 2) * sw + xx];
    };
    auto sampleV = [&](std::size_t x, std::size_t y) {
        const std::size_t xx = sx(x) & ~std::size_t(1);
        const std::size_t yy = sy(y) & ~std::size_t(1);
        return uv[(yy / 2) * sw + xx + 1];
    };
    std::vector<std::uint8_t> out(size, 0);
    const std::size_t yPlane = dw * dh;
    if (spec.format == Format::Nv12 || spec.format == Format::I420) {
        for (std::size_t y = 0; y < dh; ++y)
            for (std::size_t x = 0; x < dw; ++x)
                out[y * dw + x] = source[sy(y) * sw + sx(x)];
        for (std::size_t y = 0; y < dh / 2; ++y) {
            for (std::size_t x = 0; x < dw / 2; ++x) {
                const std::uint8_t u = sampleU(x * 2, y * 2);
                const std::uint8_t v = sampleV(x * 2, y * 2);
                if (spec.format == Format::Nv12) {
                    const std::size_t i = yPlane + y * dw + x * 2;
                    out[i] = u;
                    out[i + 1] = v;
                } else {
                    const std::size_t i = y * (dw / 2) + x;
                    out[yPlane + i] = u;
                    out[yPlane + (dw * dh / 4) + i] = v;
                }
            }
        }
    } else if (spec.format == Format::Yuy2 || spec.format == Format::Uyvy) {
        for (std::size_t y = 0; y < dh; ++y)
            for (std::size_t x = 0; x < dw; x += 2) {
                const std::uint8_t y0 = source[sy(y) * sw + sx(x)];
                const std::uint8_t y1 = source[sy(y) * sw + sx(x + 1)];
                const std::uint8_t u = sampleU(x, y);
                const std::uint8_t v = sampleV(x, y);
                const std::size_t i = (y * dw + x) * 2;
                if (spec.format == Format::Yuy2) {
                    out[i] = y0; out[i + 1] = u;
                    out[i + 2] = y1; out[i + 3] = v;
                } else {
                    out[i] = u; out[i + 1] = y0;
                    out[i + 2] = v; out[i + 3] = y1;
                }
            }
    } else {
        const std::size_t pixelSize = spec.format == Format::Bgr24 ? 3 : 4;
        for (std::size_t y = 0; y < dh; ++y) {
            const std::size_t row = spec.bottomUp ? dh - 1 - y : y;
            for (std::size_t x = 0; x < dw; ++x) {
                auto* dest = &out[(row * dw + x) * pixelSize];
                ToBgr(source[sy(y) * sw + sx(x)], sampleU(x, y),
                      sampleV(x, y), dest);
                if (pixelSize == 4) dest[3] = 0xFF;
            }
        }
    }
    destination.swap(out); // Only publish a fully built frame.
    return true;
}
} // namespace avcam::adaptive
