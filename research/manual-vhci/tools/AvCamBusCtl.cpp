#include <Windows.h>
#include <SetupAPI.h>
#include <devguid.h>
#include <iostream>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <vector>

#include "../shared/AvCamManualIoctl.h"

#pragma comment(lib, "setupapi.lib")

static const GUID GUID_DEVINTERFACE_AVCAM_MANUAL_BUS =
{ 0x4ef708b4, 0x08c6, 0x4a21, { 0xa7, 0x48, 0xa1, 0x69, 0x64, 0x65, 0x88, 0x31 } };

static std::wstring GetBusInterfacePath()
{
    HDEVINFO info = SetupDiGetClassDevsW(
        &GUID_DEVINTERFACE_AVCAM_MANUAL_BUS,
        nullptr,
        nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

    if (info == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("SetupDiGetClassDevsW failed");
    }

    SP_DEVICE_INTERFACE_DATA iface{};
    iface.cbSize = sizeof(iface);

    if (!SetupDiEnumDeviceInterfaces(info, nullptr, &GUID_DEVINTERFACE_AVCAM_MANUAL_BUS, 0, &iface)) {
        SetupDiDestroyDeviceInfoList(info);
        throw std::runtime_error("AvCam bus interface was not found");
    }

    DWORD required = 0;
    SetupDiGetDeviceInterfaceDetailW(info, &iface, nullptr, 0, &required, nullptr);
    if (required < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) {
        SetupDiDestroyDeviceInfoList(info);
        throw std::runtime_error("Invalid interface detail size");
    }

    std::vector<BYTE> buffer(required);
    auto detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(buffer.data());
    detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

    if (!SetupDiGetDeviceInterfaceDetailW(info, &iface, detail, required, nullptr, nullptr)) {
        SetupDiDestroyDeviceInfoList(info);
        throw std::runtime_error("SetupDiGetDeviceInterfaceDetailW failed");
    }

    std::wstring path(detail->DevicePath);
    SetupDiDestroyDeviceInfoList(info);
    return path;
}

static DWORD SendSimpleIoctl(DWORD code)
{
    const std::wstring path = GetBusInterfacePath();

    HANDLE handle = CreateFileW(
        path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (handle == INVALID_HANDLE_VALUE) {
        return GetLastError();
    }

    DWORD bytes = 0;
    const BOOL ok = DeviceIoControl(
        handle,
        code,
        nullptr,
        0,
        nullptr,
        0,
        &bytes,
        nullptr);

    const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(handle);
    return error;
}

static DWORD PrintDiagnostics()
{
    const std::wstring path = GetBusInterfacePath();
    HANDLE handle = CreateFileW(
        path.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return GetLastError();
    }

    AVCAM_DIAG_BUFFER output{};
    DWORD bytes = 0;
    const BOOL ok = DeviceIoControl(
        handle, IOCTL_AVCAM_READ_DIAGNOSTICS,
        nullptr, 0, &output, sizeof(output), &bytes, nullptr);
    const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(handle);
    if (error != ERROR_SUCCESS) {
        return error;
    }
    if (bytes < sizeof(output) || output.Count > AVCAM_DIAG_TRACE_CAPACITY) {
        return ERROR_INVALID_DATA;
    }

    std::wcout << L"Recorded=" << output.Count
               << L" TotalSinceDriverLoad=" << output.TotalEvents << L"\n";
    std::wcout << L"Type: 1=unsupported internal USB IOCTL,"
                  L" 2=URB handler return, 3=PnP QUERY_INTERFACE observed.\n"
                  L"Type 3 status=0x00000103 means entry was observed;"
                  L" it is not the framework's completion result.\n";
    for (unsigned long i = 0; i < output.Count; ++i) {
        const auto& event = output.Events[i];
        std::wcout << L"#" << std::dec << event.Sequence
                   << L" Type=" << event.EventType
                   << L" Code=0x" << std::hex << std::setw(8) << std::setfill(L'0')
                   << event.Code
                   << L" Status=0x" << std::setw(8)
                   << static_cast<unsigned long>(event.Status)
                   << std::dec << std::setfill(L' ') << L"\n";
    }
    return ERROR_SUCCESS;
}

int wmain(int argc, wchar_t** argv)
{
    if (argc != 2) {
        std::wcerr << L"Usage: AvCamBusCtl.exe plug|unplug|path|trace\n";
        return 2;
    }

    try {
        const std::wstring command(argv[1]);

        if (command == L"path") {
            std::wcout << GetBusInterfacePath() << L"\n";
            return 0;
        }

        if (command == L"trace") {
            const DWORD error = PrintDiagnostics();
            if (error != ERROR_SUCCESS) {
                std::wcerr << L"Diagnostic read failed with Win32 error "
                           << error << L"\n";
                return static_cast<int>(error);
            }
            return 0;
        }

        DWORD error = ERROR_INVALID_PARAMETER;
        if (command == L"plug") {
            error = SendSimpleIoctl(IOCTL_AVCAM_PLUG_DEVICE);
        } else if (command == L"unplug") {
            error = SendSimpleIoctl(IOCTL_AVCAM_UNPLUG_DEVICE);
        } else {
            std::wcerr << L"Unknown command: " << command << L"\n";
            return 2;
        }

        if (error != ERROR_SUCCESS) {
            std::wcerr << L"DeviceIoControl failed with Win32 error " << error << L"\n";
            return static_cast<int>(error == 0 ? 1 : error);
        }

        std::wcout << command << L" succeeded\n";
        return 0;
    }
    catch (const std::exception& ex) {
        std::cerr << ex.what() << "\n";
        return 1;
    }
}
