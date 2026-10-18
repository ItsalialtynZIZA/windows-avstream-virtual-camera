#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "../shared/AvCamIsoPacketLayout.h"
#include "../shared/AvCamIsoFrameClock.h"
#include "../shared/AvCamUvcPayload.h"

static void Require(bool ok, const char* msg)
{
    if (!ok) throw std::runtime_error(msg);
}

int main()
{
    try {
        constexpr unsigned int packetCount = 128;
        constexpr unsigned int packetCapacity = AVCAM_ISO_MAX_MICROFRAME_BYTES;
        constexpr unsigned long long interval = AVCAM_UVC_FRAME_INTERVAL_100NS;
        constexpr unsigned long long urbDuration = packetCount * AVCAM_ISO_MICROFRAME_100NS;
        std::vector<unsigned char> jpeg(63937u, 0x6Du);
        jpeg[0] = 0xFF; jpeg[1] = 0xD8;
        jpeg[jpeg.size()-2] = 0xFF; jpeg[jpeg.size()-1] = 0xD9;
        std::vector<unsigned char> transfer(packetCount * packetCapacity);
        std::vector<unsigned char> reconstructed;
        AVCAM_UVC_PACKETIZER state{};
        AvCamUvcPacketizerReset(&state);
        unsigned long long nextDue = 0;
        unsigned int completeFrames = 0;
        unsigned char lastFid = 0;

        for (unsigned int urb=0; urb<625; ++urb) {
            std::fill(transfer.begin(), transfer.end(), static_cast<unsigned char>(0xCD));
            const unsigned long long now = 50000000ull + urb * urbDuration;
            bool eof = false;
            unsigned long returnedBytes = 0;
            for (unsigned int slot=0; slot<packetCount; ++slot) {
                const unsigned int offset = slot*packetCapacity;
                const unsigned long long packetTime = now +
                    slot * AVCAM_ISO_MICROFRAME_100NS;
                Require(AvCamIsoPacketWindowValid(offset,offset+packetCapacity,
                    static_cast<unsigned int>(transfer.size())),
                    "Invalid packet layout");
                if (eof || (state.Offset == 0 && packetTime < nextDue)) {
                    Require(transfer[offset]==0xCD, "Empty packet unexpectedly touched");
                    continue;
                }
                if (state.Offset == 0) {
                    nextDue = AvCamIsoAdvanceFrameDue(nextDue,packetTime,interval);
                }
                unsigned int written = 0;
                const int result=AvCamUvcPacketizeMjpeg(
                    &state,jpeg.data(),static_cast<unsigned int>(jpeg.size()),
                    transfer.data()+offset,packetCapacity,&written);
                Require(result==AVCAM_UVC_PACKET_OK &&
                        written >= 3 && written <= packetCapacity,
                        "Invalid UVC payload length");
                returnedBytes+=written;
                Require(transfer[offset]==AVCAM_UVC_PAYLOAD_HEADER_BYTES &&
                        (transfer[offset+1] & AVCAM_UVC_PAYLOAD_EOH)!=0,
                        "Malformed UVC packet header");
                Require((transfer[offset+1] & AVCAM_UVC_PAYLOAD_FID)==lastFid,
                        "Frame ID changed before EOF");
                Require(written==packetCapacity ||
                        transfer[offset+written]==0xCD,
                        "Packet write crossed its length");
                reconstructed.insert(reconstructed.end(),
                    transfer.begin()+offset+AVCAM_UVC_PAYLOAD_HEADER_BYTES,
                    transfer.begin()+offset+written);
                if ((transfer[offset+1] & AVCAM_UVC_PAYLOAD_EOF)!=0) {
                    eof=true;
                    Require(reconstructed==jpeg, "Reassembled JPEG frame mismatch");
                    reconstructed.clear();
                    ++completeFrames;
                    lastFid^=AVCAM_UVC_PAYLOAD_FID;
                }
            }
            Require(returnedBytes<=packetCount*packetCapacity,
                    "ISO transfer payload exceeded allocation");
        }

        Require(completeFrames>=298 && completeFrames<=302,
                "MJPEG stream drifted away from 30fps");
        Require(state.Offset==0 && reconstructed.empty(),
                "Streaming test ended with a partial JPEG");
        std::cout << "Integrated ISO / UVC 10s test passed: "
                  << completeFrames << " complete MJPEG frames.\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
