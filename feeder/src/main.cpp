#include <windows.h>
#include <setupapi.h>
#include <ks.h>
#include <ksmedia.h>

#include <atomic>
#include <cstddef>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

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
    const size_t totalBytes =
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

void FillMovingTestPattern(std::vector<std::byte>& packet, unsigned long frameIndex)
{
    auto* header = reinterpret_cast<AvCamFrameHeader*>(packet.data());
    header->Timestamp100ns = GetTickCount64() * 10000ull;

    auto* pixels = reinterpret_cast<unsigned char*>(
        packet.data() + sizeof(AvCamFrameHeader));

    const unsigned long movingBar =
        (frameIndex * 12u) % AVCAM_FRAME_WIDTH;

    for (unsigned long y = 0; y < AVCAM_FRAME_HEIGHT; ++y)
    {
        auto* row = pixels + y * AVCAM_FRAME_STRIDE_BGRA32;
        for (unsigned long x = 0; x < AVCAM_FRAME_WIDTH; ++x)
        {
            const unsigned long offset = x * 4u;
            const unsigned char horizontal =
                static_cast<unsigned char>((x * 255u) / (AVCAM_FRAME_WIDTH - 1u));
            const unsigned char vertical =
                static_cast<unsigned char>((y * 255u) / (AVCAM_FRAME_HEIGHT - 1u));

            const unsigned long distance =
                (x > movingBar) ? (x - movingBar) : (movingBar - x);
            const bool highlight = distance < 24u;

            row[offset + 0] = highlight ? 0x20 : horizontal;
            row[offset + 1] = highlight ? 0xE0 : vertical;
            row[offset + 2] = highlight ? 0xFF : 0x40;
            row[offset + 3] = 0xFF;
        }
    }
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

int wmain()
{
    SetConsoleCtrlHandler(ConsoleControlHandler, TRUE);

    std::wcout << L"AvCam feeder\n";
    std::wcout << L"Protocol version: " << AVCAM_PROTOCOL_VERSION << L"\n";
    std::wcout << L"Press Ctrl+C to stop.\n";

    auto packet = CreateFramePacket();
    unsigned long frameIndex = 0;
    bool announcedWaiting = false;

    while (g_Running.load())
    {
        const auto producerInterfaces =
            FindDevicePaths(GUID_DEVINTERFACE_AVCAM_FRAME_SINK);

        if (producerInterfaces.empty())
        {
            if (!announcedWaiting)
            {
                std::wcout << L"Waiting for AvCam driver/device...\n";
                announcedWaiting = true;
            }
            Sleep(1000);
            continue;
        }

        FillMovingTestPattern(packet, frameIndex++);
        WinHandle filter = FindAvCamFilter(packet);
        if (filter.value == INVALID_HANDLE_VALUE)
        {
            if (!announcedWaiting)
            {
                std::wcout << L"AvCam device is present; waiting for filter transport...\n";
                announcedWaiting = true;
            }
            Sleep(1000);
            continue;
        }

        announcedWaiting = false;
        std::wcout << L"Streaming 1280x720 BGRA32 test frames.\n";
        ULONGLONG nextStatsTick = GetTickCount64() + 1000ull;

        while (g_Running.load())
        {
            FillMovingTestPattern(packet, frameIndex++);

            DWORD errorCode = ERROR_SUCCESS;
            if (!SubmitFrame(filter.value, packet, &errorCode))
            {
                std::wcerr << L"Frame transport lost (Win32 error "
                           << errorCode << L"). Reconnecting...\n";
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

            Sleep(33);
        }
    }

    std::wcout << L"Feeder stopped.\n";
    return 0;
}
