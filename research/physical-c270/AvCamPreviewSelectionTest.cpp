#include "AvCamPreviewSelection.h"
#include <cstdint>
#include <iostream>
#include <limits>

int main() {
    using namespace avcam::preview;
    if (Fresh(0, 0) || Fresh(1000, 0) || Fresh(999, 1000)) return 1;
    if (!Fresh(1000, 1000) || !Fresh(3000, 1000) ||
        Fresh(3001, 1000)) return 2;
    if (!Fresh(std::numeric_limits<std::uint64_t>::max(),
               std::numeric_limits<std::uint64_t>::max() - 1, 2000))
        return 3;
    if (Select(false, false, Mode::Output) != Source::None ||
        Select(true, false, Mode::Output) != Source::Raw ||
        Select(true, true, Mode::Output) != Source::Obs ||
        Select(false, true, Mode::Output) != Source::Obs) return 4;
    if (Select(true, true, Mode::Raw) != Source::Raw ||
        Select(false, true, Mode::Raw) != Source::None ||
        Select(true, true, Mode::Obs) != Source::Obs ||
        Select(true, false, Mode::Obs) != Source::None) return 5;
    if (Select(true, true, Mode::Compare) != Source::None) return 6;
    std::cout << "AVCAM_PREVIEW_SELECTION_PASS\n";
    return 0;
}
