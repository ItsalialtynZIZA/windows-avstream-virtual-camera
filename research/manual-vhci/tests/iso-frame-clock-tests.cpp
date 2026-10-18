#include <cstdint>
#include <iostream>
#include <stdexcept>
#include "../shared/AvCamIsoFrameClock.h"

static void Check(bool ok, const char* msg)
{
    if (!ok) throw std::runtime_error(msg);
}

int main()
{
    try {
        constexpr unsigned long long interval = 333333ull;
        constexpr unsigned long long urbInterval =
            128ull * AVCAM_ISO_MICROFRAME_100NS;
        const unsigned long long start = 50000000ull;
        unsigned long long next = 0;
        unsigned int frames = 0;
        unsigned long long lastFrame = 0;
        // Simulate 10 seconds of URBs with exact USB microframe positions.
        for (unsigned int urb = 0; urb < 625; ++urb) {
            const unsigned long long base = start + urb * urbInterval;
            bool emitted = false;
            for (unsigned int packet = 0; packet < 128; ++packet) {
                const unsigned long long now = base +
                    packet * AVCAM_ISO_MICROFRAME_100NS;
                if (!emitted && now >= next) {
                    Check(frames == 0 || now > lastFrame,
                          "Frame timestamps must strictly increase");
                    lastFrame = now;
                    next = AvCamIsoAdvanceFrameDue(next, now, interval);
                    Check(next > now, "Next frame due must remain in the future");
                    emitted = true;
                    ++frames;
                }
            }
        }
        Check(frames >= 299 && frames <= 302,
              "10-second 30fps microframe scheduler drifted");
        Check(AvCamIsoAdvanceFrameDue(1000000, 2100000, interval) > 2100000,
              "Late frame must skip missed deadlines");
        Check(AvCamIsoAdvanceFrameDue(1000000, 2100000, interval)
              <= 2100000 + interval, "Late frame must not build backlog");
        Check(AvCamIsoAdvanceFrameDue(0, 1500000, interval)
              == 1500000 + interval, "First frame due mismatch");
        // Returned ASAP StartFrame is the submission/start time, not
        // the frame observed after a 16ms URB has completed.
        Check(AvCamIsoUsbFrameNumber(10ull * AVCAM_ISO_USB_FRAME_100NS) == 10u,
              "ASAP start frame mismatch");
        Check(AvCamIsoUsbFrameNumber(2050ull * AVCAM_ISO_USB_FRAME_100NS) == 2u,
              "ASAP frame wraparound mismatch");
        const auto began = AvCamIsoUsbFrameNumber(
            1234ull * AVCAM_ISO_USB_FRAME_100NS);
        const auto ended = AvCamIsoUsbFrameNumber(
            (1234ull + 16ull) * AVCAM_ISO_USB_FRAME_100NS);
        Check(began == 1234u && ended == 1250u && began != ended,
              "ASAP start frame must not come from completion time");
        std::cout << "ISO 30fps microframe pacing tests passed: "
                  << frames << " frames in 10s.\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
