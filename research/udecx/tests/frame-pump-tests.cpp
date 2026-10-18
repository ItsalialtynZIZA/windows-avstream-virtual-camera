#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "../AvCamUdeFramePump.h"

static void Expect(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
static std::vector<unsigned char> MakeJpeg(unsigned seed) {
    std::vector<unsigned char> jpeg(42738u);
    for (unsigned i = 0; i < jpeg.size(); ++i) {
        jpeg[i] = static_cast<unsigned char>((i * 37u + seed) & 0xffu);
    }
    jpeg[0] = 0xff;
    jpeg[1] = 0xd8;
    jpeg[jpeg.size() - 2] = 0xff;
    jpeg[jpeg.size() - 1] = 0xd9;
    return jpeg;
}
static void TestFrameRotationAcrossUrbs() {
    constexpr unsigned count = 128u;
    constexpr unsigned window = 3072u;
    constexpr unsigned capacity = count * window;

    auto a = MakeJpeg(11);
    auto b = MakeJpeg(23);
    auto c = MakeJpeg(41);
    std::vector<unsigned char> slotA(a.size());
    std::vector<unsigned char> slotB(a.size());
    AVCAM_UDE_FRAME_SOURCE source{};
    Expect(AvCamUdeFrameSourceInit(&source,
        slotA.data(), static_cast<unsigned>(slotA.size()),
        slotB.data(), static_cast<unsigned>(slotB.size())) ==
            AVCAM_UDE_FRAME_OK, "Source init failed");
    Expect(AvCamUdeFrameSourcePublish(
        &source, a.data(), static_cast<unsigned>(a.size())) ==
            AVCAM_UDE_FRAME_OK, "A publication failed");

    AVCAM_UDE_FRAME_PUMP pump{};
    Expect(AvCamUdeFramePumpInit(&pump, &source) == AVCAM_UDE_PUMP_OK,
        "Pump init failed");
    std::vector<unsigned char> buffer(capacity + 16u, 0xa5);
    std::vector<unsigned> offsets(count);
    std::vector<unsigned> lengths(count);
    for (unsigned i = 0; i < count; ++i) offsets[i] = i * window;
    std::vector<unsigned char> rebuilt;
    unsigned completed = 0;
    unsigned fid = 0;
    bool started = false;
    bool sentB = false;
    bool sentC = false;

    for (unsigned urb = 0; urb < 120; ++urb) {
        std::fill(buffer.begin(), buffer.end(), static_cast<unsigned char>(0xa5u));
        std::fill(lengths.begin(), lengths.end(), 999u);
        unsigned returned = 0;
        const auto result = AvCamUdeFramePumpFill(
            &pump, buffer.data() + 8, capacity,
            offsets.data(), count,
            static_cast<unsigned long long>(urb) * count *
                AVCAM_UDE_ISO_MICROFRAME_100NS,
            lengths.data(), &returned);
        Expect(result == AVCAM_UDE_PUMP_OK, "Pump fill rejected legal URB");
        Expect(std::all_of(buffer.begin(), buffer.begin() + 8,
            [](unsigned char x) { return x == 0xa5; }), "Left canary damaged");
        Expect(std::all_of(buffer.end() - 8, buffer.end(),
            [](unsigned char x) { return x == 0xa5; }), "Right canary damaged");
        unsigned total = 0;
        for (unsigned i = 0; i < count; ++i) {
            const unsigned length = lengths[i];
            Expect(length <= window, "Length exceeds host window");
            total += length;
            if (length == 0) continue;
            const auto* payload = buffer.data() + 8 + offsets[i];
            Expect(length >= 3 && payload[0] == 2 &&
                (payload[1] & AVCAM_UVC_PAYLOAD_EOH),
                "Malformed UVC ISO header");
            const unsigned currentFid = payload[1] & AVCAM_UVC_PAYLOAD_FID;
            if (!started) {
                if (completed > 0) {
                    Expect(currentFid != fid, "FID did not flip at EOF");
                }
                fid = currentFid;
                started = true;
            }
            Expect(currentFid == fid, "FID changed within JPEG");
            rebuilt.insert(rebuilt.end(), payload + 2, payload + length);
            if (payload[1] & AVCAM_UVC_PAYLOAD_EOF) {
                const auto& expected = completed == 0 ? a :
                    completed == 1 ? b : c;
                Expect(rebuilt == expected,
                    "EOF produced mixed or corrupted JPEG generations");
                rebuilt.clear();
                ++completed;
                started = false;
            }
        }
        Expect(total == returned, "Sparse sum mismatch");

        if (urb == 0) {
            Expect(pump.Lease.Data != nullptr, "A should span multiple URBs");
            Expect(AvCamUdeFrameSourcePublish(
                &source, b.data(), static_cast<unsigned>(b.size())) ==
                    AVCAM_UDE_FRAME_OK, "B publication rejected");
            sentB = true;
            Expect(AvCamUdeFrameSourcePublish(
                &source, c.data(), static_cast<unsigned>(c.size())) ==
                    AVCAM_UDE_FRAME_BUSY,
                    "Frame A overwritten before its EOF");
        }
        if (!sentC && completed >= 1 && completed < 2) {
            Expect(AvCamUdeFrameSourcePublish(
                &source, c.data(), static_cast<unsigned>(c.size())) ==
                    AVCAM_UDE_FRAME_OK,
                    "Slot should be free immediately after first EOF");
            sentC = true;
        }
    }
    Expect(sentB && sentC, "Both generation switches must be exercised");
    Expect(completed >= 50 && completed <= 60,
        "Expected approximately 30fps from ISO time base");
    Expect(pump.Stream.Packetizer.FramesCompleted == completed,
        "Frame count differs from portable encoder");
    Expect(AvCamUdeFramePumpReset(&pump) == AVCAM_UDE_PUMP_OK,
        "Reset should release the active JPEG lease");
    Expect(pump.Lease.Data == nullptr &&
        pump.Stream.Packetizer.FramesCompleted == 0,
        "Reset did not clear stream state");
}

static void TestInvalidInput() {
    unsigned char storageA[16] = {};
    unsigned char storageB[16] = {};
    unsigned char jpeg[5] = {0xff, 0xd8, 1, 0xff, 0xd9};
    AVCAM_UDE_FRAME_SOURCE source{};
    AVCAM_UDE_FRAME_PUMP pump{};
    unsigned char buffer[16] = {};
    unsigned int offsets[2] = {0, 0};
    unsigned int lengths[2] = {555, 555};
    unsigned int returned = 555;
    Expect(AvCamUdeFrameSourceInit(
        &source, storageA, 16, storageB, 16) == AVCAM_UDE_FRAME_OK,
        "Source initialization failed");
    Expect(AvCamUdeFramePumpInit(&pump, &source) == AVCAM_UDE_PUMP_OK,
        "Pump initialization failed");
    Expect(AvCamUdeFramePumpFill(
        &pump, buffer, 16, offsets, 2, 0, lengths, &returned) ==
        AVCAM_UDE_PUMP_NO_FRAME, "No-frame input not rejected");
    Expect(returned == 0 && lengths[0] == 555,
        "No-frame rejection must not mutate packet metadata");
    Expect(AvCamUdeFrameSourcePublish(
        &source, jpeg, sizeof(jpeg)) == AVCAM_UDE_FRAME_OK,
        "JPEG publish failed");
    Expect(AvCamUdeFramePumpFill(
        &pump, buffer, 16, offsets, 2, 0, lengths, &returned) ==
        AVCAM_UDE_PUMP_INVALID, "Overlapping offsets accepted");
    Expect(pump.Lease.Data == nullptr && lengths[0] == 555,
        "Invalid offsets acquired a lease or changed metadata");
}

int main() {
    try {
        TestFrameRotationAcrossUrbs();
        TestInvalidInput();
        std::cout << "MJPEG frame pump multi-URB lease transitions passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
