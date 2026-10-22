// Real Windows cross-process smoke: explicit OBS producer in parent,
// separate named pipe broker process, separate consumer connection.
// No physical camera, Windows Camera Frame Server, COM registration,
// AVStream installation, or elevated permissions required.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include "AvCamMfPipe.h"
#include "../AvCamRouterIpc.h"

namespace {
std::uint64_t NowMs() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}
class Child final {
public:
    bool Start(const wchar_t* filename) {
        std::wstring cmd = L"\"" + std::wstring(filename) + L"\" --frames 12";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION info{};
        if (!CreateProcessW(filename, cmd.data(), nullptr, nullptr, FALSE,
                            CREATE_NO_WINDOW, nullptr, nullptr,
                            &startup, &info)) return false;
        process_ = info.hProcess;
        CloseHandle(info.hThread);
        return true;
    }
    ~Child() {
        if (!process_) return;
        if (WaitForSingleObject(process_, 0) == WAIT_TIMEOUT)
            TerminateProcess(process_, 100);
        WaitForSingleObject(process_, 5000);
        CloseHandle(process_);
    }
    bool Running() const {
        return process_ && WaitForSingleObject(process_, 0) == WAIT_TIMEOUT;
    }
private:
    HANDLE process_ = nullptr;
};
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    using namespace avcam;
    // Demonstrate that the service bridge never publishes RAW in OBS absence.
    ipc::Publisher raw;
    if (!raw.Start(ipc::Channel::Raw)) return 3;
    std::vector<unsigned char> rawPixels(ipc::FrameBytes, 0x33);
    if (!raw.Publish(rawPixels.data(), rawPixels.size(), 1, NowMs())) return 4;

    Child bridge;
    if (!bridge.Start(argv[1])) return 5;

    mfpipe::Reader reader;
    bool attached = false;
    const auto until = NowMs() + 5000;
    while (NowMs() < until && !attached) {
        attached = reader.Connect();
        if (!attached)
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    if (!attached) return 6;

    mfpipe::Frame frame;
    if (reader.Next(frame)) return 7; // STRICT OBS ONLY: no RAW leak
    reader.Close();
    std::cout << "AVCAM_MF_PIPE_RAW_ISOLATION_PASS\n";

    ipc::Publisher obs;
    if (!obs.Start(ipc::Channel::Obs)) return 8;
    std::atomic<bool> stop{false};
    std::atomic<bool> sent{true};
    std::thread producer([&] {
        std::vector<unsigned char> obsPixels(ipc::FrameBytes, 0x9b);
        for (std::uint64_t sequence = 1; sequence <= 180 && !stop; ++sequence) {
            if (!obs.Publish(obsPixels.data(), obsPixels.size(),
                             sequence, NowMs())) {
                sent = false;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
        }
    });
    int result = 0;
    unsigned received = 0;
    const auto deadline = NowMs() + 9000;
    std::uint64_t last = 0;
    while (NowMs() < deadline && received < 8) {
        mfpipe::Frame next;
        if (!reader.Next(next)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            continue;
        }
        if (next.nv12.size() != mfpipe::PayloadBytes ||
            next.nv12.front() != 149 ||  // 155 gray -> BT.601 limited-range
            next.nv12[static_cast<std::size_t>(mfpipe::Width) *
                      mfpipe::Height] != 128 ||
            next.nv12.back() != 128 ||
            (next.header.session == frame.header.session &&
             next.header.sequence <= last)) {
            result = 9;
            break;
        }
        frame = std::move(next);
        last = frame.header.sequence;
        ++received;
    }
    stop = true;
    producer.join();
    obs.Stop();
    raw.Stop();
    if (!result && (!sent || received < 8 || !bridge.Running())) result = 10;
    if (!result) {
        std::cout << "AVCAM_MF_PIPE_CROSS_PROCESS_OBS_ONLY_PASS\n"
                  << "AVCAM_MF_PIPE_NV12_FRAMES=" << received << "\n";
    }
    return result;
}
