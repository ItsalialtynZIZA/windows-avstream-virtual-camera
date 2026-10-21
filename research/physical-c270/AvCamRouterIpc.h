#pragma once
// Optional, per-session BGRA frame IPC for the application-space AvCam router.
// Does not install a camera, modify its identity, or intercept any application.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include <utility>

namespace avcam::ipc {
constexpr std::uint32_t Width = 1280;
constexpr std::uint32_t Height = 720;
constexpr std::uint32_t Stride = Width * 4;
constexpr std::size_t FrameBytes = static_cast<std::size_t>(Stride) * Height;
constexpr std::uint32_t Magic = 0x31435641u; // "AVC1"
constexpr std::uint32_t Version = 1;
enum class Channel : std::uint32_t { Raw = 1, Obs = 2 };

inline const wchar_t* MappingName(Channel channel) noexcept {
    return channel == Channel::Raw ? L"Local\\AvCamRouterRawV1"
                                   : L"Local\\AvCamRouterObsV1";
}
struct Header {
    volatile LONG epoch;   // odd = writer busy; even = consistent snapshot
    volatile LONG active;  // cleared on OBS disconnect
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t channel;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t stride;
    std::uint32_t bytes;
    std::uint64_t sequence;
    std::uint64_t capturedMs; // steady_clock milliseconds; not wall time
};
struct Region {
    Header header;
    unsigned char bgra[FrameBytes];
};
struct Snapshot {
    std::vector<unsigned char> bgra;
    std::uint64_t sequence = 0;
    std::uint64_t capturedMs = 0;
    Channel channel = Channel::Raw;
};

// Each channel has exactly one publisher. Never attach to another writer's
// mapping: concurrent writers would break the snapshot consistency contract.
class Publisher final {
public:
    Publisher() = default;
    Publisher(const Publisher&) = delete;
    Publisher& operator=(const Publisher&) = delete;
    ~Publisher() { Stop(); }

    bool Start(Channel channel) noexcept {
        Stop();
        const HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
            PAGE_READWRITE, 0, static_cast<DWORD>(sizeof(Region)), MappingName(channel));
        if (!mapping) return false;
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            CloseHandle(mapping);
            SetLastError(ERROR_ALREADY_EXISTS);
            return false;
        }
        auto* region = static_cast<Region*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS,
                                                           0, 0, sizeof(Region)));
        if (!region) {
            const DWORD error = GetLastError();
            CloseHandle(mapping);
            SetLastError(error);
            return false;
        }
        mapping_ = mapping;
        region_ = region;
        // New pagefile-backed mappings start zeroed. Set metadata before
        // readers can see an active snapshot.
        region_->header.magic = Magic;
        region_->header.version = Version;
        region_->header.channel = static_cast<std::uint32_t>(channel);
        region_->header.width = Width;
        region_->header.height = Height;
        region_->header.stride = Stride;
        region_->header.bytes = static_cast<std::uint32_t>(FrameBytes);
        return true;
    }
    bool Publish(const unsigned char* bgra, std::size_t size,
                 std::uint64_t sequence, std::uint64_t capturedMs) noexcept {
        if (!region_ || !bgra || size != FrameBytes || sequence == 0) return false;
        InterlockedIncrement(&region_->header.epoch);
        std::memcpy(region_->bgra, bgra, FrameBytes);
        region_->header.sequence = sequence;
        region_->header.capturedMs = capturedMs;
        InterlockedExchange(&region_->header.active, 1);
        MemoryBarrier();
        InterlockedIncrement(&region_->header.epoch);
        return true;
    }
    void Invalidate() noexcept {
        if (!region_) return;
        InterlockedIncrement(&region_->header.epoch);
        InterlockedExchange(&region_->header.active, 0);
        MemoryBarrier();
        InterlockedIncrement(&region_->header.epoch);
    }
    void Stop() noexcept {
        if (region_) {
            Invalidate();
            UnmapViewOfFile(region_);
            region_ = nullptr;
        }
        if (mapping_) {
            CloseHandle(mapping_);
            mapping_ = nullptr;
        }
    }
private:
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
        auto* region = static_cast<const Region*>(MapViewOfFile(mapping, FILE_MAP_READ,
                                                                 0, 0, sizeof(Region)));
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
    // A consumer can pass its last sequence to avoid copying stale 3.5 MiB frames.
    bool TryRead(Snapshot& output, std::uint64_t afterSequence = 0) const {
        if (!region_) return false;
        const Header& h = region_->header;
        const LONG before = h.epoch;
        if ((before & 1) || !h.active) return false;
        MemoryBarrier();
        if (h.magic != Magic || h.version != Version ||
            h.channel != static_cast<std::uint32_t>(channel_) ||
            h.width != Width || h.height != Height ||
            h.stride != Stride || h.bytes != FrameBytes || !h.sequence)
            return false;
        const std::uint64_t seq = h.sequence;
        if (seq <= afterSequence) return false;
        const std::uint64_t time = h.capturedMs;
        // Do not copy to the caller until a complete, stable frame is verified.
        std::vector<unsigned char> bytes(FrameBytes);
        std::memcpy(bytes.data(), region_->bgra, FrameBytes);
        MemoryBarrier();
        const LONG after = h.epoch;
        if (before != after || (after & 1) || !h.active) return false;
        output.bgra = std::move(bytes);
        output.sequence = seq;
        output.capturedMs = time;
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
