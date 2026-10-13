#include <windows.h>
#include <setupapi.h>
#include <iostream>
#include <memory>
#include <string>

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

    std::wstring FindDevicePath()
    {
        DevInfoList list;
        list.value = SetupDiGetClassDevsW(
            &GUID_DEVINTERFACE_AVCAM_FRAME_SINK,
            nullptr,
            nullptr,
            DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

        if (list.value == INVALID_HANDLE_VALUE)
        {
            return {};
        }

        SP_DEVICE_INTERFACE_DATA interfaceData{};
        interfaceData.cbSize = sizeof(interfaceData);

        if (!SetupDiEnumDeviceInterfaces(
                list.value,
                nullptr,
                &GUID_DEVINTERFACE_AVCAM_FRAME_SINK,
                0,
                &interfaceData))
        {
            return {};
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
            return {};
        }

        auto buffer = std::make_unique<std::byte[]>(requiredBytes);
        auto detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.get());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        if (!SetupDiGetDeviceInterfaceDetailW(
                list.value,
                &interfaceData,
                detail,
                requiredBytes,
                nullptr,
                nullptr))
        {
            return {};
        }

        return detail->DevicePath;
    }
}

int wmain()
{
    std::wcout << L"AvCam feeder\n";
    std::wcout << L"Protocol version: " << AVCAM_PROTOCOL_VERSION << L"\n";

    const std::wstring devicePath = FindDevicePath();
    if (devicePath.empty())
    {
        std::wcerr << L"AvCam frame-sink interface is not present.\n";
        std::wcerr << L"Install/start the driver before running the feeder.\n";
        return 2;
    }

    std::wcout << L"Interface: " << devicePath << L"\n";

    HANDLE device = CreateFileW(
        devicePath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (device == INVALID_HANDLE_VALUE)
    {
        std::wcerr << L"CreateFile failed. Win32 error: " << GetLastError() << L"\n";
        return 3;
    }

    std::wcout << L"Driver interface opened successfully.\n";
    CloseHandle(device);
    return 0;
}
