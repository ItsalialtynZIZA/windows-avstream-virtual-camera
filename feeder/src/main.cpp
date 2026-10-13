#include <windows.h>
#include <setupapi.h>
#include <ks.h>
#include <ksmedia.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "FrameSource.h"
#include "MediaFoundationFrameSource.h"
#include "TestPatternFrameSource.h"
#include "VirtualCameraProtocol.h"

namespace
{
std::atomic_bool g_Running{true};

BOOL WINAPI ConsoleControlHandler(DWORD controlType)
{
    if (controlType == CTRL_C_EVENT ||
        controlType == CTRL_BREAK_EVENT ||
        controlType == CTRL_CLOSE_EVENT)
    {
        g_Running.store(false);
        return TRUE;
    }

    return FALSE;
}

struct ComApartment
{
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool shouldUninitialize = SUCCEEDED(result);

    ~ComApartment()
    {
        if (shouldUninitialize)
        {
            CoUninitialize();
        }
    }

    bool IsUsable() const noexcept
    {
        return SUCCEEDED(result) || result == RPC_E_CHANGED_MODE;
    }
};

struct DevInfoList
{
    HDEVINFO value = INVALID_HANDLE_VALUE;

    ~DevInfoList()
    {
        if (value != INVALID_HANDLE_VALUE)
        {
            SetupDiDestroyDeviceInfoList(value);
        }
    }
};

struct WinHandle
{
    HANDLE value = INVALID_HANDLE_VALUE;

    ~WinHandle()
    {
        Reset();
    }

    WinHandle() = default;
    WinHandle(const WinHandle&) = delete;
    WinHandle& operator=(const WinHandle&) = delete;

    WinHandle(WinHandle&& other) noexcept : value(other.value)
    {
        other.value = INVALID_HANDLE_VALUE;
    }

    WinHandle& operator=(WinHandle&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            value = other.value;
            other.value = INVALID_HANDLE_VALUE;
        }
        return *this;
    }

    void Reset()
    {
        if (value != INVALID_HANDLE_VALUE)
        {
            CloseHandle(value);
            value = INVALID_HANDLE_VALUE;
        }
    }
};

struct Options
{
    std::wstring source = L"test";
    std::wstring deviceName = L"OBS Virtual Camera";
    bool listSources = false;
    bool showHelp = false;
};

void PrintUsage()
{
    std::wcout
        << L"Usage:\n"
        << L"  AvCamFeeder.exe --source test\n"
        << L"  AvCamFeeder.exe --source obs [--device-name \"OBS Virtual Camera\"]\n"
        << L"  AvCamFeeder.exe --list-sources\n";
}

bool ParseOptions(int argc, wchar_t* argv[], Options& options)
{
    for (int index = 1; index < argc; ++index)
    {
        const std::wstring argument = argv[index];

        if (argument == L"--help" || argument == L"-h")
        {
            options.showHelp = true;
            continue;
        }

        if (argument == L"--list-sources")
        {
            options.listSources = true;
            continue;
        }

        if (argument == L"--source" && index + 1 < argc)
        {
            options.source = argv[++index];
            continue;
        }

        if (argument == L"--device-name" && index + 1 < argc)
        {
            options.deviceName = argv[++index];
            continue;
        }

        std::wcerr << L"Unknown or incomplete argument: " << argument << L"\n";
        PrintUsage();
        return false;
    }

    if (options.source != L"test" && options.source != L"obs")
    {
        std::wcerr << L"Unsupported source: " << options.source << L"\n";
        PrintUsage();
        return false;
    }

    return true;
}

std::unique_ptr<IFrameSource> CreateFrameSource(const Options& options)
{
    if (options.source == L"obs")
    {
        return std::make_unique<MediaFoundationFrameSource>(options.deviceName);
    }

    return std::make_unique<TestPatternFrameSource>();
}

