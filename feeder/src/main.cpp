#include <windows.h>
#include <setupapi.h>
#include <ks.h>
#include <ksmedia.h>

#include <cstddef>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "VirtualCameraProtocol.h"

namespace
{
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

std::vector<std::byte> BuildTestFramePacket()
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

    auto* pixels = reinterpret_cast<unsigned char*>(
        packet.data() + sizeof(AvCamFrameHeader));

    for (unsigned long y = 0; y < AVCAM_FRAME_HEIGHT; ++y)
    {
        auto* row = pixels + y * AVCAM_FRAME_STRIDE_BGRA32;
        for (unsigned long x = 0; x < AVCAM_FRAME_WIDTH; ++x)
        {
            const unsigned long offset = x * 4;
            const unsigned char horizontal =
                static_cast<unsigned char>((x * 255u) / (AVCAM_FRAME_WIDTH - 1u));
            const unsigned char vertical =
                static_cast<unsigned char>((y * 255u) / (AVCAM_FRAME_HEIGHT - 1u));
            const bool checker = (((x / 64u) + (y / 64u)) & 1u) != 0;

            row[offset + 0] = checker ? 0x20 : horizontal;
            row[offset + 1] = vertical;
            row[offset + 2] = checker ? horizontal : 0xE0;
            row[offset + 3] = 0xFF;
        }
    }

    return packet;
}

bool SubmitTestFrameToFilter(const std::wstring& filterPath)
{
    HANDLE filter = CreateFileW(
        filterPath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (filter == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    KSMETHOD method{};
    method.Set = KSMETHODSETID_AVCAM_FRAME_TRANSPORT;
    method.Id = AVCAM_METHOD_SUBMIT_FRAME;
    method.Flags = KSMETHOD_TYPE_SEND;

    auto packet = BuildTestFramePacket();
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

    if (!ok)
    {
        CloseHandle(filter);
        return false;
    }

    std::wcout << L"Accepted by AVStream filter: " << filterPath << L"\n";
    CloseHandle(filter);
    return true;
}
} // namespace

int wmain()
{
    std::wcout << L"AvCam feeder\n";
    std::wcout << L"Protocol version: " << AVCAM_PROTOCOL_VERSION << L"\n";

    const auto producerInterfaces =
        FindDevicePaths(GUID_DEVINTERFACE_AVCAM_FRAME_SINK);

    if (producerInterfaces.empty())
    {
        std::wcerr << L"AvCam producer interface is not present.\n";
        std::wcerr << L"Install/start the AvCam driver before running the feeder.\n";
        return 2;
    }

    std::wcout << L"AvCam producer interface detected.\n";

    const auto cameraFilters = FindDevicePaths(KSCATEGORY_VIDEO_CAMERA);
    if (cameraFilters.empty())
    {
        std::wcerr << L"No AVStream camera filters are present.\n";
        return 3;
    }

    for (const auto& filterPath : cameraFilters)
    {
        if (SubmitTestFrameToFilter(filterPath))
        {
            std::wcout << L"Submitted one 1280x720 BGRA32 test frame.\n";
            return 0;
        }
    }

    std::wcerr << L"AvCam filter did not accept the private frame method.\n";
    return 4;
}
