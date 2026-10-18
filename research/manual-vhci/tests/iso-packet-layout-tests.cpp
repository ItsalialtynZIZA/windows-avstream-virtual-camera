#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "../shared/AvCamIsoPacketLayout.h"

static void Check(bool ok, const char* msg) {
    if (!ok) throw std::runtime_error(msg);
}

int main() {
    try {
        constexpr unsigned int kCount = 128;
        constexpr unsigned int kBytes = kCount * AVCAM_ISO_MAX_MICROFRAME_BYTES;
        for (unsigned int i=0; i<kCount; ++i) {
            const unsigned int start = i*AVCAM_ISO_MAX_MICROFRAME_BYTES;
            Check(AvCamIsoPacketWindowValid(start,
                start+AVCAM_ISO_MAX_MICROFRAME_BYTES, kBytes),
                "Expected valid 3072-byte microframe");
        }
        Check(!AvCamIsoPacketWindowValid(10,10,100), "Zero span accepted");
        Check(!AvCamIsoPacketWindowValid(11,10,100), "Descending offsets accepted");
        Check(!AvCamIsoPacketWindowValid(0,2,100), "Missing UVC payload byte accepted");
        Check(!AvCamIsoPacketWindowValid(0,3073,4096), "Oversized packet accepted");
        Check(!AvCamIsoPacketWindowValid(100,400,399), "Out-of-bounds span accepted");
        Check(!AvCamIsoPacketWindowValid(UINT32_MAX-10u, UINT32_MAX, 1024), "Overflow-adjacent span accepted");
        Check(AvCamIsoPacketWindowValid(0,3,3), "Smallest UVC packet rejected");
        Check(AvCamIsoPacketWindowValid(9,12,12), "Valid nonzero offset rejected");

        // Model sparse transfers: a short packet must never move later offsets.
        std::vector<unsigned char> buffer(kBytes, 0xCC);
        const unsigned int offset = 21*AVCAM_ISO_MAX_MICROFRAME_BYTES;
        const unsigned int length = 137u;
        Check(AvCamIsoPacketWindowValid(offset,offset+AVCAM_ISO_MAX_MICROFRAME_BYTES,kBytes),
              "Short packet window invalid");
        for(unsigned int j=0;j<length;++j) buffer[offset+j]=0x5A;
        Check(buffer[offset+length]==0xCC &&
              buffer[offset+AVCAM_ISO_MAX_MICROFRAME_BYTES]==0xCC,
              "Short packet corrupted the next packet's offset");
        Check(AvCamIsoSparseExtentBytes(42766u, 42766u) == 0u,
              "Packed first-slot ISO payload falsely sparse");
        Check(AvCamIsoSparseExtentBytes(20u*3072u + 42766u, 42766u)
                  == 20u*3072u,
              "Sparse frame beginning after slot 0 was not detected");
        Check(AvCamIsoSparseExtentBytes(393216u, 0u) == 393216u,
              "Maximal sparse extent lost");
        Check(AvCamIsoSparseExtentBytes(0u, 0u) == 0u,
              "Empty URB reported a sparse extent");
        std::cout << "ISO window/bounds/short-packet/sparse-extent tests passed.\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
