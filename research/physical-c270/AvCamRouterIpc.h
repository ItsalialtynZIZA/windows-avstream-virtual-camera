#pragma once
// AvCam application-space BGRA transport V2. A reader may keep a mapping
// open while its writer restarts. This is not a camera driver or endpoint.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace avcam::ipc {
constexpr std::uint32_t Width = 1280;
constexpr std::uint32_t Height = 720;
constexpr std::uint32_t Stride = Width * 4;
constexpr std::size_t FrameBytes = static_cast<std::size_t>(Stride) * Height;
constexpr std::uint32_t Magic = 0x32435641u; // "AVC2"
constexpr std::uint32_t Version = 2;
enum class Channel : std::uint32_t { Raw = 1, Obs = 2 };

inline const wchar_t* MappingName(Channel channel) noexcept {
    return channel == Channel::Raw ? L"Local\\AvCamRouterRawV2"
                                   : L"Local\\AvCamRouterObsV2";
}
inline const wchar_t* WriterName(Channel channel) noexcept {
    return channel == Channel::Raw ? L"Local\\AvCamRouterRawOwnerV2"
                                   : L"Local\\AvCamRouterObsOwnerV2";
}

struct Header {
    volatile LONG epoch;  // odd = writer mutating; even = complete snapshot
    volatile LONG active; // cleared on disconnect or normal publisher exit
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t channel;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t stride;
    std::uint32_t bytes;
    std::uint64_t session;    // changes when a new publisher acquires channel
    std::uint64_t sequence;   // restarts from 1 in each publisher session
    std::uint64_t capturedMs; // steady_clock milliseconds, not wall clock
};
struct Region { Header header; unsigned char bgra[FrameBytes]; };
struct Cursor {
    std::uint64_t session = 0;
    std::uint64_t sequence = 0;
};
struct Snapshot {
    std::vector<unsigned char> bgra;
    std::uint64_t session = 0;
    std::uint64_t sequence = 0;
    std::uint64_t capturedMs = 0;
    Channel channel = Channel::Raw;
    Cursor Position() const noexcept { return {session, sequence}; }
};

inline std::uint64_t NewSession() noexcept {
    // Use a per-process high-half nonce and a monotonic per-process
    // low-half counter. Session IDs cannot repeat within one process until
    // billions of restarts, even if two restarts share the same clock tick.
    static const std::uint64_t processNonce = [] {
        FILETIME stamp{};
        GetSystemTimePreciseAsFileTime(&stamp);
        const auto time = (static_cast<std::uint64_t>(stamp.dwHighDateTime) << 32)
                        | stamp.dwLowDateTime;
        const auto pid = static_cast<std::uint64_t>(GetCurrentProcessId());
        const auto seed = time ^ (time >> 32)
                        ^ (pid * 0x9e3779b97f4a7c15ull);
        return (seed & 0xffffffffull) << 32;
    }();
    static std::atomic<std::uint32_t> counter{0};
    const auto number = counter.fetch_add(1, std::memory_order_relaxed) + 1;
    return processNonce | number;
}

class Publisher final {
public:
    Publisher() = default;
    Publisher(const Publisher&) = delete;
    Publisher& operator=(const Publisher&) = delete;
    ~Publisher() { Stop(); }