std::vector<std::wstring> FindDevicePaths(const GUID& interfaceGuid)
{
    std::vector<std::wstring> paths;

    DevInfoList list;
    list.value = SetupDiGetClassDevsW(
        &interfaceGuid,
        nullptr,
        nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

    if (list.value == INVALID_HANDLE_VALUE)
    {
        return paths;
    }

    for (DWORD index = 0;; ++index)
    {
        SP_DEVICE_INTERFACE_DATA interfaceData{};
        interfaceData.cbSize = sizeof(interfaceData);

        if (!SetupDiEnumDeviceInterfaces(
                list.value,
                nullptr,
                &interfaceGuid,
                index,
                &interfaceData))
        {
            if (GetLastError() == ERROR_NO_MORE_ITEMS)
            {
                break;
            }
            continue;
        }

        DWORD requiredBytes = 0;
        SetupDiGetDeviceInterfaceDetailW(
            list.value,
            &interfaceData,
            nullptr,
            0,
            &requiredBytes,
            nullptr);

        if (requiredBytes < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W))
        {
            continue;
        }

        auto buffer = std::make_unique<std::byte[]>(requiredBytes);
        auto* detail =
            reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.get());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        if (SetupDiGetDeviceInterfaceDetailW(
                list.value,
                &interfaceData,
                detail,
                requiredBytes,
                nullptr,
                nullptr))
        {
            paths.emplace_back(detail->DevicePath);
        }
    }

    return paths;
}

std::vector<std::byte> CreateFramePacket()
{
    const std::size_t totalBytes =
        sizeof(AvCamFrameHeader) + AVCAM_FRAME_BYTES_BGRA32;

    std::vector<std::byte> packet(totalBytes);

    auto* header = reinterpret_cast<AvCamFrameHeader*>(packet.data());
    header->Version = AVCAM_PROTOCOL_VERSION;
    header->Width = AVCAM_FRAME_WIDTH;
    header->Height = AVCAM_FRAME_HEIGHT;
    header->Stride = AVCAM_FRAME_STRIDE_BGRA32;
    header->PixelFormat = AVCAM_PIXEL_BGRA32;
    header->Timestamp100ns = 0;
    header->PayloadBytes = AVCAM_FRAME_BYTES_BGRA32;

    return packet;
}

bool FillPacketFromSource(IFrameSource& source, std::vector<std::byte>& packet)
{
    if (packet.size() < sizeof(AvCamFrameHeader) + AVCAM_FRAME_BYTES_BGRA32)
    {
        return false;
    }

    auto* header = reinterpret_cast<AvCamFrameHeader*>(packet.data());
    auto* pixels = reinterpret_cast<unsigned char*>(
        packet.data() + sizeof(AvCamFrameHeader));

    std::uint64_t timestamp100ns = 0;
    if (!source.ReadFrame(pixels, AVCAM_FRAME_BYTES_BGRA32, timestamp100ns))
    {
        return false;
    }

    header->Timestamp100ns = timestamp100ns;
    return true;
}

bool SubmitFrame(HANDLE filter, std::vector<std::byte>& packet, DWORD* errorCode)
{
    KSMETHOD method{};
    method.Set = KSMETHODSETID_AVCAM_FRAME_TRANSPORT;
    method.Id = AVCAM_METHOD_SUBMIT_FRAME;
    method.Flags = KSMETHOD_TYPE_SEND;

    DWORD bytesReturned = 0;
    const BOOL ok = DeviceIoControl(
        filter,
        IOCTL_KS_METHOD,
        &method,
        sizeof(method),
        packet.data(),
        static_cast<DWORD>(packet.size()),
        &bytesReturned,
        nullptr);

    if (!ok && errorCode != nullptr)
    {
        *errorCode = GetLastError();
    }

    return ok != FALSE;
}

bool QueryStats(HANDLE filter, AvCamTransportStats& stats)
{
    KSMETHOD method{};
    method.Set = KSMETHODSETID_AVCAM_FRAME_TRANSPORT;
    method.Id = AVCAM_METHOD_GET_STATS;
    method.Flags = KSMETHOD_TYPE_SEND;

    DWORD bytesReturned = 0;
    AvCamTransportStats result{};

    const BOOL ok = DeviceIoControl(
        filter,
        IOCTL_KS_METHOD,
        &method,
        sizeof(method),
        &result,
        sizeof(result),
        &bytesReturned,
        nullptr);

    if (!ok || bytesReturned < sizeof(result) ||
        result.Version != AVCAM_PROTOCOL_VERSION)
    {
        return false;
    }

    stats = result;
    return true;
}

void PrintStats(const AvCamTransportStats& stats)
{
    std::wcout
        << L"stats: submitted=" << stats.SubmittedFrames
        << L" replaced=" << stats.ReplacedFrames
        << L" capture_reads=" << stats.CaptureReads
        << L" fallback=" << stats.FallbackFrames
        << L"\n";
}

