#include <Windows.h>
#include <SetupAPI.h>
#include <devguid.h>
#include <iostream>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <vector>
#include <fstream>

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

static const wchar_t* QueryInterfaceName(unsigned long code)
{
    switch (code) {
    case 0xB1A96A13u: return L"USB_BUS_INTERFACE_USBDI_GUID";
    case 0xDEEE98EAu: return L"GUID_USBD_CLIENT_INTERFACE";
    case 0x496B8280u: return L"GUID_BUS_INTERFACE_STANDARD";
    case 0xB8E992ECu: return L"GUID_PNP_EXTENDED_ADDRESS_INTERFACE";
    default: return L"Unknown interface";
    }
}

static const wchar_t* BusCallbackName(unsigned long code)
{
    switch (code) {
    case 1: return L"GetUSBDIVersion";
    case 2: return L"QueryBusTime";
    case 3: return L"SubmitIsoOutUrb";
    case 4: return L"QueryBusInformation";
    case 5: return L"IsDeviceHighSpeed";
    default: return L"Unknown callback";
    }
}

static const wchar_t* UvcRequestName(unsigned long request)
{
    switch (request) {
    case 0x01: return L"SET_CUR";
    case 0x81: return L"GET_CUR";
    case 0x82: return L"GET_MIN";
    case 0x83: return L"GET_MAX";
    case 0x84: return L"GET_RES";
    case 0x85: return L"GET_LEN";
    case 0x86: return L"GET_INFO";
    case 0x87: return L"GET_DEF";
    default: return L"Unknown UVC request";
    }
}

static void PrintUvcInfo(unsigned long code)
{
    const unsigned long request = code >> 24;
    const unsigned long selector = (code >> 16) & 0xFF;
    const unsigned long index = code & 0xFFFF;
    std::wcout << L" Request=" << UvcRequestName(request)
               << L" Selector=" << selector
               << L" Interface=" << (index & 0xFF)
               << L" Entity=" << (index >> 8);
}


static DWORD SendJpegFrame(const wchar_t* filename)
{
    std::ifstream input(filename, std::ios::binary | std::ios::ate);
    if (!input) {
        std::wcerr << L"Cannot open JPEG file.\n";
        return ERROR_FILE_NOT_FOUND;
    }
    const std::streamoff size = input.tellg();
    if (size < 4 || size > 2068480) {
        std::wcerr << L"JPEG file must be between 4 and 2068480 bytes.\n";
        return ERROR_INVALID_DATA;
    }
    input.seekg(0, std::ios::beg);
    std::vector<unsigned char> bytes(static_cast<size_t>(size));
    if (!input.read(reinterpret_cast<char*>(bytes.data()), size)) {
        std::wcerr << L"Failed to read complete JPEG file.\n";
        return ERROR_READ_FAULT;
    }
    AVCAM_FRAME_HEADER frame{};
    frame.Width = 1920;
    frame.Height = 1080;
    frame.FourCC = 0x47504A4Du; // 'MJPG' little endian
    frame.PayloadBytes = static_cast<unsigned long>(bytes.size());

    const std::wstring path = GetBusInterfacePath();
    HANDLE handle = CreateFileW(
        path.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return GetLastError();
    }
    DWORD returned = 0;
    const BOOL ok = DeviceIoControl(
        handle, IOCTL_AVCAM_WRITE_FRAME,
        &frame, sizeof(frame), bytes.data(),
        static_cast<DWORD>(bytes.size()), &returned, nullptr);
    const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(handle);
    if (error == ERROR_SUCCESS) {
        std::wcout << L"MJPEG frame accepted: " << bytes.size()
                   << L" bytes (1920x1080)\n";
    }
    return error;
}


