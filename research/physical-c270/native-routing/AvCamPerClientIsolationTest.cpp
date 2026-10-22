// Cross-platform simulation of two authenticated native client contexts.
// NEVER claims that unchanged Chrome can currently provide such a context.
#include "AvCamPerClientIsolation.h"
#include <atomic>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace avcam::per_client;

namespace {
constexpr auto kPhysical = LR"(\\?\USB#VID_046D&PID_0825&MI_00#EXAMPLE#{e5323777-f976-4f5b-9b55-b94699c46e44}\GLOBAL)";
constexpr auto kInstance = LR"(USB\VID_046D&PID_0825&MI_00\EXAMPLE)";

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
NativeClientProof Verified(SessionId id) {
    NativeClientProof proof;
    proof.nativeSessionId = id;
    proof.originalPhysicalInterface = kPhysical;
    proof.clientSelectedInterface = kPhysical;
    proof.originalPnpInstanceId = kInstance;
    proof.independentlyAuthenticatedClient = true;
    proof.frameDeliveryIsolatedToThisClient = true;
    return proof;
}
Frame Make(std::uint8_t tag, std::uint64_t publisherEpoch,
           std::uint64_t seq, std::uint64_t when = 100000) {
    Frame frame;
    frame.bytes = {tag, tag, tag};
    frame.publisherEpoch = publisherEpoch;
    frame.sequence = seq;
    frame.capturedMs = when;
    return frame;
}
}

