#pragma once
// Windows application-space RAW/OBS V2 IPC -> independent per-client router.
// This only feeds the internal AvCam frame policy. It does not open a camera,
// activate FrameServer, enumerate a new endpoint, authenticate Chrome clients,
// or modify Logitech drivers. A future OS adapter remains unimplemented.
#include "../AvCamRouterIpc.h"
#include "AvCamPerClientIsolation.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace avcam::native_ipc {

inline std::uint64_t NowMs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

class Ingest final {
public:
    explicit Ingest(per_client::Router& router,
                    std::uint64_t maxAgeMs = 1000)
        : router_(router), maxAgeMs_(maxAgeMs) {}
    Ingest(const Ingest&) = delete;
    Ingest& operator=(const Ingest&) = delete;
    ~Ingest() { Stop(); }

    // One finite nonblocking snapshot attempt per channel. The two writers
    // are independent. Failed OBS transport cannot alter published RAW.
    void Poll() {
        if (stopped_) return;
        PollChannel(raw_, ipc::Channel::Raw, per_client::Channel::Raw);
        PollChannel(obs_, ipc::Channel::Obs, per_client::Channel::Obs);
    }

    void Stop() {
        if (stopped_) return;
        stopped_ = true;
        raw_.reader.Stop();
        obs_.reader.Stop();
        router_.DisconnectPublisher(per_client::Channel::Raw);
        router_.DisconnectPublisher(per_client::Channel::Obs);
    }

private:
    struct State {
        ipc::Subscriber reader;
        ipc::Cursor cursor{};
        std::uint64_t lastSession = 0;
        std::uint64_t lastAcceptedAtMs = 0;
        bool valid = false;
    };

    void Invalidate(State& state, per_client::Channel channel) {
        if (state.valid) router_.DisconnectPublisher(channel);
        state.valid = false;
        state.lastSession = 0;
        state.lastAcceptedAtMs = 0;
    }

    void PollChannel(State& state, ipc::Channel ipcChannel,
                     per_client::Channel routeChannel) {
        const auto nowMs = NowMs();
        if (!state.reader.Attached() && !state.reader.Start(ipcChannel)) {
            Invalidate(state, routeChannel);
            return;
        }

        // V2's active bit resets on normal owner exit; on forced termination
        // it can remain set, so freshness also bounds any abandoned frame.
        if (!state.reader.IsActive()) {
            Invalidate(state, routeChannel);
        }

        ipc::Snapshot snapshot;
        if (!state.reader.TryRead(snapshot, state.cursor)) {
            if (state.valid &&
                (nowMs < state.lastAcceptedAtMs ||
                 nowMs - state.lastAcceptedAtMs > maxAgeMs_))
                Invalidate(state, routeChannel);
            return;
        }
        state.cursor = snapshot.Position();
        if (snapshot.channel != ipcChannel ||
            snapshot.bgra.size() != ipc::FrameBytes ||
            !snapshot.session || !snapshot.sequence || !snapshot.capturedMs ||
            nowMs < snapshot.capturedMs ||
            nowMs - snapshot.capturedMs > maxAgeMs_) {
            Invalidate(state, routeChannel);
            return;
        }

        // New writer acquired the V2 publisher lease and therefore has a
        // new frame session. Revoke the previous epoch before accepting it.
        if (state.valid && state.lastSession != snapshot.session)
            Invalidate(state, routeChannel);
        per_client::Frame frame;
        frame.bytes = std::move(snapshot.bgra);
        frame.publisherEpoch = snapshot.session;
        frame.sequence = snapshot.sequence;
        frame.capturedMs = snapshot.capturedMs;

        if (!router_.Publish(routeChannel, std::move(frame))) {
            // Bad/out-of-order data must not leave a stale processed output.
            Invalidate(state, routeChannel);
            return;
        }
        state.valid = true;
        state.lastSession = snapshot.session;
        state.lastAcceptedAtMs = nowMs;
    }

    per_client::Router& router_;
    std::uint64_t maxAgeMs_;
    State raw_;
    State obs_;
    bool stopped_ = false;
};
} // namespace avcam::native_ipc
