#include <iostream>
#include <stdexcept>
#include "../AvCamUdeAsyncIso.h"

static void Need(bool ok, const char* why) {
    if (!ok) throw std::runtime_error(why);
}

static void AssertInvariants(const AVCAM_UDE_ASYNC_ISO& e) {
    unsigned active = 0;
    for (unsigned i = 0; i < AVCAM_UDE_ASYNC_MAX_REQUESTS; ++i) {
        const auto& s = e.Slots[i];
        if (s.Phase == AVCAM_UDE_ASYNC_FREE) {
            Need(s.Token == 0 && s.Generation == 0 &&
                 s.CancelRequested == 0 &&
                 s.CompletionDecision == AVCAM_UDE_ASYNC_FINISH_NONE,
                 "Freed slot retained stale identity");
        } else {
            ++active;
            Need(s.Token != 0 && s.Generation != 0 &&
                 s.Phase <= AVCAM_UDE_ASYNC_COMPLETING,
                 "Invalid owned request");
            Need((s.Phase == AVCAM_UDE_ASYNC_COMPLETING) ==
                 (s.CompletionDecision != AVCAM_UDE_ASYNC_FINISH_NONE),
                 "Completion decision/phase mismatch");
            for (unsigned j = i + 1; j < AVCAM_UDE_ASYNC_MAX_REQUESTS; ++j) {
                const auto& t = e.Slots[j];
                Need(t.Phase == AVCAM_UDE_ASYNC_FREE ||
                     t.Token != s.Token, "Duplicate request token");
            }
        }
    }
    Need(active == e.Drain.Active, "Unbalanced drain active count");
    Need(!(e.Drain.Accepting && e.Drain.PurgePending),
         "Endpoint accepting during purge");
    Need(!(e.Drain.Accepting && e.ResetPending),
         "Endpoint accepting during reset");
}

static void NormalAndCancelRace() {
    AVCAM_UDE_ASYNC_ISO e{};
    AvCamUdeAsyncInit(&e);
    unsigned long long token = 99;
    Need(AvCamUdeAsyncSubmit(&e, &token) == AVCAM_UDE_ASYNC_BUSY &&
         token == 0, "Before START request must not enter");
    Need(AvCamUdeAsyncStart(&e) == AVCAM_UDE_ASYNC_OK, "START failed");
    Need(AvCamUdeAsyncSubmit(&e, &token) == AVCAM_UDE_ASYNC_OK,
         "Submit failed");
    const auto first = token;
    Need(AvCamUdeAsyncClaimCompletion(&e, token) ==
         AVCAM_UDE_ASYNC_FINISH_NONE, "Completed while still waiting");
    Need(AvCamUdeAsyncWorkerStart(&e, token) == AVCAM_UDE_ASYNC_OK,
         "Worker failed to claim packet");
    Need(AvCamUdeAsyncWorkerDone(&e, token) == AVCAM_UDE_ASYNC_OK,
         "Worker did not release buffer");
    Need(AvCamUdeAsyncClaimCompletion(&e, token) ==
         AVCAM_UDE_ASYNC_FINISH_DATA, "Successful URB not reported");
    Need(e.Drain.Active == 1, "Completion claim prematurely drained URB");
    Need(AvCamUdeAsyncClaimCompletion(&e, token) ==
         AVCAM_UDE_ASYNC_FINISH_NONE, "Double completion");
    Need(AvCamUdeAsyncCancel(&e, token) == AVCAM_UDE_ASYNC_BUSY,
         "Cancellation modified already claimed completion");
    Need(AvCamUdeAsyncCompletionReturned(&e, token) == AVCAM_UDE_ASYNC_OK,
         "Completed request did not leave drain after WDF return");
    Need(AvCamUdeAsyncCompletionReturned(&e, token) == AVCAM_UDE_ASYNC_INVALID,
         "Double completion return accepted");
    Need(AvCamUdeAsyncCancel(&e, token) == AVCAM_UDE_ASYNC_NOT_FOUND,
         "Already completed request accepted stale cancellation");

    Need(AvCamUdeAsyncSubmit(&e, &token) == AVCAM_UDE_ASYNC_OK &&
         token != first, "Request ABA token reuse");
    Need(AvCamUdeAsyncCancel(&e, first) == AVCAM_UDE_ASYNC_NOT_FOUND,
         "Old completion token cancelled replacement request");
    Need(AvCamUdeAsyncCancel(&e, token) == AVCAM_UDE_ASYNC_OK,
         "Cancel while waiting failed");
    Need(AvCamUdeAsyncWorkerStart(&e, token) == AVCAM_UDE_ASYNC_BUSY,
         "Cancelled worker mapped request");
    Need(AvCamUdeAsyncClaimCompletion(&e, token) ==
         AVCAM_UDE_ASYNC_FINISH_NONE,
         "Cancelled waiting request completed before worker acknowledgment");
    Need(AvCamUdeAsyncWorkerDone(&e, token) == AVCAM_UDE_ASYNC_OK,
         "Worker could not abandon canceled waiting request");
    Need(AvCamUdeAsyncClaimCompletion(&e, token) ==
         AVCAM_UDE_ASYNC_FINISH_CANCELLED, "Cancellation lost");
    Need(AvCamUdeAsyncCompletionReturned(&e, token) == AVCAM_UDE_ASYNC_OK,
         "Cancelled waiting completion never drained");
    AssertInvariants(e);

    Need(AvCamUdeAsyncSubmit(&e, &token) == AVCAM_UDE_ASYNC_OK,
         "Third submission failed");
    Need(AvCamUdeAsyncWorkerStart(&e, token) == AVCAM_UDE_ASYNC_OK,
         "Worker could not begin");
    Need(AvCamUdeAsyncCancel(&e, token) == AVCAM_UDE_ASYNC_OK,
         "Cancel during encode rejected");
    Need(AvCamUdeAsyncClaimCompletion(&e, token) ==
         AVCAM_UDE_ASYNC_FINISH_NONE,
         "Worker-owned mapped buffer was completed on cancellation");
    Need(AvCamUdeAsyncWorkerDone(&e, token) == AVCAM_UDE_ASYNC_OK,
         "Cancelled worker could not release mapped buffer");
    Need(AvCamUdeAsyncClaimCompletion(&e, token) ==
         AVCAM_UDE_ASYNC_FINISH_CANCELLED,
         "In-flight cancellation not honored");
    Need(AvCamUdeAsyncCompletionReturned(&e, token) == AVCAM_UDE_ASYNC_OK,
         "In-flight cancel completion did not finish");
    AssertInvariants(e);
}

