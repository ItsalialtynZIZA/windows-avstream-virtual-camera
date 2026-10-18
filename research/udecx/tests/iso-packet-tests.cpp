#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "../AvCamUdeIso.h"

static void Expect(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

static void RunContinuousIsoWindowTest() {
    constexpr unsigned packets = 128u;
    constexpr unsigned window = 3072u;
    constexpr unsigned capacity = packets * window;
    constexpr unsigned jpegBytes = 42738u;
    constexpr unsigned urbs = 260u;

    std::vector<unsigned char> jpeg(jpegBytes);
    for (unsigned i = 0; i < jpegBytes; ++i) {
        jpeg[i] = static_cast<unsigned char>((i * 37u + 11u) & 255u);
    }
    jpeg[0] = 0xFFu;
    jpeg[1] = 0xD8u;
    jpeg[jpegBytes - 2u] = 0xFFu;
    jpeg[jpegBytes - 1u] = 0xD9u;

    std::vector<unsigned> offsets(packets);
    std::vector<unsigned> actual(packets);
    std::vector<unsigned char> buffer(capacity + 16u);
    for (unsigned i = 0; i < packets; ++i) {
        offsets[i] = i * window;
    }

    AVCAM_UDE_ISO_STREAM stream{};
    AvCamUdeIsoReset(&stream);
    std::vector<unsigned char> reconstructed;
    unsigned completed = 0;
    unsigned nonEmptyPackets = 0;
    unsigned lastFrameId = 0;
    bool frameStarted = false;

    for (unsigned urb = 0; urb < urbs; ++urb) {
        std::fill(buffer.begin(), buffer.end(), 0xA5u);
        std::fill(actual.begin(), actual.end(), 999u);
        unsigned returned = 0;
        const auto result = AvCamUdeIsoFill(
            &stream, jpeg.data(), jpegBytes, buffer.data() + 8u,
            capacity, offsets.data(), packets,
            static_cast<unsigned long long>(urb) * packets *
                AVCAM_UDE_ISO_MICROFRAME_100NS,
            actual.data(), &returned);
        Expect(result == AVCAM_UDE_ISO_OK, "ISO fill must succeed");
        Expect(std::all_of(buffer.begin(), buffer.begin() + 8u,
                    [](unsigned char byte) { return byte == 0xA5u; }),
               "Canary preceding the URB transfer buffer changed");
        Expect(std::all_of(buffer.end() - 8u, buffer.end(),
                    [](unsigned char byte) { return byte == 0xA5u; }),
               "Canary following the URB transfer buffer changed");

        unsigned sum = 0;
        for (unsigned i = 0; i < packets; ++i) {
            sum += actual[i];
            Expect(actual[i] <= window, "Packet exceeds host ISO window");
            if (actual[i] == 0) continue;
            ++nonEmptyPackets;
            const unsigned char* payload = buffer.data() + 8u + offsets[i];
            Expect(actual[i] >= 3u && actual[i] <= 163u,
                   "42KB JPEG must be paced to 161 payload bytes per microframe");
            Expect(payload[0] == 2u && (payload[1] & AVCAM_UVC_PAYLOAD_EOH) != 0,
                   "UVC header missing");
            const unsigned fid = payload[1] & AVCAM_UVC_PAYLOAD_FID;
            if (!frameStarted) {
                Expect(completed == 0u || fid != lastFrameId,
                       "UVC FID must toggle between frames");
                lastFrameId = fid;
                frameStarted = true;
            } else {
                Expect(fid == lastFrameId, "FID changed inside a JPEG frame");
            }
            const unsigned length = actual[i] - 2u;
            Expect(reconstructed.size() + length <= jpeg.size(),
                   "Reconstructed JPEG is larger than its source");
            reconstructed.insert(reconstructed.end(), payload + 2u,
                                 payload + actual[i]);
            if ((payload[1] & AVCAM_UVC_PAYLOAD_EOF) != 0) {
                Expect(reconstructed == jpeg, "JPEG stream content mismatch");
                reconstructed.clear();
                ++completed;
                frameStarted = false;
            }
        }
        Expect(sum == returned, "Returned ISO bytes differ from packet sum");
        Expect(returned <= capacity, "ISO completion exceeds transfer buffer");
    }

    Expect(completed >= 120u && completed <= 126u,
           "MJPEG stream must maintain approximately 30 complete fps");
    Expect(completed == stream.Packetizer.FramesCompleted,
           "Finished-JPEG count differs from packetizer state");
    Expect(nonEmptyPackets > 32000u,
           "MJPEG frame payload should be present in most microframes");
    Expect(reconstructed.size() == stream.Packetizer.Offset,
           "In-flight JPEG must agree with packetizer's next byte index");
    Expect(std::equal(reconstructed.begin(), reconstructed.end(), jpeg.begin()),
           "Final partial JPEG must be an exact source prefix");

    std::cout << "ISO simulation: completed=" << completed
              << " packets=" << nonEmptyPackets << '\n';
}

static void RunMalformedLayoutTest() {
    constexpr unsigned count = 4u;
    constexpr unsigned capacity = 4u * 3072u;
    unsigned offsets[count] = {0u, 3072u, 3072u, 9216u};
    unsigned packetBytes[count] = {999u, 999u, 999u, 999u};
    unsigned char jpeg[4] = {0xFF, 0xD8, 0xFF, 0xD9};
    std::vector<unsigned char> buffer(capacity, 0xA5u);
    AVCAM_UDE_ISO_STREAM stream{};
    AvCamUdeIsoReset(&stream);
    unsigned returned = 999u;

    int result = AvCamUdeIsoFill(
        &stream, jpeg, 4u, buffer.data(), capacity,
        offsets, count, 0u, packetBytes, &returned);
    Expect(result == AVCAM_UDE_ISO_INVALID && returned == 0u,
           "Duplicate host offsets must reject the entire ISO URB");
    Expect(stream.Initialized == 0u &&
           stream.Packetizer.FramesCompleted == 0u,
           "Malformed layout must not mutate ISO stream state");
    Expect(std::all_of(buffer.begin(), buffer.end(),
           [](unsigned char byte) { return byte == 0xA5u; }),
           "Malformed layout must not touch the transfer buffer");
    Expect(std::all_of(packetBytes, packetBytes + count,
           [](unsigned value) { return value == 999u; }),
           "Malformed layout must not update packet length descriptors");

    offsets[2] = 6144u;
    returned = 999u;
    result = AvCamUdeIsoFill(
        &stream, jpeg, 4u, buffer.data(), capacity, offsets, count,
        ULLONG_MAX - 100u, packetBytes, &returned);
    Expect(result == AVCAM_UDE_ISO_INVALID && returned == 0u,
           "Microframe clock overflow must be rejected");

    returned = 999u;
    result = AvCamUdeIsoFill(
        &stream, jpeg, 0u, buffer.data(), capacity, offsets, count,
        0u, packetBytes, &returned);
    Expect(result == AVCAM_UDE_ISO_INVALID && returned == 0u,
           "Empty JPEG input must be rejected");
}

int main() {
    try {
        RunContinuousIsoWindowTest();
        RunMalformedLayoutTest();
        std::cout << "UdeCx ISO buffer-layout and MJPEG pacing tests passed\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
