#include <iostream>
#include <stdexcept>
#include "../AvCamUdeRequestLifecycle.h"

static void Expect(bool valid, const char* message) {
    if (!valid) throw std::runtime_error(message);
}

static void CompletionNormally() {
    AVCAM_UDE_REQUEST_LIFECYCLE request{};
    AvCamUdeRequestInit(&request);
    Expect(AvCamUdeRequestClaimCompletion(&request) ==
           AVCAM_UDE_COMPLETION_NONE, "Worker-owned buffer must not be completed");
    Expect(AvCamUdeRequestWorkerFinished(&request) == 1,
           "Encoder must be allowed to release the buffer");
    Expect(AvCamUdeRequestClaimCompletion(&request) ==
           AVCAM_UDE_COMPLETION_DATA, "Completed request must report data");
    Expect(AvCamUdeRequestClaimCompletion(&request) ==
           AVCAM_UDE_COMPLETION_NONE, "Request must never complete twice");
    Expect(AvCamUdeRequestMarkCancelled(&request) == 0,
           "Late cancellation must not change already completed request");
    Expect(AvCamUdeRequestWorkerFinished(&request) == 0,
           "Duplicate worker finished event must be rejected");
}

static void CancellationBeforeWorkerFinish() {
    AVCAM_UDE_REQUEST_LIFECYCLE request{};
    AvCamUdeRequestInit(&request);
    Expect(AvCamUdeRequestMarkCancelled(&request) == 1,
           "Cancellation must be recorded");
    Expect(AvCamUdeRequestClaimCompletion(&request) ==
           AVCAM_UDE_COMPLETION_NONE,
           "Cancelled worker-owned buffer must not be released early");
    Expect(AvCamUdeRequestWorkerFinished(&request) == 1,
           "Worker must acknowledge cancellation");
    Expect(AvCamUdeRequestClaimCompletion(&request) ==
           AVCAM_UDE_COMPLETION_CANCELLED,
           "Cancellation must win before completion is claimed");
    Expect(AvCamUdeRequestClaimCompletion(&request) ==
           AVCAM_UDE_COMPLETION_NONE,
           "Cancelled request must not be completed twice");
}

static void CancellationAfterWorkerFinish() {
    AVCAM_UDE_REQUEST_LIFECYCLE request{};
    AvCamUdeRequestInit(&request);
    Expect(AvCamUdeRequestWorkerFinished(&request) == 1,
           "Worker finish must be accepted");
    Expect(AvCamUdeRequestMarkCancelled(&request) == 1,
           "Cancellation arriving before claim is accepted");
    Expect(AvCamUdeRequestClaimCompletion(&request) ==
           AVCAM_UDE_COMPLETION_CANCELLED,
           "Pending cancellation must override prepared data");
}

static void InvalidCalls() {
    Expect(AvCamUdeRequestWorkerFinished(nullptr) == 0,
           "Null request must not be modified");
    Expect(AvCamUdeRequestMarkCancelled(nullptr) == 0,
           "Null request cannot be cancelled");
    Expect(AvCamUdeRequestClaimCompletion(nullptr) ==
           AVCAM_UDE_COMPLETION_NONE,
           "Null request must not complete");
}

int main() {
    try {
        CompletionNormally();
        CancellationBeforeWorkerFinish();
        CancellationAfterWorkerFinish();
        InvalidCalls();
        std::cout << "UdeCx request lifecycle single-completion tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