static void PurgeAndReset() {
    AVCAM_UDE_ASYNC_ISO e{};
    AvCamUdeAsyncInit(&e);
    Need(AvCamUdeAsyncStart(&e) == AVCAM_UDE_ASYNC_OK, "START failed");
    unsigned long long tokens[AVCAM_UDE_ASYNC_MAX_REQUESTS]{};
    for (unsigned i = 0; i < AVCAM_UDE_ASYNC_MAX_REQUESTS; ++i) {
        Need(AvCamUdeAsyncSubmit(&e, &tokens[i]) == AVCAM_UDE_ASYNC_OK,
             "Queue capacity unexpectedly low");
    }
    unsigned long long rejected = 0xdeadull;
    Need(AvCamUdeAsyncSubmit(&e, &rejected) == AVCAM_UDE_ASYNC_BUSY &&
         rejected == 0, "Full queue admitted an unbounded request");
    Need(AvCamUdeAsyncWorkerStart(&e, tokens[0]) ==
         AVCAM_UDE_ASYNC_OK, "First request not processing");
    Need(AvCamUdeAsyncBeginPurge(&e) == AVCAM_UDE_ASYNC_OK,
         "PURGE rejected");
    Need(AvCamUdeAsyncBeginPurge(&e) == AVCAM_UDE_ASYNC_BUSY,
         "Duplicate PURGE accepted");
    Need(AvCamUdeAsyncClaimPurgeAck(&e) == 0,
         "PURGE acknowledged with active requests");
    Need(AvCamUdeAsyncStart(&e) == AVCAM_UDE_ASYNC_BUSY,
         "Endpoint restarted before PURGE drain");
    for (unsigned i = 0; i < AVCAM_UDE_ASYNC_MAX_REQUESTS; ++i) {
        Need(AvCamUdeAsyncWorkerStart(&e, tokens[i]) ==
             AVCAM_UDE_ASYNC_BUSY, "Purged request started encoding");
        Need(AvCamUdeAsyncWorkerDone(&e, tokens[i]) ==
             AVCAM_UDE_ASYNC_OK, "Purged request did not release slot");
        Need(AvCamUdeAsyncClaimCompletion(&e, tokens[i]) ==
             AVCAM_UDE_ASYNC_FINISH_CANCELLED,
             "Purge allowed successful stale completion");
        Need(AvCamUdeAsyncClaimPurgeAck(&e) == 0,
             "PURGE acknowledged before native completion returned");
        Need(AvCamUdeAsyncCompletionReturned(&e, tokens[i]) ==
             AVCAM_UDE_ASYNC_OK,
             "Purged request completion did not return");
        Need(AvCamUdeAsyncClaimPurgeAck(&e) ==
             (i == AVCAM_UDE_ASYNC_MAX_REQUESTS - 1 ? 1 : 0),
             "Purge acknowledged too early or not at all");
    }
    Need(AvCamUdeAsyncClaimPurgeAck(&e) == 0,
         "Double PURGE acknowledgement");
    AssertInvariants(e);
    Need(AvCamUdeAsyncStart(&e) == AVCAM_UDE_ASYNC_OK,
         "New endpoint generation could not start");
    const auto epoch = e.Drain.Generation;
    unsigned long long token = 0;
    Need(AvCamUdeAsyncSubmit(&e, &token) == AVCAM_UDE_ASYNC_OK,
         "Submission after purge failed");
    Need(AvCamUdeAsyncWorkerStart(&e, token) == AVCAM_UDE_ASYNC_OK,
         "Work not started");
    Need(AvCamUdeAsyncBeginReset(&e) == AVCAM_UDE_ASYNC_OK,
         "RESET rejected");
    Need(AvCamUdeAsyncFinishReset(&e) == AVCAM_UDE_ASYNC_BUSY,
         "RESET completed before request drained");
    Need(AvCamUdeAsyncWorkerDone(&e, token) == AVCAM_UDE_ASYNC_OK,
         "Reset worker did not release buffer");
    Need(AvCamUdeAsyncClaimCompletion(&e, token) ==
         AVCAM_UDE_ASYNC_FINISH_CANCELLED,
         "Reset returned pre-reset JPEG bytes");
    Need(AvCamUdeAsyncFinishReset(&e) == AVCAM_UDE_ASYNC_BUSY,
         "RESET allowed while native completion still in progress");
    Need(AvCamUdeAsyncCompletionReturned(&e, token) == AVCAM_UDE_ASYNC_OK,
         "Reset-request completion did not drain");
    Need(AvCamUdeAsyncFinishReset(&e) == AVCAM_UDE_ASYNC_OK,
         "RESET could not establish new generation");
    Need(e.Drain.Generation == epoch + 1,
         "Reset did not invalidate old frame epoch");
    Need(AvCamUdeAsyncCancel(&e, token) == AVCAM_UDE_ASYNC_NOT_FOUND,
         "Stale token survived reset");
    AssertInvariants(e);
}

