#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "../shared/AvCamUvcPayload.h"

static void Require(bool ok, const char* message)
{
    if (!ok) {
        throw std::runtime_error(message);
    }
}

int main()
{
    try {
        // Two packets from one frame: only the second packet gets EOF.
        const unsigned char jpeg[] = {0xFF, 0xD8, 1, 2, 3, 4, 0xFF, 0xD9};
        unsigned char output[16]{};
        AVCAM_UVC_PACKETIZER state{};
        AvCamUvcPacketizerReset(&state);
        unsigned int actual = 42;
        int rc = AvCamUvcPacketizeMjpeg(&state, jpeg, sizeof(jpeg),
                                       output, 6, &actual);
        Require(rc == AVCAM_UVC_PACKET_OK && actual == 6,
                "First packet must contain 2 header + 4 image bytes.");
        Require(output[0] == 2 && output[1] == 0x80 &&
                output[2] == 0xFF && output[3] == 0xD8 &&
                state.Offset == 4 && state.FramesCompleted == 0,
                "First packet header, data, and offset mismatch.");

        rc = AvCamUvcPacketizeMjpeg(&state, jpeg, sizeof(jpeg),
                                    output, 6, &actual);
        Require(rc == AVCAM_UVC_PACKET_OK && actual == 6 &&
                output[1] == 0x82 && output[4] == 0xFF &&
                output[5] == 0xD9 && state.Offset == 0 &&
                state.FrameId == 1 && state.FramesCompleted == 1,
                "End-of-frame and frame identifier must change at EOF.");

        rc = AvCamUvcPacketizeMjpeg(&state, jpeg, sizeof(jpeg),
                                    output, sizeof(output), &actual);
        Require(rc == AVCAM_UVC_PACKET_OK && actual == 10 &&
                output[1] == 0x83 && state.FrameId == 0 &&
                state.FramesCompleted == 2,
                "Next frame must have toggled FID and EOF.");

        // A real frame should be spread across approximately 267 USB
        // microframes (33.3 ms at 125 us), not sent in a short burst.
        const unsigned int cap = AvCamUvcPacedPacketCapacity(
            42738u, AVCAM_UVC_PAYLOAD_MAX_BYTES, 333333u, 1250u);
        Require(cap == 163u,
                "42KB MJPEG should use approximately 161 image bytes per microframe");
        Require(AvCamUvcPacedPacketCapacity(1u, 3072u, 333333u, 1250u) == 3u,
                "Minimum JPEG size requires two-byte header and one data byte");
        Require(AvCamUvcPacedPacketCapacity(42738u, 100u, 333333u, 1250u) == 100u,
                "Frame pacing must respect caller's packet buffer bound");
        Require(AvCamUvcPacedPacketCapacity(42738u, 32768u, 333333u, 1250u) == 163u,
                "Frame pacing must ignore oversize physical capacity");
        Require(AvCamUvcPacedPacketCapacity(0u, 3072u, 333333u, 1250u) == 0u,
                "Zero-length frames must not produce ISO packets");
        Require(AvCamUvcPacedPacketCapacity(42738u, 2u, 333333u, 1250u) == 0u,
                "Insufficient space for UVC packet must be rejected");
        Require(AvCamUvcPacedPacketCapacity(42738u, 3072u, 0u, 1250u) == 0u,
                "Frame interval must not be zero");

        // Preserve destination sentinel and reset actualLength on failures.
        unsigned char sentinel[4]{0xAA, 0xBB, 0xCC, 0xDD};
        actual = 777;
        rc = AvCamUvcPacketizeMjpeg(&state, jpeg, sizeof(jpeg),
                                    sentinel, 2, &actual);
        Require(rc == AVCAM_UVC_PACKET_INVALID && actual == 0 &&
                sentinel[0] == 0xAA && sentinel[3] == 0xDD,
                "Short output buffer must remain untouched.");
        rc = AvCamUvcPacketizeMjpeg(&state, jpeg, 0,
                                    sentinel, 4, &actual);
        Require(rc == AVCAM_UVC_PACKET_INVALID && actual == 0,
                "Empty frame must be rejected.");
        state.Offset = sizeof(jpeg);
        rc = AvCamUvcPacketizeMjpeg(&state, jpeg, sizeof(jpeg),
                                    sentinel, 4, &actual);
        Require(rc == AVCAM_UVC_PACKET_INVALID && actual == 0,
                "Out-of-range frame offset must be rejected.");

        // Real advertised MJPEG max: ensure no packet exceeds the USB capacity.
        std::vector<unsigned char> image(AVCAM_UVC_FRAME_MAX_BYTES, 0x23);
        std::vector<unsigned char> rebuilt;
        rebuilt.reserve(image.size());
        unsigned char packet[AVCAM_UVC_PAYLOAD_MAX_BYTES]{};
        AvCamUvcPacketizerReset(&state);
        unsigned int count = 0;
        do {
            rc = AvCamUvcPacketizeMjpeg(&state, image.data(),
                static_cast<unsigned int>(image.size()),
                packet, sizeof(packet), &actual);
            Require(rc == AVCAM_UVC_PACKET_OK &&
                    actual > AVCAM_UVC_PAYLOAD_HEADER_BYTES &&
                    actual <= sizeof(packet),
                    "Payload packet must remain within USB packet capacity.");
            Require(packet[0] == 2 && (packet[1] & 0x80) != 0,
                    "Payload header must have EOH and length 2.");
            if (state.FramesCompleted == 0) {
                Require((packet[1] & 0x02) == 0,
                        "Intermediate packet must not signal EOF.");
            } else {
                Require((packet[1] & 0x02) != 0,
                        "Final packet must signal EOF.");
            }
            rebuilt.insert(rebuilt.end(), packet + 2, packet + actual);
            ++count;
            Require(count < 10000, "Packetizer failed to advance.");
        } while (state.FramesCompleted == 0);
        Require(rebuilt == image && state.Offset == 0 &&
                state.FrameId == 1, "Reassembled maximum JPEG must match input.");

        std::cout << "MJPEG UVC payload packetizer tests passed: "
                  << count << " packets for maximum frame.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
