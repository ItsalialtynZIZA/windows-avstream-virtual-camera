// Read-only enumeration of the Windows camera interfaces and associated PnP IDs.
// No driver installation, registry writes, camera acquisition or device activation.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <ks.h>
#include <ksmedia.h>
#include <iostream>
#include <string>
#include <vector>
#include <utility>
#include <initializer_list>
#include "PhysicalEndpointAcceptance.h"

#pragma comment(lib, "setupapi.lib")

struct MatchStats {
    unsigned found = 0;
    unsigned exactMatches = 0;
};

static MatchStats EnumCategory(const GUID& category, const wchar_t* label,
                               const std::wstring& expectedPath) {
    MatchStats stats{};
    HDEVINFO info = SetupDiGetClassDevsW(&category, nullptr, nullptr,
                                        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (info == INVALID_HANDLE_VALUE) {
        std::wcerr << L"ENUM_FAILED=" << label << L" code=" << GetLastError() << L"\n";
        return stats;
    }
    for (DWORD index = 0;; ++index) {
        SP_DEVICE_INTERFACE_DATA iface{};
        iface.cbSize = sizeof(iface);
        if (!SetupDiEnumDeviceInterfaces(info, nullptr, &category, index, &iface)) {
            if (GetLastError() != ERROR_NO_MORE_ITEMS)
                std::wcerr << L"INTERFACE_ENUM_ERROR=" << GetLastError() << L"\n";
            break;
        }
        DWORD required = 0;
        SetupDiGetDeviceInterfaceDetailW(info, &iface, nullptr, 0, &required, nullptr);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || required == 0)
            continue;
        std::vector<BYTE> buffer(required);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        SP_DEVINFO_DATA device{};
        device.cbSize = sizeof(device);
        if (!SetupDiGetDeviceInterfaceDetailW(info, &iface, detail, required, nullptr, &device))
            continue;
        wchar_t instance[MAX_DEVICE_ID_LEN + 1]{};
        if (!SetupDiGetDeviceInstanceIdW(info, &device, instance, MAX_DEVICE_ID_LEN + 1, nullptr))
            continue;
        const std::wstring path(detail->DevicePath);
        const bool c270 = avcam::acceptance::IsPhysicalLogitechC270Instance(instance)
                       && avcam::acceptance::IsPhysicalLogitechC270InterfacePath(path);
        // Do not print non-C270 device names, paths or serials.
        if (c270) {
            ++stats.found;
            if (!expectedPath.empty() &&
                avcam::acceptance::Upper(expectedPath) ==
                    avcam::acceptance::Upper(path))
                ++stats.exactMatches;
            std::wcout << L"CATEGORY=" << label << L"\n"
                       << L"C270_PNP_INSTANCE=" << instance << L"\n"
                       << L"C270_INTERFACE=" << path << L"\n"
                       << L"C270_PHYSICAL_PATH_SHAPE=PASS\n";
        }
    }
    SetupDiDestroyDeviceInfoList(info);
    return stats;
}

int wmain(int argc, wchar_t** argv) {
    const bool verify = argc == 3 && std::wstring(argv[1]) == L"--verify-interface";
    if (argc != 1 && !verify) {
        std::wcerr << L"USAGE: AvCamEnumeratePhysicalC270.exe [--verify-interface <device-interface-path>]\\n";
        return 2;
    }
    const std::wstring expectedPath = verify ? argv[2] : L"";
    std::wcout << L"AVCAM_READONLY_PNP_ENUM_BEGIN\n";
    MatchStats total{};
    const auto a = EnumCategory(KSCATEGORY_VIDEO_CAMERA, L"VIDEO_CAMERA", expectedPath);
    const auto b = EnumCategory(KSCATEGORY_CAPTURE, L"CAPTURE", expectedPath);
    const auto d = EnumCategory(KSCATEGORY_VIDEO, L"VIDEO", expectedPath);
    total.found = a.found + b.found + d.found;
    total.exactMatches = a.exactMatches + b.exactMatches + d.exactMatches;
    std::wcout << L"AVCAM_PHYSICAL_C270_INTERFACES_FOUND=" << total.found << L"\n";
    if (!total.found)
        std::wcout << L"AVCAM_NO_PHYSICAL_C270_INTERFACE_MATCH\n";
    if (verify)
        std::wcout << L"AVCAM_SELECTED_INTERFACE_IS_CURRENT_C270="
                   << (total.exactMatches ? L"YES" : L"NO") << L"\n";
    std::wcout << L"AVCAM_READONLY_PNP_ENUM_COMPLETE\n";
    return verify && !total.exactMatches ? 3 : 0;
}
