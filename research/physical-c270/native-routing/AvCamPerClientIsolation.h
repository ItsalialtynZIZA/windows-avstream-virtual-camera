#pragma once
// AvCam STRICT per-consumer frame policy, independent of OS capture APIs.
// This intentionally is NOT an endpoint hook/driver nor a way to distinguish
// unchanged Chrome from Deep Live Cam. Native per-client attestation must be
// implemented by a separate documented Windows adapter before use.
//
// RAW never receives OBS bytes; OBS route never falls back to RAW.
// No process-name guesses, USB identity spoofing or device mutations.
#include "../PhysicalEndpointAcceptance.h"
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace avcam::per_client {

using SessionId = std::uint64_t;
enum class Channel : unsigned { Raw = 1, Obs = 2 };

struct Frame {
    std::vector<std::uint8_t> bytes;
    std::uint64_t publisherEpoch = 0; // New, nonzero value after sender restart.
    std::uint64_t sequence = 0;       // Increasing within publisher epoch.
    std::uint64_t capturedMs = 0;     // Same monotonic clock as nowMs.
};
struct Delivery {
    Frame frame;
    Channel channel = Channel::Raw;
};

// Adapter-owned source proof. A process name or arbitrary caller string is
// NEVER sufficient. There is currently no production Windows adapter that
// can issue this proof for two consumers of the same physical C270 endpoint.
// Test code can fabricate it ONLY to exercise the policy in isolation.
struct NativeClientProof {
    SessionId nativeSessionId = 0;
    std::wstring originalPhysicalInterface;
    std::wstring clientSelectedInterface;
    std::wstring originalPnpInstanceId;
    bool independentlyAuthenticatedClient = false;
    bool frameDeliveryIsolatedToThisClient = false;
};

enum class AttachStatus {
    Attached,
    RejectedUnprovenIsolation,
    RejectedPhysicalEndpoint,
    RejectedDuplicateSession,
    RejectedShutdown
};

class Router final {
public:
    explicit Router(std::uint64_t maxAgeMs = 1000)
        : maxAgeMs_(maxAgeMs) {}

    Router(const Router&) = delete;
    Router& operator=(const Router&) = delete;

    AttachStatus Attach(const NativeClientProof& proof, Channel channel) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (shutdown_) return AttachStatus::RejectedShutdown;
        // Attestation flags are consumed ONLY from a native trusted adapter.
        // This repository intentionally provides no such adapter yet.
        if (!proof.nativeSessionId ||
            !proof.independentlyAuthenticatedClient ||
            !proof.frameDeliveryIsolatedToThisClient)
            return AttachStatus::RejectedUnprovenIsolation;
        if (!acceptance::IsExistingPhysicalC270Interface(
                proof.originalPhysicalInterface,
                proof.clientSelectedInterface,
                proof.originalPnpInstanceId))
            return AttachStatus::RejectedPhysicalEndpoint;
        if (sessions_.count(proof.nativeSessionId))
            return AttachStatus::RejectedDuplicateSession;
        sessions_.emplace(proof.nativeSessionId, channel);
        return AttachStatus::Attached;
    }

    bool Publish(Channel channel, Frame frame) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (shutdown_ || frame.bytes.empty() || !frame.publisherEpoch ||
            !frame.sequence || !frame.capturedMs)
            return false;

        Frame& previous = channel == Channel::Raw ? raw_ : obs_;
        // A new OBS publisher may start only after the previous epoch was
        // explicitly disconnected. Never allow a stale parallel writer to
        // replace the current source simply by choosing another epoch.
        if (previous.publisherEpoch &&
            previous.publisherEpoch != frame.publisherEpoch)
            return false;
        if (previous.publisherEpoch == frame.publisherEpoch &&
            frame.sequence <= previous.sequence)
            return false;
        previous = std::move(frame);
        return true;
    }

    // No implicit routing by executable name. No OBS -> RAW fallback.
    // A non-registered native session never reads either video channel.
    bool Read(SessionId session, std::uint64_t nowMs, Delivery& out) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (shutdown_) return false;
        const auto it = sessions_.find(session);
        if (it == sessions_.end()) return false;
        const Frame& frame =
            it->second == Channel::Raw ? raw_ : obs_;
        if (frame.bytes.empty() || nowMs < frame.capturedMs ||
            nowMs - frame.capturedMs > maxAgeMs_)
            return false;
        out.frame = frame;  // private copy; never share mutable buffers.
        out.channel = it->second;
        return true;
    }

    void DisconnectPublisher(Channel channel) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (channel == Channel::Raw) raw_ = {};
        else obs_ = {};
    }

    void Detach(SessionId session) {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_.erase(session);
    }

    void Shutdown() {
        std::lock_guard<std::mutex> lock(mutex_);
        shutdown_ = true;
        sessions_.clear();
        raw_ = {};
        obs_ = {};
    }

    std::size_t ActiveSessions() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return sessions_.size();
    }

private:
    mutable std::mutex mutex_;
    bool shutdown_ = false;
    std::uint64_t maxAgeMs_;
    std::map<SessionId, Channel> sessions_;
    Frame raw_;
    Frame obs_;
};
} // namespace avcam::per_client
