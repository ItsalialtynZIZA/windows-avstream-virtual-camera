// On-device diagnostic for Windows Camera Frame Server's LocalService
// session-0 read access to the OBS-only AvCam NV12 broker pipe.
// NO virtual camera installation, USB changes, COM registration, or VM.
// --probe --accept-temporary-service requires explicit elevated consent;
// the unique temporary Win32 service is deleted after its bounded test.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <winsvc.h>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>
#include "AvCamMfPipe.h"

namespace {
constexpr wchar_t ServicePrefix[] = L"AvCamMfPipeProbe_";
constexpr DWORD ServiceWaitMs = 18000;
constexpr DWORD SampleWaitMs = 11000;
constexpr DWORD MinimumFrames = 3;
SERVICE_STATUS_HANDLE statusHandle = nullptr;
void WINAPI IgnoreServiceControl(DWORD) noexcept {}

std::uint64_t NowMs() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}
bool RunningAsLocalService() {
    BYTE sidBuffer[SECURITY_MAX_SID_SIZE]{};
    DWORD sidSize = sizeof sidBuffer;
    if (!CreateWellKnownSid(WinLocalServiceSid, nullptr, sidBuffer, &sidSize))
        return false;
    BOOL matches = FALSE;
    return CheckTokenMembership(nullptr, sidBuffer, &matches) && matches;
}

void SetStatus(DWORD state, DWORD error = NO_ERROR) {
    if (!statusHandle) return;
    SERVICE_STATUS status{};
    status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    status.dwCurrentState = state;
    status.dwControlsAccepted = 0;
    status.dwWin32ExitCode = error;
    status.dwCheckPoint = state == SERVICE_RUNNING ? 1 : 0;
    SetServiceStatus(statusHandle, &status);
}

DWORD CheckPipe() {
    if (!RunningAsLocalService()) return 120;
    DWORD sessionId = static_cast<DWORD>(-1);
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &sessionId) ||
        sessionId != 0) return 121;

    avcam::mfpipe::Reader reader;
    std::uint64_t latestSession = 0, latestSequence = 0;
    DWORD count = 0;
    const auto deadline = NowMs() + SampleWaitMs;
    while (NowMs() < deadline && count < MinimumFrames) {
        avcam::mfpipe::Frame frame;
        if (!reader.Next(frame)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(75));
            continue;
        }
        const auto& h = frame.header;
        if (!avcam::mfpipe::ValidHeader(h) ||
            frame.nv12.size() != avcam::mfpipe::PayloadBytes ||
            (h.session == latestSession && h.sequence <= latestSequence))
            return 122;
        if (NowMs() < h.capturedMs || NowMs() - h.capturedMs > 2000)
            return 123;
        latestSession = h.session;
        latestSequence = h.sequence;
        ++count;
    }
    return count >= MinimumFrames ? NO_ERROR : 124;
}

void WINAPI ActualServiceMain(DWORD argc, LPWSTR* argv) {
    if (argc == 0 || !argv || !argv[0]) return;
    statusHandle = RegisterServiceCtrlHandlerW(argv[0], IgnoreServiceControl);
    if (!statusHandle) return;
    SetStatus(SERVICE_RUNNING);
    const DWORD result = CheckPipe();
    SetStatus(SERVICE_STOPPED, result);
}

bool IsAdministrator() {
    BYTE sidBuffer[SECURITY_MAX_SID_SIZE]{};
    DWORD sidSize = sizeof sidBuffer;
    if (!CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr,
                            sidBuffer, &sidSize)) return false;
    BOOL member = FALSE;
    return CheckTokenMembership(nullptr, sidBuffer, &member) && member;
}
std::wstring GetExePath() {
    std::vector<wchar_t> path(32768);
    const DWORD n = GetModuleFileNameW(nullptr, path.data(),
                                      static_cast<DWORD>(path.size()));
    return n && n < path.size() ? std::wstring(path.data(), n) : L"";
}

class ServiceScope final {
public:
    ~ServiceScope() {
        if (service_) {
            DeleteService(service_);
            CloseServiceHandle(service_);
        }
        if (manager_) CloseServiceHandle(manager_);
        if (!exeCopy_.empty() && canDeleteExecutable_)
            DeleteFileW(exeCopy_.c_str());
    }

