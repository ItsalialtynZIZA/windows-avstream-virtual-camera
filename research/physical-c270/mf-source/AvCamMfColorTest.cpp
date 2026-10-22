#include "AvCamMfColor.h"
#include <iostream>
#include <vector>
int main() {
    using namespace avcam::mfcolor;
    std::vector<std::uint8_t> bgra(BgraBytes, 0);
    std::vector<std::uint8_t> out(Nv12Bytes, 0);
    if (Convert(nullptr, BgraBytes, out.data(), Nv12Bytes) ||
        Convert(bgra.data(), BgraBytes-1, out.data(), Nv12Bytes) ||
        Convert(bgra.data(), BgraBytes, out.data(), Nv12Bytes-1))
        return 1;
    // Every pixel black. BT.601 limited-range: Y=16, U=V=128.
    if (!Convert(bgra.data(), bgra.size(), out.data(), out.size()) ||
        out.front() != 16 || out[Width*Height-1] != 16 ||
        out[Width*Height] != 128 || out.back() != 128)
        return 2;
    // RGB32 buffer of solid red, ignores alpha, 4:2:0 subsampling.
    for (std::size_t i = 0; i < bgra.size(); i += 4) {
        bgra[i + 2] = 255;
        bgra[i + 3] = static_cast<unsigned char>(i & 0xff);
    }
    if (!Convert(bgra.data(), bgra.size(), out.data(), out.size()) ||
        out.front() != 82 || out[Width*Height-1] != 82 ||
        out[Width*Height] != 90 || out[Width*Height+1] != 240)
        return 3;
    // Red channel is private OBS input; no synthetic camera output exists
    // when a producer has not supplied a sample.
    std::cout << "AVCAM_MF_BGRA_NV12_CONVERSION_PASS\n";
    return 0;
}