    bool Start(Channel channel) noexcept {
        Stop();
        // This named mutex is a *lifetime lease*, not a held mutex lock:
        // only publishers create/retain it. A second writer, including one
        // on the same thread, fails while its name already exists. Reader
        // handles keep the mapping alive but NEVER retain the owner lease.
        HANDLE owner = CreateMutexW(nullptr, FALSE, WriterName(channel));
        if (!owner) return false;
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            CloseHandle(owner);
            SetLastError(ERROR_ALREADY_EXISTS);
            return false;
        }
        const HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
            PAGE_READWRITE, 0, static_cast<DWORD>(sizeof(Region)), MappingName(channel));
        if (!mapping) {
            const DWORD error = GetLastError();
            CloseHandle(owner);
            SetLastError(error);
            return false;
        }
        // A prior viewer may retain the old mapping. Reuse it only while
        // holding the new exclusive writer lease. The requested MapView size
        // also rejects an existing, incompatible (too small) mapping.
        auto* region = static_cast<Region*>(MapViewOfFile(
            mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Region)));
        if (!region) {
            const DWORD error = GetLastError();
            CloseHandle(mapping);
            CloseHandle(owner);
            SetLastError(error);
            return false;
        }
        owner_ = owner;
        mapping_ = mapping;
        region_ = region;
        Header& h = region_->header;
        const LONG prior = InterlockedCompareExchange(&h.epoch, 0, 0);
        const LONG busy = static_cast<LONG>(
            (static_cast<std::uint32_t>(prior) & ~1u) | 1u);
        InterlockedExchange(&h.epoch, busy);
        InterlockedExchange(&h.active, 0);
        MemoryBarrier();
        h.magic = Magic;
        h.version = Version;
        h.channel = static_cast<std::uint32_t>(channel);
        h.width = Width;
        h.height = Height;
        h.stride = Stride;
        h.bytes = static_cast<std::uint32_t>(FrameBytes);
        h.session = NewSession();
        h.sequence = 0;
        h.capturedMs = 0;
        MemoryBarrier();
        InterlockedExchange(&h.epoch,
            static_cast<LONG>(static_cast<std::uint32_t>(busy) + 1u));
        return true;
    }

    bool Publish(const unsigned char* bgra, std::size_t size,
                 std::uint64_t sequence, std::uint64_t capturedMs) noexcept {
        if (!region_ || !bgra || size != FrameBytes || !sequence) return false;
        Header& h = region_->header;
        InterlockedIncrement(&h.epoch);
        std::memcpy(region_->bgra, bgra, FrameBytes);
        h.sequence = sequence;
        h.capturedMs = capturedMs;
        InterlockedExchange(&h.active, 1);
        MemoryBarrier();
        InterlockedIncrement(&h.epoch);
        return true;
    }

    void Invalidate() noexcept {
        if (!region_) return;
        Header& h = region_->header;
        InterlockedIncrement(&h.epoch);
        InterlockedExchange(&h.active, 0);
        MemoryBarrier();
        InterlockedIncrement(&h.epoch);
    }

    void Stop() noexcept {
        if (region_) {
            Invalidate();
            UnmapViewOfFile(region_);
            region_ = nullptr;
        }
        if (mapping_) { CloseHandle(mapping_); mapping_ = nullptr; }
        if (owner_) { CloseHandle(owner_); owner_ = nullptr; }
    }
private:
    HANDLE owner_ = nullptr;
    HANDLE mapping_ = nullptr;
    Region* region_ = nullptr;
};

class Subscriber final {
public:
    Subscriber() = default;
    Subscriber(const Subscriber&) = delete;
    Subscriber& operator=(const Subscriber&) = delete;
    ~Subscriber() { Stop(); }

    bool Start(Channel channel) noexcept {
        Stop();
        const HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, MappingName(channel));
        if (!mapping) return false;
        auto* region = static_cast<const Region*>(MapViewOfFile(
            mapping, FILE_MAP_READ, 0, 0, sizeof(Region)));
        if (!region) {
            const DWORD error = GetLastError();
            CloseHandle(mapping);
            SetLastError(error);
            return false;
        }
        mapping_ = mapping;
        region_ = region;
        channel_ = channel;
        return true;
    }

    bool IsActive() const noexcept {
        return region_ && region_->header.active != 0;
    }

    bool TryRead(Snapshot& output, Cursor after = {}) const {
        if (!region_) return false;
        const Header& h = region_->header;
        const LONG before = h.epoch;
        if ((before & 1) || !h.active) return false;
        MemoryBarrier();
        if (h.magic != Magic || h.version != Version ||
            h.channel != static_cast<std::uint32_t>(channel_) ||
            h.width != Width || h.height != Height ||
            h.stride != Stride || h.bytes != FrameBytes)
            return false;
        const auto session = h.session;
        const auto sequence = h.sequence;
        if (!session || !sequence ||
            (session == after.session && sequence <= after.sequence))
            return false;
        const auto capturedMs = h.capturedMs;
        std::vector<unsigned char> bytes(FrameBytes);
        std::memcpy(bytes.data(), region_->bgra, FrameBytes);
        MemoryBarrier();
        const LONG afterEpoch = h.epoch;
        if (before != afterEpoch || (afterEpoch & 1) || !h.active ||
            session != h.session) return false;
        output.bgra = std::move(bytes);
        output.session = session;
        output.sequence = sequence;
        output.capturedMs = capturedMs;
        output.channel = channel_;
        return true;
    }

    void Stop() noexcept {
        if (region_) { UnmapViewOfFile(region_); region_ = nullptr; }
        if (mapping_) { CloseHandle(mapping_); mapping_ = nullptr; }
    }
private:
    HANDLE mapping_ = nullptr;
    const Region* region_ = nullptr;
    Channel channel_ = Channel::Raw;
};
} // namespace avcam::ipc