static void BoundedInterleavings(AVCAM_UDE_ASYNC_ISO e, unsigned depth) {
    AssertInvariants(e);
    if (depth == 0) return;
    for (unsigned action = 0; action < 11; ++action) {
        auto next = e;
        unsigned long long token = 0;
        for (const auto& slot : next.Slots) {
            if (slot.Phase != AVCAM_UDE_ASYNC_FREE) {
                token = slot.Token;
                break;
            }
        }
        switch (action) {
        case 0: (void)AvCamUdeAsyncStart(&next); break;
        case 1: {unsigned long long fresh = 0; (void)AvCamUdeAsyncSubmit(&next, &fresh); break;}
        case 2: (void)AvCamUdeAsyncCancel(&next, token); break;
        case 3: (void)AvCamUdeAsyncWorkerStart(&next, token); break;
        case 4: (void)AvCamUdeAsyncWorkerDone(&next, token); break;
        case 5: (void)AvCamUdeAsyncClaimCompletion(&next, token); break;
        case 6: (void)AvCamUdeAsyncCompletionReturned(&next, token); break;
        case 7: (void)AvCamUdeAsyncBeginPurge(&next); break;
        case 8: (void)AvCamUdeAsyncClaimPurgeAck(&next); break;
        case 9: (void)AvCamUdeAsyncBeginReset(&next); break;
        default: (void)AvCamUdeAsyncFinishReset(&next); break;
        }
        BoundedInterleavings(next, depth - 1);
    }
}

int main() {
    try {
        NormalAndCancelRace();
        PurgeAndReset();
        AVCAM_UDE_ASYNC_ISO e{};
        AvCamUdeAsyncInit(&e);
        BoundedInterleavings(e, 5u);
        std::cout << "Bounded async ISO cancellation/reset/purge interleavings passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