static DWORD PrintStreamStats()
{
    const std::wstring path = GetBusInterfacePath();
    HANDLE handle = CreateFileW(
        path.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return GetLastError();

    AVCAM_STREAM_STATS stats{};
    DWORD returned = 0;
    const BOOL ok = DeviceIoControl(
        handle, IOCTL_AVCAM_READ_STREAM_STATS,
        nullptr, 0, &stats, sizeof(stats), &returned, nullptr);
    const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(handle);
    if (error != ERROR_SUCCESS) return error;
    if (returned != sizeof(stats) || stats.Version != AVCAM_STREAM_STATS_VERSION)
        return ERROR_INVALID_DATA;

    std::wcout << L"StreamStatsVersion=" << stats.Version << L"\n"
               << L"FrameWrites=" << stats.FrameWrites
               << L" LastAcceptedFrameBytes=" << stats.LastAcceptedFrameBytes << L"\n"
               << L"FrameSwaps=" << stats.FrameSwaps
               << L" ActiveFrameBytes=" << stats.ActiveFrameBytes
               << L" PendingReady=" << stats.PendingReady << L"\n"
               << L"IsoUrbsCompleted=" << stats.IsoUrbsCompleted
               << L" IsoUrbsWithData=" << stats.IsoUrbsWithData
               << L" IsoUrbsEmpty=" << stats.IsoUrbsEmpty << L"\n"
               << L"IsoPacketsWithData=" << stats.IsoPacketsWithData
               << L" MjpegFramesCompleted=" << stats.MjpegFramesCompleted
               << L" TotalIsoPayloadBytes=" << stats.TotalIsoPayloadBytes << L"\n"
               << L"LastIsoUrbBytes=" << stats.LastIsoUrbBytes
               << L" LastIsoUrbPackets=" << stats.LastIsoUrbPackets << L"\n";
    return ERROR_SUCCESS;
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
                  L" 2=URB handler return, 3=PnP QUERY_INTERFACE observed,"
                  L" 4=PnP QUERY_INTERFACE requested Version/Size," 
                  L" 5=USB_BUS_INTERFACE_USBDI_V1 callback," 
                  L" 6=UVC control status, 7=UVC request length/direction,\n"
                  L" 8=ISO packet count/capacity, 9=ISO flags/start frame,\n"
                  L" 10=ISO state/available packet descriptors, 11=ISO offsets,\n"
                  L" 12=ISO completion packets/bytes, 13=accepted JPEG size.\n"
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
                   << std::dec << std::setfill(L' ');
        if (event.EventType == AVCAM_DIAG_EVENT_QUERY_INTERFACE) {
            std::wcout << L" GUID=" << QueryInterfaceName(event.Code);
        } else if (event.EventType == AVCAM_DIAG_EVENT_QUERY_INTERFACE_DETAILS) {
            const unsigned long packed = static_cast<unsigned long>(event.Status);
            std::wcout << L" GUID=" << QueryInterfaceName(event.Code)
                       << L" Version=" << (packed & 0xFFFFu)
                       << L" Size=" << (packed >> 16);
        }
        if (event.EventType == AVCAM_DIAG_EVENT_USB_BUS_CALLBACK) {
            std::wcout << L" Callback=" << BusCallbackName(event.Code);
        }
        if (event.EventType == AVCAM_DIAG_EVENT_ISOCH_REQUEST) {
            std::wcout << L" Packets=" << event.Code
                       << L" BufferBytes=" << static_cast<unsigned long>(event.Status);
        } else if (event.EventType == AVCAM_DIAG_EVENT_ISOCH_FLAGS) {
            std::wcout << L" Flags=0x" << std::hex << event.Code
                       << std::dec << L" StartFrame=" << static_cast<unsigned long>(event.Status);
        } else if (event.EventType == AVCAM_DIAG_EVENT_ISOCH_STATE) {
            std::wcout << L" Configured=" << ((event.Code & 0x01u) != 0)
                       << L" Alt1=" << ((event.Code & 0x02u) != 0)
                       << L" Committed=" << ((event.Code & 0x04u) != 0)
                       << L" PipeMatches=" << ((event.Code & 0x08u) != 0)
                       << L" DirectionIn=" << ((event.Code & 0x10u) != 0)
                       << L" HasMDL=" << ((event.Code & 0x20u) != 0)
                       << L" HasBuffer=" << ((event.Code & 0x40u) != 0)
                       << L" PacketCountFits=" << ((event.Code & 0x80u) != 0)
                       << L" AvailableDescriptors=" << static_cast<unsigned long>(event.Status);
        } else if (event.EventType == AVCAM_DIAG_EVENT_ISOCH_OFFSETS) {
            std::wcout << L" FirstOffset=" << event.Code
                       << L" LastOffset=" << static_cast<unsigned long>(event.Status);
        } else if (event.EventType == AVCAM_DIAG_EVENT_ISOCH_COMPLETION) {
            std::wcout << L" FilledPackets=" << event.Code
                       << L" ActualBytes=" << static_cast<unsigned long>(event.Status);
        } else if (event.EventType == AVCAM_DIAG_EVENT_FRAME_WRITE) {
            std::wcout << L" AcceptedFrameBytes=" << event.Code;
        }
        if (event.EventType == AVCAM_DIAG_EVENT_UVC_CONTROL ||
            event.EventType == AVCAM_DIAG_EVENT_UVC_REQUEST_LENGTH) {
            PrintUvcInfo(event.Code);
            if (event.EventType == AVCAM_DIAG_EVENT_UVC_REQUEST_LENGTH) {
                const unsigned long value = static_cast<unsigned long>(event.Status);
                std::wcout << L" Length=" << (value & 0x00FFFFFF)
                           << L" Direction=" << ((value & 0x40000000) ? L"IN" : L"OUT");
            }
        }
        std::wcout << L"\n";
    }
    return ERROR_SUCCESS;
}

int wmain(int argc, wchar_t** argv)
{
    if (argc != 2 && !(argc == 3 && wcscmp(argv[1], L"frame") == 0)) {
        std::wcerr << L"Usage: AvCamBusCtl.exe plug|unplug|path|trace|stats|frame <jpeg-file>\n";
        return 2;
    }

    try {
        const std::wstring command(argv[1]);

        if (command == L"path") {
            std::wcout << GetBusInterfacePath() << L"\n";
            return 0;
        }

        if (command == L"frame") {
            const DWORD error = SendJpegFrame(argv[2]);
            if (error != ERROR_SUCCESS) {
                std::wcerr << L"Frame upload failed with Win32 error " << error << L"\n";
            }
            return static_cast<int>(error);
        }

        if (command == L"stats") {
            const DWORD error = PrintStreamStats();
            if (error != ERROR_SUCCESS) {
                std::wcerr << L"Stream stats read failed with Win32 error "
                           << error << L"\n";
            }
            return static_cast<int>(error);
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
