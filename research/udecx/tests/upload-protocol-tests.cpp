#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "../AvCamUdeUploadProtocol.h"

static void Expect(bool ok, const char* reason) {
    if (!ok) throw std::runtime_error(reason);
}

/* Minimal SOI / APP0 / SOF0 / SOS / entropy / EOI envelope. This is a
 * parser fixture only, not a claimed decodable JPEG. WIC decoder acceptance
 * is a separate runtime gate before first real Media Foundation frame.
 */
static std::vector<unsigned char> MakeHeader(unsigned width = 1920,
                                             unsigned height = 1080) {
    return {
        0xff,0xd8, 0xff,0xe0, 0,4, 1,2,
        0xff,0xc0, 0,11, 8,
        static_cast<unsigned char>(height >> 8),
        static_cast<unsigned char>(height),
        static_cast<unsigned char>(width >> 8),
        static_cast<unsigned char>(width), 1, 1, 0x11, 0,
        0xff,0xda, 0,8, 1,1,0,0,63,0,
        1,2,3,4,0xff,0xd9
    };
}
static std::vector<unsigned char> Wrap(
    const std::vector<unsigned char>& jpeg)
{
    AVCAM_UDE_UPLOAD_HEADER head{};
    head.Magic = AVCAM_UDE_UPLOAD_MAGIC;
    head.Version = AVCAM_UDE_UPLOAD_VERSION;
    head.Width = AVCAM_UDE_UPLOAD_WIDTH;
    head.Height = AVCAM_UDE_UPLOAD_HEIGHT;
    head.FrameBytes = static_cast<unsigned>(jpeg.size());
    std::vector<unsigned char> result(sizeof(head) + jpeg.size());
    std::memcpy(result.data(), &head, sizeof(head));
    std::memcpy(result.data() + sizeof(head), jpeg.data(), jpeg.size());
    return result;
}
static void TestValidEnvelope() {
    auto packet = Wrap(MakeHeader());
    const unsigned char* jpeg = nullptr;
    unsigned bytes = 0;
    Expect(sizeof(AVCAM_UDE_UPLOAD_HEADER) == 20u, "Wire header size changed");
    Expect(AvCamUdeValidateUpload(packet.data(),
        static_cast<unsigned>(packet.size()), &jpeg, &bytes) ==
        AVCAM_UDE_UPLOAD_OK, "Valid JPEG envelope rejected");
    Expect(bytes == packet.size() - sizeof(AVCAM_UDE_UPLOAD_HEADER),
        "Wrong returned JPEG length");
    Expect(jpeg == packet.data() + sizeof(AVCAM_UDE_UPLOAD_HEADER),
        "Unexpected JPEG buffer pointer");
}
static void TestMalformations() {
    const auto jpeg = MakeHeader();
    auto packet = Wrap(jpeg);
    const unsigned char* returned = reinterpret_cast<const unsigned char*>(1);
    unsigned bytes = 123;
    auto checkRejected = [&](std::vector<unsigned char>& candidate,
                             const char* message) {
        returned = reinterpret_cast<const unsigned char*>(1);
        bytes = 123;
        Expect(AvCamUdeValidateUpload(candidate.data(),
            static_cast<unsigned>(candidate.size()),
            &returned, &bytes) == AVCAM_UDE_UPLOAD_INVALID, message);
        Expect(returned == nullptr && bytes == 0,
            "Rejected upload exposed frame bytes");
    };
    {
        auto p = packet;
        p[0] ^= 1;
        checkRejected(p, "Wrong magic accepted");
    }
    {
        auto p = packet;
        auto* h = reinterpret_cast<AVCAM_UDE_UPLOAD_HEADER*>(p.data());
        h->Version = 2;
        checkRejected(p, "Unknown version accepted");
    }
    {
        auto p = packet;
        auto* h = reinterpret_cast<AVCAM_UDE_UPLOAD_HEADER*>(p.data());
        h->FrameBytes = 0xffffffffu;
        checkRejected(p, "Oversized or overflowed frame accepted");
    }
    {
        auto p = packet;
        p.pop_back();
        checkRejected(p, "Truncated EOI accepted");
    }
    {
        auto p = packet;
        p[sizeof(AVCAM_UDE_UPLOAD_HEADER) + 10] = 0xff;
        checkRejected(p, "Malformed segment length accepted");
    }
    {
        auto p = Wrap(MakeHeader(1280, 720));
        checkRejected(p, "Wrong SOF dimensions accepted");
    }
    {
        auto p = packet;
        p[sizeof(AVCAM_UDE_UPLOAD_HEADER) + 22] = 0xd9;
        checkRejected(p, "Missing SOS accepted");
    }
    {
        auto p = packet;
        p[p.size() - 1] = 0;
        checkRejected(p, "Missing EOI accepted");
    }
    {
        auto p = packet;
        auto* h = reinterpret_cast<AVCAM_UDE_UPLOAD_HEADER*>(p.data());
        --h->FrameBytes;
        checkRejected(p, "Trailing bytes or length mismatch accepted");
    }
}
int main() {
    try {
        TestValidEnvelope();
        TestMalformations();
        std::cout << "Bounded MJPEG upload protocol tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
