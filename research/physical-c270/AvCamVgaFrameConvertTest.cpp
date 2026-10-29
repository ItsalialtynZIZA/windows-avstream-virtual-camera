// Portable regression: no camera, driver, COM or registry operations.
#include "AvCamVgaFrameConvert.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>
int main() {
    using namespace avcam::vga;
    std::vector<std::uint8_t> source(kSrcNv12Bytes);
    for (std::size_t y = 0; y < kSrcHeight; ++y)
        for (std::size_t x = 0; x < kSrcWidth; ++x)
            source[y * kSrcWidth + x] = static_cast<std::uint8_t>((x / 8 + y / 8) & 255);
    for (std::size_t y = 0; y < kSrcHeight / 2; ++y)
        for (std::size_t x = 0; x < kSrcWidth; x += 2) {
            source[kSrcWidth * kSrcHeight + y * kSrcWidth + x] =
                static_cast<std::uint8_t>((x / 2) & 255);
            source[kSrcWidth * kSrcHeight + y * kSrcWidth + x + 1] =
                static_cast<std::uint8_t>(y & 255);
        }
    std::vector<std::uint8_t> out;
    assert(!ConvertFrom720pNv12(nullptr, source.size(), Format::Nv12, out));
    assert(!ConvertFrom720pNv12(source.data(), source.size() - 1, Format::Nv12, out));
    assert(out.empty());
    assert(ConvertFrom720pNv12(source.data(), source.size(), Format::Nv12, out));
    assert(out.size() == kNv12Bytes);
    assert(out[0] == source[kCropLeft]); // 16:9 to centred 4:3 crop
    assert(out[kWidth * kHeight - 1] ==
           source[((kHeight - 1) * kSrcHeight / kHeight) * kSrcWidth +
                  kCropLeft + ((kWidth - 1) * kCropWidth / kWidth)]);
    assert(out[kWidth * kHeight] == source[kSrcWidth * kSrcHeight + kCropLeft]);
    assert(out[kWidth * kHeight + 1] == source[kSrcWidth * kSrcHeight + kCropLeft + 1]);
    assert(ConvertFrom720pNv12(source.data(), source.size(), Format::Yuy2, out));
    assert(out.size() == kYuy2Bytes);
    assert(out[0] == source[kCropLeft]);
    assert(out[1] == source[kSrcWidth * kSrcHeight + kCropLeft]);
    assert(out[2] == source[kCropLeft + 1]);
    assert(out[3] == source[kSrcWidth * kSrcHeight + kCropLeft + 1]);
    assert(out[kYuy2Bytes - 4] == source[((kHeight - 1) * kSrcHeight / kHeight) * kSrcWidth +
            kCropLeft + ((kWidth - 2) * kCropWidth / kWidth)]);
    std::cout << "AVCAM_OBS_VGA_NV12_CROP_PASS\n"
                 "AVCAM_OBS_VGA_YUY2_PACK_PASS\n"
                 "AVCAM_OBS_VGA_MALFORMED_INPUT_REJECT_PASS\n";
    return 0;
}
