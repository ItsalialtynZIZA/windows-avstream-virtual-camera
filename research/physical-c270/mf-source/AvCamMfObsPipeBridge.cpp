// Explicit opt-in, OBS-only, cross-session Media Foundation frame broker.
// User-mode process; NO RAW exposure, no driver, registry or camera changes.
// Pipe DACL: launching user (full), LocalService (read), SYSTEM (read).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <securitybaseapi.h>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include "AvCamMfPipe.h"
#include "AvCamMfColor.h"
#include "../AvCamRouterIpc.h"

namespace {
std::uint64_t NowMs() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}
bool Fresh(std::uint64_t now, std::uint64_t when) noexcept {
    return when && when <= now && now - when <= 2000;
}

class PipePermissions final {
public:
    PipePermissions() = default;
    PipePermissions(const PipePermissions&) = delete;
    PipePermissions& operator=(const PipePermissions&) = delete;
    ~PipePermissions() {
        if (descriptor_) LocalFree(descriptor_);
    }

    SECURITY_ATTRIBUTES* Build() noexcept {
        HANDLE processToken = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &processToken))
            return nullptr;
        DWORD size = 0;
        GetTokenInformation(processToken, TokenUser, nullptr, 0, &size);
        std::vector<std::uint8_t> buffer(size);
        const BOOL gotUser = size &&
            GetTokenInformation(processToken, TokenUser,
                                buffer.data(), size, &size);
        CloseHandle(processToken);
        if (!gotUser) return nullptr;
        const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());
        LPWSTR owner = nullptr;
        if (!ConvertSidToStringSidW(user->User.Sid, &owner)) return nullptr;
        const std::wstring sddl =
            L"D:P(A;;GA;;;" + std::wstring(owner) +
            L")(A;;GR;;;LS)(A;;GR;;;SY)";
        LocalFree(owner);
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl.c_str(), SDDL_REVISION_1, &descriptor_, nullptr))
            return nullptr;
        attributes_.nLength = sizeof(attributes_);
        attributes_.lpSecurityDescriptor = descriptor_;
        attributes_.bInheritHandle = FALSE;
        return &attributes_;
    }
private:
    PSECURITY_DESCRIPTOR descriptor_ = nullptr;
    SECURITY_ATTRIBUTES attributes_{};
};

bool ConnectClient(HANDLE pipe) {
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!event) return false;
    OVERLAPPED pending{};
    pending.hEvent = event;
    BOOL connected = ConnectNamedPipe(pipe, &pending);
    if (!connected) {
        const DWORD error = GetLastError();
        if (error == ERROR_PIPE_CONNECTED) {
            connected = TRUE;
        } else if (error == ERROR_IO_PENDING) {
            if (WaitForSingleObject(event, 8000) == WAIT_OBJECT_0) {
                DWORD ignored = 0;
                connected = GetOverlappedResult(
                    pipe, &pending, &ignored, FALSE);
            } else {
                CancelIoEx(pipe, &pending);
                DWORD ignored = 0;
                GetOverlappedResult(pipe, &pending, &ignored, TRUE);
            }
        }
    }
    CloseHandle(event);
    return connected != FALSE;
}

int Run(unsigned frameLimit) {
    PipePermissions permissions;
    SECURITY_ATTRIBUTES* acl = permissions.Build();
    if (!acl) return 5;
    HANDLE pipe = CreateNamedPipeW(
        avcam::mfpipe::Name,
        PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1, static_cast<DWORD>(avcam::mfpipe::PayloadBytes * 2),
        0, 0, acl);
    if (pipe == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        std::wcerr << L"AVCAM_MF_PIPE_CREATE_FAILED win32=" << error << L"\n";
        return 6;
    }
    std::wcout << L"AVCAM_MF_PIPE_SERVER_READY obs_only=1 local_service_read=1\n"
               << std::flush;

    unsigned published = 0;
    avcam::ipc::Subscriber obs;
    avcam::ipc::Cursor cursor{};
    std::vector<std::uint8_t> nv12(avcam::mfcolor::Nv12Bytes);
    while (frameLimit == 0 || published < frameLimit) {
        if (!ConnectClient(pipe)) continue;
        std::wcout << L"AVCAM_MF_PIPE_CLIENT_CONNECTED\n" << std::flush;
        // Only the processed OBS mapping is opened. Never access RAW.
        while (frameLimit == 0 || published < frameLimit) {
            if (!obs.Attached() && !obs.Start(avcam::ipc::Channel::Obs)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
                continue;
            }
            avcam::ipc::Snapshot snapshot;
            if (!obs.TryRead(snapshot, cursor)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(3));
                continue;
            }
            cursor = snapshot.Position();
            if (snapshot.channel != avcam::ipc::Channel::Obs ||
                snapshot.bgra.size() != avcam::mfcolor::BgraBytes ||
                !Fresh(NowMs(), snapshot.capturedMs))
                continue;
            if (!avcam::mfcolor::Convert(snapshot.bgra.data(),
                                         snapshot.bgra.size(),
                                         nv12.data(), nv12.size())) continue;
            const avcam::mfpipe::FrameHeader header{
                avcam::mfpipe::Magic, avcam::mfpipe::Version,
                static_cast<std::uint32_t>(nv12.size()),
                avcam::mfpipe::Width, avcam::mfpipe::Height,
                avcam::mfpipe::FormatNV12, snapshot.session,
                snapshot.sequence, snapshot.capturedMs
            };
            if (!avcam::mfpipe::WriteExactly(pipe, &header, sizeof(header)) ||
                !avcam::mfpipe::WriteExactly(pipe, nv12.data(), nv12.size()))
                break;
            ++published;
        }
        DisconnectNamedPipe(pipe);
    }
    obs.Stop();
    CloseHandle(pipe);
    std::wcout << L"AVCAM_MF_PIPE_OBS_FRAMES=" << published << L"\n";
    return published ? 0 : 7;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && std::wstring(argv[1]) == L"--continuous")
        return Run(0);
    if (argc == 3 && std::wstring(argv[1]) == L"--frames") {
        wchar_t* end = nullptr;
        const unsigned long limit = wcstoul(argv[2], &end, 10);
        if (!end || *end || !limit || limit > 100000)
            return 2;
        return Run(static_cast<unsigned>(limit));
    }
    std::wcerr << L"Usage: AvCamMfObsPipeBridge.exe --continuous | --frames N\n";
    return 2;
}
