#include <iostream>
#include <stdexcept>
#include "../AvCamUdeEndpointDrain.h"

static void Expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static void TestStartAndPurge() {
    AVCAM_UDE_ENDPOINT_DRAIN e{};
    AvCamUdeDrainInit(&e);
    Expect(AvCamUdeDrainEnter(&e) == AVCAM_UDE_DRAIN_BUSY,
        "No I/O before START");
    Expect(AvCamUdeDrainStart(&e) == AVCAM_UDE_DRAIN_OK, "START rejected");
    const auto epoch = e.Generation;
    Expect(AvCamUdeDrainEnter(&e) == AVCAM_UDE_DRAIN_OK, "Enter 1 failed");
    Expect(AvCamUdeDrainEnter(&e) == AVCAM_UDE_DRAIN_OK, "Enter 2 failed");
    Expect(AvCamUdeDrainBeginPurge(&e) == AVCAM_UDE_DRAIN_OK,
        "PURGE rejected");
    Expect(AvCamUdeDrainEnter(&e) == AVCAM_UDE_DRAIN_BUSY,
        "I/O accepted while purging");
    Expect(AvCamUdeDrainStart(&e) == AVCAM_UDE_DRAIN_INVALID,
        "START accepted while purge pending");
    Expect(AvCamUdeDrainClaimPurgeAck(&e) == 0,
        "Purge acknowledged while requests are active");
    Expect(AvCamUdeDrainLeave(&e) == AVCAM_UDE_DRAIN_OK,
        "First request did not drain");
    Expect(AvCamUdeDrainClaimPurgeAck(&e) == 0,
        "Purge acknowledged with one request still active");
    Expect(AvCamUdeDrainLeave(&e) == AVCAM_UDE_DRAIN_OK,
        "Last request did not drain");
    Expect(AvCamUdeDrainClaimPurgeAck(&e) == 1, "Purge ack missing");
    Expect(AvCamUdeDrainClaimPurgeAck(&e) == 0, "Double purge ack");
    Expect(AvCamUdeDrainEnter(&e) == AVCAM_UDE_DRAIN_BUSY,
        "Accepted I/O after purge but before restart");
    Expect(AvCamUdeDrainStart(&e) == AVCAM_UDE_DRAIN_OK,
        "Restart rejected after complete purge");
    Expect(e.Generation == epoch + 1, "Epoch not incremented");
    Expect(AvCamUdeDrainLeave(&e) == AVCAM_UDE_DRAIN_INVALID,
        "Unmatched completion accepted");
}

static void TestEdgeCases() {
    AVCAM_UDE_ENDPOINT_DRAIN e{};
    AvCamUdeDrainInit(&e);
    Expect(AvCamUdeDrainBeginPurge(&e) == AVCAM_UDE_DRAIN_OK,
        "Idle PURGE must be acknowledged");
    Expect(AvCamUdeDrainBeginPurge(&e) == AVCAM_UDE_DRAIN_BUSY,
        "Duplicate PURGE must be denied");
    Expect(AvCamUdeDrainClaimPurgeAck(&e) == 1,
        "Idle PURGE cannot remain pending");
    e.Generation = ~0ull;
    Expect(AvCamUdeDrainStart(&e) == AVCAM_UDE_DRAIN_INVALID,
        "Epoch rollover must be rejected");
    e.Generation = 1;
    Expect(AvCamUdeDrainStart(&e) == AVCAM_UDE_DRAIN_OK,
        "Valid START rejected");
    e.Active = ~0u;
    Expect(AvCamUdeDrainEnter(&e) == AVCAM_UDE_DRAIN_BUSY,
        "Outstanding count overflow accepted");
    Expect(AvCamUdeDrainClaimPurgeAck(nullptr) == 0,
        "Null state must never be acknowledged");
}

/* Enumerate event interleavings without relying on sleeping threads.
 * All transitions are serialized under a lock in the kernel.
 * Invariants must hold for every allowed prefix.
 */
static void Enumerate(AVCAM_UDE_ENDPOINT_DRAIN state, unsigned depth) {
    Expect(!(state.Accepting && state.PurgePending),
        "Purge and accepts cannot coexist");
    if (depth == 0) return;
    for (unsigned event = 0; event < 5; ++event) {
        auto next = state;
        if (event == 0) {
            (void)AvCamUdeDrainStart(&next);
        } else if (event == 1) {
            (void)AvCamUdeDrainEnter(&next);
        } else if (event == 2) {
            (void)AvCamUdeDrainLeave(&next);
        } else if (event == 3) {
            (void)AvCamUdeDrainBeginPurge(&next);
        } else {
            const unsigned before = next.Active;
            const auto claimed = AvCamUdeDrainClaimPurgeAck(&next);
            Expect(!claimed || before == 0,
                "Purge ack claimed with in-flight requests");
        }
        Enumerate(next, depth - 1);
    }
}

int main() {
    try {
        TestStartAndPurge();
        TestEdgeCases();
        AVCAM_UDE_ENDPOINT_DRAIN initial{};
        AvCamUdeDrainInit(&initial);
        Enumerate(initial, 7);
        std::cout << "Endpoint drain interleavings and purge safety passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
