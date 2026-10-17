#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <SetupAPI.h>
#include <newdev.h>

#include <cwchar>
#include <iostream>
#include <string>

namespace {
constexpr wchar_t kRootHardwareId[] = L"Root\\AvCamVirtualUsbHost";
constexpr wchar_t kDeviceName[] = L"AvCamVirtualUsbHost";

std::wstring GetFullInfPath(const wchar_t* relativePath)
{
    wchar_t path[32768]{};
    const DWORD length = GetFullPathNameW(
        relativePath, static_cast<DWORD>(_countof(path)), path, nullptr);
    if (length == 0 || length >= _countof(path)) {
        return {};
    }
    const DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        return {};
    }
    return path;
}

bool HasMatchingHardwareId(const wchar_t* multiSz, size_t charCount)
{
    size_t pos = 0;
    while (pos < charCount && multiSz[pos] != L'\0') {
        const size_t remaining = charCount - pos;
        const wchar_t* current = multiSz + pos;
        const wchar_t* terminator =
            static_cast<const wchar_t*>(wmemchr(current, L'\0', remaining));
        if (terminator == nullptr) {
            break;
        }
        if (_wcsicmp(current, kRootHardwareId) == 0) {
            return true;
        }
        pos += static_cast<size_t>(terminator - current) + 1;
    }
    return false;
}

bool RootDeviceExists(const GUID& classGuid, bool& found)
{
    found = false;
    HDEVINFO devices = SetupDiGetClassDevsW(&classGuid, nullptr, nullptr, 0);
    if (devices == INVALID_HANDLE_VALUE) {
        std::wcerr << L"SetupDiGetClassDevs failed: " << GetLastError() << L"\n";
        return false;
    }

    for (DWORD index = 0;; ++index) {
        SP_DEVINFO_DATA device{};
        device.cbSize = sizeof(device);
        if (!SetupDiEnumDeviceInfo(devices, index, &device)) {
            const DWORD error = GetLastError();
            if (error != ERROR_NO_MORE_ITEMS) {
                std::wcerr << L"SetupDiEnumDeviceInfo failed: " << error << L"\n";
                SetupDiDestroyDeviceInfoList(devices);
                return false;
            }
            break;
        }

        wchar_t ids[1024]{};
        DWORD type = 0;
        DWORD bytes = 0;
        if (SetupDiGetDeviceRegistryPropertyW(
                devices, &device, SPDRP_HARDWAREID, &type,
                reinterpret_cast<PBYTE>(ids), sizeof(ids), &bytes) &&
            type == REG_MULTI_SZ && bytes <= sizeof(ids) &&
            bytes % sizeof(wchar_t) == 0 &&
            HasMatchingHardwareId(ids, bytes / sizeof(wchar_t))) {
            found = true;
            break;
        }
    }

    SetupDiDestroyDeviceInfoList(devices);
    return true;
}

bool RegisterRootDevice(const GUID& classGuid)
{
    HDEVINFO devices = SetupDiCreateDeviceInfoList(&classGuid, nullptr);
    if (devices == INVALID_HANDLE_VALUE) {
        std::wcerr << L"SetupDiCreateDeviceInfoList failed: " << GetLastError() << L"\n";
        return false;
    }

    SP_DEVINFO_DATA device{};
    device.cbSize = sizeof(device);
    if (!SetupDiCreateDeviceInfoW(
            devices, kDeviceName, &classGuid, nullptr, nullptr,
            DICD_GENERATE_ID, &device)) {
        std::wcerr << L"SetupDiCreateDeviceInfo failed: " << GetLastError() << L"\n";
        SetupDiDestroyDeviceInfoList(devices);
        return false;
    }

    // A valid REG_MULTI_SZ has two trailing NUL characters.
    constexpr wchar_t hardwareIds[] = L"Root\\AvCamVirtualUsbHost\0";
    if (!SetupDiSetDeviceRegistryPropertyW(
            devices, &device, SPDRP_HARDWAREID,
            reinterpret_cast<const BYTE*>(hardwareIds),
            static_cast<DWORD>(sizeof(hardwareIds)))) {
        std::wcerr << L"SetupDiSetDeviceRegistryProperty failed: " << GetLastError() << L"\n";
        SetupDiDestroyDeviceInfoList(devices);
        return false;
    }

    if (!SetupDiCallClassInstaller(DIF_REGISTERDEVICE, devices, &device)) {
        std::wcerr << L"DIF_REGISTERDEVICE failed: " << GetLastError() << L"\n";
        SetupDiDestroyDeviceInfoList(devices);
        return false;
    }

    SetupDiDestroyDeviceInfoList(devices);
    std::wcout << L"Registered root-enumerated device " << kRootHardwareId << L"\n";
    return true;
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
    if (argc != 3 || wcscmp(argv[1], L"install") != 0) {
        std::wcerr << L"Usage: AvCamBusInstall.exe install <absolute-or-relative-path-to-AvCamBus.inf>\n"
                   << L"Run from an elevated PowerShell session on a dedicated test system.\n";
        return 2;
    }

    const std::wstring infPath = GetFullInfPath(argv[2]);
    if (infPath.empty()) {
        std::wcerr << L"INF file does not exist: " << argv[2] << L"\n";
        return 2;
    }

    GUID classGuid{};
    wchar_t className[256]{};
    if (!SetupDiGetINFClassW(
            infPath.c_str(), &classGuid, className,
            static_cast<DWORD>(_countof(className)), nullptr)) {
        std::wcerr << L"SetupDiGetINFClass failed: " << GetLastError() << L"\n";
        return 1;
    }

    bool exists = false;
    if (!RootDeviceExists(classGuid, exists)) {
        return 1;
    }
    if (!exists && !RegisterRootDevice(classGuid)) {
        return 1;
    }
    if (exists) {
        std::wcout << L"Existing root device found; updating driver instead of creating a duplicate.\n";
    }

    BOOL rebootRequired = FALSE;
    if (!UpdateDriverForPlugAndPlayDevicesW(
            nullptr, kRootHardwareId, infPath.c_str(),
            INSTALLFLAG_FORCE, &rebootRequired)) {
        std::wcerr << L"UpdateDriverForPlugAndPlayDevices failed: "
                   << GetLastError() << L"\n"
                   << L"Inspect C:\\Windows\\INF\\setupapi.dev.log and the device's PnP problem code.\n";
        return 1;
    }

    std::wcout << L"Driver installation API succeeded. Check device status before plugging a child.\n";
    if (rebootRequired) {
        std::wcout << L"Windows reports that a restart is required.\n";
    }
    return 0;
}