WinHandle FindAvCamFilter(std::vector<std::byte>& probePacket)
{
    const auto cameraFilters = FindDevicePaths(KSCATEGORY_VIDEO_CAMERA);

    for (const auto& filterPath : cameraFilters)
    {
        WinHandle candidate;
        candidate.value = CreateFileW(
            filterPath.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);

        if (candidate.value == INVALID_HANDLE_VALUE)
        {
            continue;
        }

        DWORD errorCode = ERROR_SUCCESS;
        if (SubmitFrame(candidate.value, probePacket, &errorCode))
        {
            std::wcout << L"Accepted by AVStream filter: " << filterPath << L"\n";
            return candidate;
        }
    }

    return {};
}
} // namespace

int wmain(int argc, wchar_t* argv[])
{
    SetConsoleCtrlHandler(ConsoleControlHandler, TRUE);

    Options options;
    if (!ParseOptions(argc, argv, options))
    {
        return 2;
    }

    if (options.showHelp)
    {
        PrintUsage();
        return 0;
    }

    ComApartment com;
    if (!com.IsUsable())
    {
        std::wcerr << L"COM initialization failed: 0x"
                   << std::hex << static_cast<unsigned long>(com.result) << L"\n";
        return 3;
    }

    if (options.listSources)
    {
        const auto sources = EnumerateMediaFoundationVideoSources();
        std::wcout << L"Media Foundation video sources (" << sources.size() << L"):\n";
        for (std::size_t index = 0; index < sources.size(); ++index)
        {
            std::wcout << L"  [" << index << L"] " << sources[index] << L"\n";
        }
        return 0;
    }

    auto source = CreateFrameSource(options);
    auto packet = CreateFramePacket();

    std::wcout << L"AvCam feeder\n";
    std::wcout << L"Protocol version: " << AVCAM_PROTOCOL_VERSION << L"\n";
    std::wcout << L"Requested source: " << options.source << L"\n";
    std::wcout << L"Press Ctrl+C to stop.\n";

    bool sourceStarted = false;
    bool announcedSourceWaiting = false;
    bool announcedTransportWaiting = false;

    while (g_Running.load())
    {
        if (!sourceStarted)
        {
            if (!source->Start())
            {
                if (!announcedSourceWaiting)
                {
                    std::wcout << L"Waiting for frame source: " << source->Name() << L"\n";
                    announcedSourceWaiting = true;
                }
                Sleep(1000);
                continue;
            }

            sourceStarted = true;
            announcedSourceWaiting = false;
            std::wcout << L"Frame source started: " << source->Name() << L"\n";
        }

        const auto producerInterfaces =
            FindDevicePaths(GUID_DEVINTERFACE_AVCAM_FRAME_SINK);

        if (producerInterfaces.empty())
        {
            if (!announcedTransportWaiting)
            {
                std::wcout << L"Waiting for AvCam driver/device...\n";
                announcedTransportWaiting = true;
            }
            Sleep(1000);
            continue;
        }

        if (!FillPacketFromSource(*source, packet))
        {
            std::wcerr << L"Frame source lost. Reconnecting source...\n";
            source->Stop();
            sourceStarted = false;
            Sleep(500);
            continue;
        }

        WinHandle filter = FindAvCamFilter(packet);
        if (filter.value == INVALID_HANDLE_VALUE)
        {
            if (!announcedTransportWaiting)
            {
                std::wcout << L"AvCam device is present; waiting for filter transport...\n";
                announcedTransportWaiting = true;
            }
            Sleep(1000);
            continue;
        }

        announcedTransportWaiting = false;
        std::wcout << L"Streaming 1280x720 BGRA32 frames.\n";
        ULONGLONG nextStatsTick = GetTickCount64() + 1000ull;

        while (g_Running.load())
        {
            if (!FillPacketFromSource(*source, packet))
            {
                std::wcerr << L"Frame source lost. Reconnecting source...\n";
                source->Stop();
                sourceStarted = false;
                break;
            }

            DWORD errorCode = ERROR_SUCCESS;
            if (!SubmitFrame(filter.value, packet, &errorCode))
            {
                std::wcerr << L"Frame transport lost (Win32 error "
                           << errorCode << L"). Reconnecting transport...\n";
                filter.Reset();
                Sleep(500);
                break;
            }

            const ULONGLONG now = GetTickCount64();
            if (now >= nextStatsTick)
            {
                AvCamTransportStats stats{};
                if (QueryStats(filter.value, stats))
                {
                    PrintStats(stats);
                }
                nextStatsTick = now + 1000ull;
            }

            Sleep(1);
        }
    }

    source->Stop();
    std::wcout << L"Feeder stopped.\n";
    return 0;
}
