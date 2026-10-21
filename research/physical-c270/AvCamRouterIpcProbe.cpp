// Read-only consumer for the optional AvCam router RAW/OBS IPC channels.
// --self-test uses synthetic frames and does not open a camera or OBS.
#include "AvCamRouterIpc.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

using namespace avcam::ipc;

int SelfTest() {
    Publisher raw;
    Publisher obs;
    if (!raw.Start(Channel::Raw) || !obs.Start(Channel::Obs)) {
        std::cerr << "AVCAM_IPC_SELFTEST_MAPPING_FAILED error=" << GetLastError() << "\n";
        return 1;
    }
    Subscriber rawReader;
    Subscriber obsReader;
    if (!rawReader.Start(Channel::Raw) || !obsReader.Start(Channel::Obs)) return 2;
    std::vector<unsigned char> original(FrameBytes, 0x11);
    std::vector<unsigned char> processed(FrameBytes, 0x99);
    if (!raw.Publish(original.data(), original.size(), 1, 100) ||
        !obs.Publish(processed.data(), processed.size(), 1, 101)) return 3;
    Snapshot a, b;
    if (!rawReader.TryRead(a) || !obsReader.TryRead(b)) return 4;
    if (a.channel != Channel::Raw || b.channel != Channel::Obs ||
        a.sequence != 1 || b.sequence != 1 ||
        a.bgra != original || b.bgra != processed) return 5;
    // A caller can change its copy without contaminating published storage.
    a.bgra[0] = 0xff;
    if (!rawReader.TryRead(a) || a.bgra[0] != 0x11 ||
        rawReader.TryRead(a, 1)) return 6;
    // OBS disconnection invalidates only the OBS channel.
    obs.Invalidate();
    if (obsReader.TryRead(b) || !rawReader.TryRead(a) || a.bgra != original)
        return 7;
    // OBS reconnect can publish another frame without affecting RAW.
    processed[0] = 0x77;
    if (!obs.Publish(processed.data(), processed.size(), 2, 200) ||
        !obsReader.TryRead(b) || b.sequence != 2 ||
        b.bgra != processed || !rawReader.TryRead(a) ||
        a.bgra != original) return 8;
    if (obs.Publish(nullptr, FrameBytes, 3, 300) ||
        obs.Publish(processed.data(), FrameBytes - 1, 3, 300))
        return 9;
    std::cout << "AVCAM_ROUTER_IPC_SELFTEST_PASS\n"
              << "AVCAM_ROUTER_IPC_INDEPENDENT_CHANNELS=1\n"
              << "AVCAM_ROUTER_IPC_OBS_DISCONNECT_INVALIDATED=1\n";
    return 0;
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--self-test") return SelfTest();
    Channel channel = Channel::Raw;
    bool channelSpecified = false;
    unsigned wanted = 30, timeoutMs = 12000;
    for (int i = 1; i < argc; ++i) {
        const std::string flag(argv[i]);
        if (flag == "--channel" && i + 1 < argc && !channelSpecified) {
            channelSpecified = true;
            const std::string value(argv[++i]);
            if (value == "raw") channel = Channel::Raw;
            else if (value == "obs") channel = Channel::Obs;
            else return 9;
        } else if ((flag == "--frames" || flag == "--timeout-ms") && i + 1 < argc) {
            const std::string value(argv[++i]);
            if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
                return 9;
            try {
                const unsigned long parsed = std::stoul(value);
                if (parsed == 0 || parsed > (flag == "--frames" ? 10000UL : 600000UL))
                    return 9;
                if (flag == "--frames") wanted = static_cast<unsigned>(parsed);
                else timeoutMs = static_cast<unsigned>(parsed);
            } catch (...) { return 9; }
        } else {
            std::cerr << "USAGE: AvCamRouterIpcProbe [--self-test | --channel raw|obs "
                         "--frames N --timeout-ms N]\n";
            return 9;
        }
    }
    Subscriber consumer;
    const auto started = std::chrono::steady_clock::now();
    std::uint64_t lastSequence = 0;
    unsigned seen = 0;
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
           std::chrono::steady_clock::now() - started).count() < timeoutMs) {
        if (!consumer.Start(channel)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        std::cout << "AVCAM_IPC_CONSUMER_CONNECTED channel="
                  << (channel == Channel::Raw ? "RAW" : "OBS") << "\n";
        Snapshot frame;
        while (std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - started).count() < timeoutMs) {
            if (consumer.TryRead(frame, lastSequence)) {
                lastSequence = frame.sequence;
                ++seen;
                if (seen == 1 || seen % 30 == 0)
                    std::cout << "AVCAM_IPC_FRAME channel="
                              << (channel == Channel::Raw ? "RAW" : "OBS")
                              << " seq=" << lastSequence
                              << " captured_ms=" << frame.capturedMs
                              << " bytes=" << frame.bgra.size() << "\n";
                if (seen >= wanted) {
                    std::cout << "AVCAM_ROUTER_IPC_CONSUMER_PASS frames=" << seen << "\n";
                    return 0;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    std::cerr << "AVCAM_ROUTER_IPC_CONSUMER_TIMEOUT frames=" << seen
              << " requested=" << wanted << "\n";
    return 6;
}
