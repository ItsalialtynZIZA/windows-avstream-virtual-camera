// Portable pure-pixel tests: no camera, registry, COM, MF or Windows headers.
#include "AvCamAdaptiveFrameConvert.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>
using namespace avcam::adaptive;

int main() {
    std::vector<std::uint8_t> input(kSourceBytes, 128);
    // Synthetic neutral gray, plus a visible left-to-right gradient and
    // separate U/V values: detect crop, chroma order and output byte order.
    for (std::size_t y = 0; y < kSourceHeight; ++y)
        for (std::size_t x = 0; x < kSourceWidth; ++x)
            input[y * kSourceWidth + x] = static_cast<std::uint8_t>(16 + (x / 16) % 200);
    for (std::size_t y = 0; y < kSourceHeight / 2; ++y)
        for (std::size_t x = 0; x < kSourceWidth; x += 2) {
            input[kSourceWidth * kSourceHeight + y * kSourceWidth + x] = 90;
            input[kSourceWidth * kSourceHeight + y * kSourceWidth + x + 1] = 150;
        }
    const Format formats[] = {
        Format::Nv12, Format::I420, Format::Yuy2, Format::Uyvy,
        Format::Bgr24, Format::Bgr32
    };
    const std::uint32_t sizes[][2] = {
        {320,240}, {640,480}, {1280,720}, {1920,1080},
        {800,600}, {640,360}, {1920,1080}
    };
    for (const auto& dims : sizes) {
        for (auto format : formats) {
            const Spec spec{dims[0], dims[1], format, false};
            std::size_t required = 0;
            assert(RequiredBytes(spec, required));
            std::vector<std::uint8_t> output;
            assert(Convert(input.data(), input.size(), spec, output));
            assert(output.size() == required);
            // Re-run: deterministic bytes and allocation bounds.
            std::vector<std::uint8_t> duplicate;
            assert(Convert(input.data(), input.size(), spec, duplicate));
            assert(output == duplicate);
        }
    }
    std::vector<std::uint8_t> output;
    assert(!Convert(nullptr, input.size(), {640,480,Format::Nv12}, output));
    assert(!Convert(input.data(), input.size() - 1, {640,480,Format::Nv12}, output));
    assert(!Convert(input.data(), input.size(), {641,480,Format::Nv12}, output));
    assert(!Convert(input.data(), input.size(), {640,479,Format::Nv12}, output));
    assert(!Convert(input.data(), input.size(), {3840,2160,Format::Nv12}, output));
    assert(!Convert(input.data(), input.size(), {0,480,Format::Nv12}, output));
    assert(!Convert(input.data(), input.size(), {640,0,Format::Nv12}, output));
    assert(output.empty());

    // 4:3 crop means first source x is 160, not x=0; exact Y mapping.
    assert(Convert(input.data(), input.size(), {640,480,Format::Nv12}, output));
    assert(output[0] == input[160]);
    assert(output[640 * 480] == 90 && output[640 * 480 + 1] == 150);
    assert(Convert(input.data(), input.size(), {640,480,Format::I420}, output));
    assert(output[640 * 480] == 90);
    assert(output[640 * 480 + (640 * 480 / 4)] == 150);
    assert(Convert(input.data(), input.size(), {640,480,Format::Yuy2}, output));
    assert(output[0] == input[160]);
    assert(output[1] == 90 && output[3] == 150);
    assert(Convert(input.data(), input.size(), {640,480,Format::Uyvy}, output));
    assert(output[0] == 90 && output[2] == 150);
    assert(output[1] == input[160]);
    assert(Convert(input.data(), input.size(), {640,480,Format::Bgr32}, output));
    assert(output.size() == 640u * 480u * 4u && output[3] == 255);
    const auto topLeft = output[0];
    assert(Convert(input.data(), input.size(), {640,480,Format::Bgr32,true}, output));
    assert(output[(480u-1u)*640u*4u] == topLeft);
    std::cout << "AVCAM_V2_FORMATS_SIX_RAW_PASS\n"
              << "AVCAM_V2_RESOLUTIONS_SEVEN_PASS\n"
              << "AVCAM_V2_BOUNDS_AND_CROP_PASS\n"
              << "AVCAM_V2_YUV_PACKING_AND_BGR_PASS\n";
    return 0;
}
