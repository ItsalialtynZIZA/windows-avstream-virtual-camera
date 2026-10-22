#pragma once
// Strict OBS-only BGRA32 to planar NV12 for 1280x720 Windows MF video.
// CPU reference implementation; no camera, COM, registry, or driver access.
#include <cstddef>
#include <cstdint>
#include <vector>
namespace avcam::mfcolor {
constexpr std::size_t Width = 1280;
constexpr std::size_t Height = 720;
constexpr std::size_t BgraBytes = Width * Height * 4;
constexpr std::size_t Nv12Bytes = Width * Height * 3 / 2;

inline std::uint8_t Clamp(int v) noexcept {
    return static_cast<std::uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
}
inline void ConvertPixel(const std::uint8_t* bgra,
                         std::uint8_t& y, std::uint8_t& u,
                         std::uint8_t& v) noexcept {
    const int b = bgra[0], g = bgra[1], r = bgra[2];
    // BT.601 studio-range fixed-point transform, alpha intentionally ignored.
    y = Clamp(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
    u = Clamp(((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128);
    v = Clamp(((112 * r - 94 * g - 18 * b + 128) >> 8) + 128);
}
inline bool Convert(const std::uint8_t* bgra, std::size_t bgraSize,
                    std::uint8_t* nv12, std::size_t nv12Size) noexcept {
    if (!bgra || !nv12 || bgraSize != BgraBytes || nv12Size < Nv12Bytes)
        return false;
    auto* yPlane = nv12;
    auto* uvPlane = nv12 + Width * Height;
    for (std::size_t row = 0; row < Height; row += 2) {
        for (std::size_t col = 0; col < Width; col += 2) {
            int sumU = 0, sumV = 0;
            for (std::size_t dy = 0; dy < 2; ++dy) {
                const auto srcRow = row + dy;
                for (std::size_t dx = 0; dx < 2; ++dx) {
                    const auto srcCol = col + dx;
                    std::uint8_t y = 0, u = 0, v = 0;
                    ConvertPixel(bgra + (srcRow * Width + srcCol) * 4, y, u, v);
                    yPlane[srcRow * Width + srcCol] = y;
                    sumU += u;
                    sumV += v;
                }
            }
            const std::size_t uv = (row / 2) * Width + col;
            uvPlane[uv] = static_cast<std::uint8_t>((sumU + 2) / 4);
            uvPlane[uv + 1] = static_cast<std::uint8_t>((sumV + 2) / 4);
        }
    }
    return true;
}
} // namespace avcam::mfcolor
