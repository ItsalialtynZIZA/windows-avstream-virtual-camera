#include <array>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include "../AvCamUdeFrameSource.h"

static void Expect(bool value, const char* reason) {
    if (!value) throw std::runtime_error(reason);
}

static void TestImmutableLeases() {
    std::array<unsigned char, 128> first{};
    std::array<unsigned char, 128> second{};
    AVCAM_UDE_FRAME_SOURCE source{};
    AVCAM_UDE_FRAME_LEASE oldFrame{};
    AVCAM_UDE_FRAME_LEASE currentFrame{};
    const unsigned char a[] = {0xff, 0xd8, 0x11, 0x22, 0xff, 0xd9};
    const unsigned char b[] = {0xff, 0xd8, 0x33, 0x44, 0xff, 0xd9};
    const unsigned char c[] = {0xff, 0xd8, 0x55, 0x66, 0xff, 0xd9};

    Expect(AvCamUdeFrameSourceInit(&source, first.data(),
        static_cast<unsigned>(first.size()), second.data(),
        static_cast<unsigned>(second.size())) == AVCAM_UDE_FRAME_OK,
        "Frame source init failed");
    Expect(AvCamUdeFrameSourceAcquire(&source, &oldFrame) ==
        AVCAM_UDE_FRAME_NO_DATA, "Uninitialized source must not supply a frame");

    Expect(AvCamUdeFrameSourcePublish(&source, a, sizeof(a)) ==
        AVCAM_UDE_FRAME_OK, "First publish failed");
    Expect(AvCamUdeFrameSourceAcquire(&source, &oldFrame) ==
        AVCAM_UDE_FRAME_OK, "Acquire first JPEG failed");
    Expect(std::equal(oldFrame.Data, oldFrame.Data + oldFrame.Length, a),
        "First JPEG bytes mismatch");

    Expect(AvCamUdeFrameSourcePublish(&source, b, sizeof(b)) ==
        AVCAM_UDE_FRAME_OK, "Second publish failed");
    Expect(std::equal(oldFrame.Data, oldFrame.Data + oldFrame.Length, a),
        "Publishing second JPEG corrupted leased first JPEG");
    Expect(AvCamUdeFrameSourceAcquire(&source, &currentFrame) ==
        AVCAM_UDE_FRAME_OK, "Acquire second JPEG failed");
    Expect(currentFrame.Generation != oldFrame.Generation,
        "Frame generations must advance");
    Expect(std::equal(currentFrame.Data,
        currentFrame.Data + currentFrame.Length, b),
        "Second JPEG bytes mismatch");

    Expect(AvCamUdeFrameSourcePublish(&source, c, sizeof(c)) ==
        AVCAM_UDE_FRAME_BUSY, "Cannot overwrite still-leased first slot");
    Expect(std::equal(oldFrame.Data, oldFrame.Data + oldFrame.Length, a),
        "Busy publish must not corrupt old frame");
    Expect(AvCamUdeFrameSourceRelease(&source, &oldFrame) ==
        AVCAM_UDE_FRAME_OK, "Release old frame failed");
    Expect(AvCamUdeFrameSourceRelease(&source, &oldFrame) ==
        AVCAM_UDE_FRAME_INVALID, "Double release must be rejected");
    Expect(AvCamUdeFrameSourcePublish(&source, c, sizeof(c)) ==
        AVCAM_UDE_FRAME_OK, "Released slot should be reusable");
    Expect(std::equal(currentFrame.Data,
        currentFrame.Data + currentFrame.Length, b),
        "Publishing third frame modified leased second frame");
    Expect(AvCamUdeFrameSourceRelease(&source, &currentFrame) ==
        AVCAM_UDE_FRAME_OK, "Release second frame failed");
}

static void TestMalformedSource() {
    unsigned char jpeg[] = {0xff, 0xd8, 0x55, 0xff, 0xd9};
    unsigned char invalid[] = {0x00, 0xd8, 0x55, 0xff, 0xd9};
    std::array<unsigned char, 16> bufferA{};
    std::array<unsigned char, 16> bufferB{};
    AVCAM_UDE_FRAME_SOURCE source{};
    AVCAM_UDE_FRAME_LEASE lease{};
    Expect(AvCamUdeFrameSourceInit(&source, bufferA.data(), 16,
        bufferA.data(), 16) == AVCAM_UDE_FRAME_INVALID,
        "Aliased frame slots must fail initialization");
    Expect(AvCamUdeFrameSourceInit(&source, bufferA.data(), 16,
        bufferB.data(), 16) == AVCAM_UDE_FRAME_OK, "Source init failed");
    Expect(AvCamUdeFrameSourcePublish(&source, invalid,
        sizeof(invalid)) == AVCAM_UDE_FRAME_INVALID,
        "Corrupted JPEG marker must reject input");
    Expect(AvCamUdeFrameSourceAcquire(&source, &lease) ==
        AVCAM_UDE_FRAME_NO_DATA, "Rejected JPEG must not be published");
    Expect(AvCamUdeFrameSourcePublish(&source, jpeg, 17) ==
        AVCAM_UDE_FRAME_INVALID,
        "Frame larger than slot must reject publication");
    Expect(AvCamUdeFrameSourcePublish(&source, jpeg,
        sizeof(jpeg)) == AVCAM_UDE_FRAME_OK,
        "Valid bounded frame must publish");
}

int main() {
    try {
        TestImmutableLeases();
        TestMalformedSource();
        std::cout << "UdeCx two-slot MJPEG source lease tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
