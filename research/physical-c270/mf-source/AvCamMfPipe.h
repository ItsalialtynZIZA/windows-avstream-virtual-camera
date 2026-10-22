#pragma once
// AvCam OBS-only frame bridge from an interactive user's session to
// Windows Camera Frame Server (LocalService in session 0).
// Named pipes, unlike Local\ file mappings, work across Windows sessions.
// This protocol ONLY transports processed NV12 OBS pixels; RAW is forbidden.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace avcam::mfpipe {
constexpr wchar_t Name[] =
    L"\\\\.\\pipe\\AvCam.Obs.Nv12.V1.8AF84A8D";
constexpr std::uint32_t Magic = 0x31434d41u; // "AMC1"
constexpr std::uint32_t Version = 1;
constexpr std::uint32_t Width = 1280;
constexpr std::uint32_t Height = 720;
constexpr std::uint32_t FormatNV12 = 0x3231564eu; // "NV12"
constexpr std::size_t PayloadBytes =
    static_cast<std::size_t>(Width) * Height * 3u / 2u;
constexpr DWORD IoTimeoutMs = 1200u;

#pragma pack(push, 1)
struct FrameHeader {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t bytes;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t format;
    std::uint64_t session;
    std::uint64_t sequence;
    std::uint64_t capturedMs;
};
#pragma pack(pop)
static_assert(sizeof(FrameHeader) == 48, "Pipe wire format cannot change implicitly");

inline bool ValidHeader(const FrameHeader& h) noexcept {
    return h.magic == Magic && h.version == Version &&
           h.bytes == PayloadBytes && h.width == Width &&
           h.height == Height && h.format == FormatNV12 &&
           h.session != 0 && h.sequence != 0 && h.capturedMs != 0;
}

inline void CloseHandleIfOpen(HANDLE& handle) noexcept {
    if (handle && handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    handle = INVALID_HANDLE_VALUE;
}

// A bounded-overlapped I/O operation: never hold MF Stop indefinitely if
// the writer dies while the service is waiting for the next sample.
inline bool ReadExactly(HANDLE pipe, void* dst, std::size_t bytes,
                        DWORD timeoutMs = IoTimeoutMs) noexcept {
    if (!dst || pipe == INVALID_HANDLE_VALUE) return false;
    auto* ptr = static_cast<unsigned char*>(dst);
    std::size_t done = 0;
    while (done < bytes) {
        HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!event) return false;
        OVERLAPPED pending{};
        pending.hEvent = event;
        DWORD transferred = 0;
        const auto chunk = static_cast<DWORD>(
            (bytes - done) > 0x7fffffffu ? 0x7fffffffu : bytes - done);
        BOOL ok = ReadFile(pipe, ptr + done, chunk, &transferred, &pending);
        if (!ok && GetLastError() == ERROR_IO_PENDING) {
            const auto wait = WaitForSingleObject(event, timeoutMs);
            if (wait == WAIT_OBJECT_0) {
                ok = GetOverlappedResult(pipe, &pending, &transferred, FALSE);
            } else {
                CancelIoEx(pipe, &pending);
                // Cancellation is asynchronous; keep stack OVERLAPPED alive
                // until the kernel has completed the operation.
                GetOverlappedResult(pipe, &pending, &transferred, TRUE);
                ok = FALSE;
            }
        }
        CloseHandle(event);
        if (!ok || !transferred) return false;
        done += transferred;
    }
    return true;
}

inline bool WriteExactly(HANDLE pipe, const void* data, std::size_t bytes,
                         DWORD timeoutMs = IoTimeoutMs) noexcept {
    if (!data || pipe == INVALID_HANDLE_VALUE) return false;
    const auto* ptr = static_cast<const unsigned char*>(data);
    std::size_t done = 0;
    while (done < bytes) {
        HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!event) return false;
        OVERLAPPED pending{};
        pending.hEvent = event;
        DWORD transferred = 0;
        const auto chunk = static_cast<DWORD>(
            (bytes - done) > 0x7fffffffu ? 0x7fffffffu : bytes - done);
        BOOL ok = WriteFile(pipe, ptr + done, chunk, &transferred, &pending);
        if (!ok && GetLastError() == ERROR_IO_PENDING) {
            const auto wait = WaitForSingleObject(event, timeoutMs);
            if (wait == WAIT_OBJECT_0)
                ok = GetOverlappedResult(pipe, &pending, &transferred, FALSE);
            else {
                CancelIoEx(pipe, &pending);
                GetOverlappedResult(pipe, &pending, &transferred, TRUE);
                ok = FALSE;
            }
        }
        CloseHandle(event);
        if (!ok || !transferred) return false;
        done += transferred;
    }
    return true;
}

struct Frame {
    FrameHeader header{};
    std::vector<std::uint8_t> nv12;
};

class Reader final {
public:
    Reader() = default;
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;
    ~Reader() { Close(); }

    bool Connect() noexcept {
        Close();
        // PIPE_REJECT_REMOTE_CLIENTS on the server excludes network traffic.
        // The server DACL permits only its owner, LocalService and SYSTEM.
        pipe_ = CreateFileW(Name, GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                            FILE_FLAG_OVERLAPPED, nullptr);
        return pipe_ != INVALID_HANDLE_VALUE;
    }
    bool Attached() const noexcept { return pipe_ != INVALID_HANDLE_VALUE; }
    bool Next(Frame& frame) {
        if (!Attached() && !Connect()) return false;
        FrameHeader h{};
        if (!ReadExactly(pipe_, &h, sizeof(h)) || !ValidHeader(h)) {
            Close();
            return false;
        }
        std::vector<std::uint8_t> buffer(PayloadBytes);
        if (!ReadExactly(pipe_, buffer.data(), buffer.size())) {
            Close();
            return false;
        }
        if (h.session == lastSession_ && h.sequence <= lastSequence_) {
            Close();
            return false;
        }
        lastSession_ = h.session;
        lastSequence_ = h.sequence;
        frame.header = h;
        frame.nv12 = std::move(buffer);
        return true;
    }
    void Close() noexcept { CloseHandleIfOpen(pipe_); }
private:
    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    std::uint64_t lastSession_ = 0;
    std::uint64_t lastSequence_ = 0;
};
} // namespace avcam::mfpipe
