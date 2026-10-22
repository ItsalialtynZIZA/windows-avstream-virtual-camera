// Windows-only cross-process IPC V2 regression: NO camera or OBS access.
// Two synthetic publisher processes, retained reader handles, forced writer
// termination and fresh publishing from a separate replacement process.
#include "AvCamRouterIpc.h"
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace avcam::ipc;

namespace {
struct Child {
    HANDLE handle = nullptr;
    Child() = default;
    Child(const Child&) = delete;
    Child& operator=(const Child&) = delete;
    ~Child() { Kill(); }

    bool Launch(const wchar_t* channel, unsigned marker) {
        if (handle) return false;
        std::vector<wchar_t> path(32768);
        DWORD length = GetModuleFileNameW(nullptr, path.data(),
                                           static_cast<DWORD>(path.size()));
        if (!length || length >= path.size()) return false;
        std::wstring command = L"\"" + std::wstring(path.data(), length) +
            L"\" --writer " + channel + L" " + std::to_wstring(marker);
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION info{};
        if (!CreateProcessW(path.data(), command.data(),
                            nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                            nullptr, nullptr, &startup, &info)) return false;
        handle = info.hProcess;
        CloseHandle(info.hThread);
        return true;
    }

    bool ExitCodeWithin(DWORD milliseconds, DWORD expected) const {
        if (!handle || WaitForSingleObject(handle, milliseconds) != WAIT_OBJECT_0)
            return false;
        DWORD actual = 0;
        return GetExitCodeProcess(handle, &actual) && actual == expected;
    }

    bool Running() const {
        return handle && WaitForSingleObject(handle, 0) == WAIT_TIMEOUT;
    }

    void Kill() noexcept {
        if (!handle) return;
        if (WaitForSingleObject(handle, 0) == WAIT_TIMEOUT)
            TerminateProcess(handle, 100); // only synthetic test child
        WaitForSingleObject(handle, 5000);
        CloseHandle(handle);
        handle = nullptr;
    }
};

std::uint64_t NowMs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

bool ReadExpected(Subscriber& reader, Channel channel,
                  Cursor after, unsigned marker, Snapshot& out,
                  DWORD timeoutMs = 4000) {
    const auto until = NowMs() + timeoutMs;
    bool attached = false;
    while (NowMs() < until) {
        if (!attached) attached = reader.Start(channel);
        if (attached && reader.TryRead(out, after)) {
            if (out.channel != channel || !out.session || !out.sequence ||
                out.bgra.size() != FrameBytes || out.bgra[0] != marker ||
                out.bgra[FrameBytes - 1] != marker)
                return false;
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

int Writer(Channel channel, unsigned marker) {
    Publisher producer;
    if (!producer.Start(channel)) {
        return GetLastError() == ERROR_ALREADY_EXISTS ? 11 : 12;
    }
    std::vector<unsigned char> bgra(FrameBytes,
                                     static_cast<unsigned char>(marker));
    for (std::uint64_t sequence = 1; sequence < 2000; ++sequence) {
        if (!producer.Publish(bgra.data(), bgra.size(), sequence, NowMs()))
            return 13;
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    return 0;
}

int SelfTest() {
    Child obsFirst, raw, duplicate, obsSecond;
    Subscriber obsReader, rawReader;
    Snapshot obsA, rawA, obsB, rawB;
    if (!obsFirst.Launch(L"obs", 0x31)) return 1;
    if (!ReadExpected(obsReader, Channel::Obs, {}, 0x31, obsA)) return 2;
    const auto previous = obsA.Position();

    if (!raw.Launch(L"raw", 0x41)) return 3;
    if (!ReadExpected(rawReader, Channel::Raw, {}, 0x41, rawA)) return 4;
    if (obsA.session == rawA.session) return 5; // distinct publishers

    if (!duplicate.Launch(L"obs", 0x91) ||
        !duplicate.ExitCodeWithin(4000, 11) || !obsFirst.Running())
        return 6;
    duplicate.Kill();

    // Emulate a crash: destructor/Invalidate does NOT run on the first writer.
    obsFirst.Kill();
    // The RAW publisher must still advance despite the OBS publisher dying.
    if (!ReadExpected(rawReader, Channel::Raw,
                      rawA.Position(), 0x41, rawB) ||
        rawB.session != rawA.session || rawB.sequence <= rawA.sequence ||
        !raw.Running()) return 7;

    // Keep obsReader mapped. Old mapping still exists in Windows even though
    // its exclusive publisher lease has been released by process termination.
    if (!obsSecond.Launch(L"obs", 0x71)) return 8;
    if (!ReadExpected(obsReader, Channel::Obs, previous, 0x71, obsB) ||
        obsB.session == obsA.session || !obsSecond.Running()) return 9;
    if (obsReader.TryRead(obsB, obsB.Position())) return 10;
    if (!ReadExpected(rawReader, Channel::Raw,
                      rawB.Position(), 0x41, rawA) ||
        rawA.session != rawB.session) return 14;

    std::cout << "AVCAM_IPC_PROCESS_CONCURRENT_OWNER_REJECTED=1\n"
              << "AVCAM_IPC_PROCESS_CRASH_RESTART_PASS\n"
              << "AVCAM_IPC_PROCESS_LIVE_READER_REATTACHED=1\n"
              << "AVCAM_IPC_PROCESS_RAW_SURVIVES_OBS_CRASH=1\n";
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--self-test")
        return SelfTest();
    if (argc == 4 && std::string(argv[1]) == "--writer") {
        const std::string type(argv[2]);
        Channel channel;
        if (type == "raw") channel = Channel::Raw;
        else if (type == "obs") channel = Channel::Obs;
        else return 9;
        unsigned long marker = 0;
        try {
            const std::string value(argv[3]);
            if (value.empty() || value.find_first_not_of("0123456789")
                                  != std::string::npos) return 9;
            marker = std::stoul(value);
        } catch (...) { return 9; }
        if (!marker || marker > 255) return 9;
        return Writer(channel, static_cast<unsigned>(marker));
    }
    std::cerr << "USAGE: AvCamRouterIpcProcessTest.exe --self-test\n";
    return 9;
}