int main() {
    try {
        Router router(750);
        // Real process name and a friendly "Logitech" label are NOT proof.
        auto unattested = Verified(10);
        unattested.independentlyAuthenticatedClient = false;
        Require(router.Attach(unattested, Channel::Obs) ==
            AttachStatus::RejectedUnprovenIsolation, "unverified client attached");
        unattested = Verified(11);
        unattested.frameDeliveryIsolatedToThisClient = false;
        Require(router.Attach(unattested, Channel::Obs) ==
            AttachStatus::RejectedUnprovenIsolation, "shared client attached");
        auto counterfeit = Verified(12);
        counterfeit.clientSelectedInterface = LR"(\\?\SWD#VirtualCamera#AVCAM)";
        Require(router.Attach(counterfeit, Channel::Obs) ==
            AttachStatus::RejectedPhysicalEndpoint, "virtual endpoint accepted");
        counterfeit = Verified(13);
        counterfeit.originalPnpInstanceId = LR"(SWD\VID_046D&PID_0825)";
        Require(router.Attach(counterfeit, Channel::Raw) ==
            AttachStatus::RejectedPhysicalEndpoint, "spoofed PnP accepted");

        // The only way to attach is to SIMULATE native authentic client
        // contexts. No production OS integration is shipped.
        Require(router.Attach(Verified(100), Channel::Raw) ==
            AttachStatus::Attached, "RAW session attach");
        Require(router.Attach(Verified(200), Channel::Obs) ==
            AttachStatus::Attached, "OBS session attach");
        Require(router.Attach(Verified(200), Channel::Raw) ==
            AttachStatus::RejectedDuplicateSession, "duplicate session attached");
        Require(router.ActiveSessions() == 2, "unexpected session count");

        Delivery result{};
        Require(!router.Read(100, 100000, result), "raw read without source");
        Require(!router.Read(200, 100000, result), "obs read without source");
        Require(router.Publish(Channel::Raw, Make(0x11, 1, 1)), "publish RAW");
        Require(router.Publish(Channel::Obs, Make(0xee, 2, 1)), "publish OBS");
        Require(router.Read(100, 100000, result) &&
            result.channel == Channel::Raw &&
            result.frame.bytes[0] == 0x11, "RAW session got processed frame");
        Require(router.Read(200, 100000, result) &&
            result.channel == Channel::Obs &&
            result.frame.bytes[0] == 0xee, "OBS session got raw frame");
        Require(!router.Read(999, 100000, result), "unknown session read");
        Require(!router.Publish(Channel::Obs, Make(0x22, 2, 1)),
            "duplicate OBS frame sequence accepted");
        Require(!router.Read(200, 100751, result), "stale OBS frame exposed");
        Require(router.Read(100, 100500, result) &&
            result.frame.bytes[0] == 0x11, "RAW freshness corrupted");

        // An OBS stop must NEVER leak the physical RAW image to Chrome.
        router.DisconnectPublisher(Channel::Obs);
        Require(!router.Read(200, 100000, result), "RAW fallback leaked to OBS client");
        Require(router.Read(100, 100000, result) &&
            result.frame.bytes[0] == 0x11, "RAW altered by OBS loss");
        Require(router.Publish(Channel::Obs, Make(0xef, 3, 1)),
            "OBS reconnection refused after explicit disconnect");
        Require(router.Read(200, 100000, result) &&
            result.frame.bytes[0] == 0xef, "OBS reconnection wrong frame");
        Require(!router.Publish(Channel::Raw, {}),
            "empty physical frame accepted");

        // Three threads: two publishers, one reader. Each session must
        // exclusively observe its assigned source under concurrent updates.
        std::atomic<bool> failure{false};
        auto publisher = [&](Channel channel, std::uint8_t tag,
                             std::uint64_t epoch) {
            for (std::uint64_t i=1; i<=5000; ++i) {
                if (!router.Publish(channel, Make(tag, epoch, i)))
                    failure = true;
            }
        };
        // Reset both channel publisher epochs first.
        router.DisconnectPublisher(Channel::Raw);
        router.DisconnectPublisher(Channel::Obs);
        std::thread raw(publisher, Channel::Raw, 0x11, 10);
        std::thread obs(publisher, Channel::Obs, 0xee, 20);
        std::thread receiver([&] {
            for (int i=0; i<5000; ++i) {
                Delivery a{};
                Delivery b{};
                if (router.Read(100, 100000, a) &&
                    (a.channel != Channel::Raw ||
                     a.frame.bytes != std::vector<std::uint8_t>{0x11,0x11,0x11}))
                    failure = true;
                if (router.Read(200, 100000, b) &&
                    (b.channel != Channel::Obs ||
                     b.frame.bytes != std::vector<std::uint8_t>{0xee,0xee,0xee}))
                    failure = true;
            }
        });
        raw.join();
        obs.join();
        receiver.join();
        Require(!failure.load(), "cross-consumer race or corrupted frame");
        Require(router.Read(100, 100000, result) &&
                result.frame.sequence == 5000, "missing final RAW frame");
        Require(router.Read(200, 100000, result) &&
                result.frame.sequence == 5000, "missing final OBS frame");

        router.Detach(200);
        Require(!router.Read(200, 100000, result),
            "detached OBS client still reading");
        Require(router.Read(100, 100000, result),
            "detaching OBS affected original RAW client");
        router.Shutdown();
        Require(router.ActiveSessions() == 0, "shutdown left client contexts");
        Require(!router.Read(100, 100000, result), "read after shutdown");
        Require(!router.Publish(Channel::Obs, Make(0xee, 30, 1)),
            "publish after shutdown");
        Require(router.Attach(Verified(300), Channel::Raw) ==
            AttachStatus::RejectedShutdown, "reattach after shutdown");

        std::cout << "AVCAM_STRICT_PER_CLIENT_SIMULATION_PASS\n"
                  << "AVCAM_RAW_OBS_CROSS_CLIENT_LEAK=0\n"
                  << "AVCAM_OBS_RAW_FALLBACK=0\n"
                  << "AVCAM_PHYSICAL_ENDPOINT_NATIVE_ADAPTER=NOT_IMPLEMENTED\n"
                  << "AVCAM_CHROME_SAME_PHYSICAL_ENDPOINT=NOT_PROVEN\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "AVCAM_STRICT_PER_CLIENT_SIMULATION_FAILED: "
                  << error.what() << '\n';
        return 1;
    }
}