    DWORD Run() {
        if (!IsAdministrator()) return 102;
        const std::wstring original = GetExePath();
        if (original.empty()) return 103;
        wchar_t programData[32768]{};
        const DWORD n = GetEnvironmentVariableW(
            L"ProgramFiles", programData, static_cast<DWORD>(std::size(programData)));
        if (!n || n >= std::size(programData)) return 104;

        const DWORD pid = GetCurrentProcessId();
        name_ = std::wstring(ServicePrefix) + std::to_wstring(pid);
        exeCopy_ = std::wstring(programData) + L"\\" + name_ + L".exe";
        if (!CopyFileW(original.c_str(), exeCopy_.c_str(), TRUE)) {
            exeCopy_.clear();  // Do not delete someone else's file.
            return 105;
        }
        // Protected Program Files directory, not user-writeable ProgramData.
        // Remove owned copy on any failure before service starts.
        canDeleteExecutable_ = true;
        manager_ = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
        if (!manager_) return 106;

        // Service execution is intentionally scoped only to this one
        // temporary diagnostic, never the permanent Camera Frame Server.
        const std::wstring command = L"\"" + exeCopy_ + L"\" --internal-service";
        service_ = CreateServiceW(
            manager_, name_.c_str(), name_.c_str(),
            SERVICE_START | SERVICE_QUERY_STATUS | DELETE,
            SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START,
            SERVICE_ERROR_NORMAL, command.c_str(), nullptr, nullptr,
            nullptr, L"NT AUTHORITY\\LocalService", nullptr);
        if (!service_) return 107;
        // The binary belongs to our unique temporary service and can
        // only be removed after SCM confirms it stopped.
        if (!StartServiceW(service_, 0, nullptr)) return 108;
        // Never delete a binary while a live LocalService process could
        // still be executing it.
        canDeleteExecutable_ = false;
        const auto deadline = NowMs() + ServiceWaitMs;
        while (NowMs() < deadline) {
            SERVICE_STATUS_PROCESS status{};
            DWORD size = 0;
            if (!QueryServiceStatusEx(service_, SC_STATUS_PROCESS_INFO,
                                      reinterpret_cast<BYTE*>(&status),
                                      sizeof(status), &size))
                return 109;
            if (status.dwCurrentState == SERVICE_STOPPED) {
                canDeleteExecutable_ = true;
                return status.dwWin32ExitCode;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(140));
        }
        // Avoid deleting the binary of a still-running service. SCM removal
        // is marked pending and Windows will close it upon service exit.
        canDeleteExecutable_ = false;
        return 110;
    }
private:
    SC_HANDLE manager_ = nullptr;
    SC_HANDLE service_ = nullptr;
    std::wstring name_;
    std::wstring exeCopy_;
    bool canDeleteExecutable_ = false;
};
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && std::wstring(argv[1]) == L"--status") {
        std::wcout << L"AVCAM_LOCALSERVICE_PROBE=AVAILABLE_NOT_EXECUTED\n"
                      L"AVCAM_LOCALSERVICE_REQUIRES=ELEVATED_EXPLICIT_CONSENT\n";
        return 0;
    }
    if (argc == 2 && std::wstring(argv[1]) == L"--internal-service") {
        SERVICE_TABLE_ENTRYW table[] = {
            {const_cast<LPWSTR>(ServicePrefix), &ActualServiceMain},
            {nullptr, nullptr}
        };
        return StartServiceCtrlDispatcherW(table) ? 0 : 111;
    }
    if (argc != 3 || std::wstring(argv[1]) != L"--probe" ||
        std::wstring(argv[2]) != L"--accept-temporary-service") {
        std::wcerr << L"Usage: AvCamMfLocalServiceProbe.exe --status | "
                      L"--probe --accept-temporary-service\n";
        return 2;
    }
    std::wcout << L"AVCAM_LOCALSERVICE_TEMPORARY_SERVICE_TEST_BEGIN\n";
    ServiceScope service;
    const DWORD result = service.Run();
    std::wcout << L"AVCAM_LOCALSERVICE_PIPE_RESULT=" << result << L"\n";
    if (result == NO_ERROR)
        std::wcout << L"AVCAM_LOCALSERVICE_SESSION0_OBS_PIPE_PASS\n";
    return result ? 1 : 0;
}
