#pragma once
// The strict physical-camera acceptance gate is deliberately independent
// of Windows PnP display names and software-camera friendly-name spoofing.
#include <string>
#include <string_view>
#include <algorithm>
#include <cwctype>

namespace avcam::acceptance {
inline std::wstring Upper(std::wstring_view s) {
    std::wstring out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](wchar_t c) { return std::towupper(c); });
    return out;
}

// Match the VID/PID at the FIRST USB identity token only. Finding these
// bytes somewhere later in a string is not proof of a physical C270.
inline bool IsPhysicalLogitechC270Instance(std::wstring_view pnpInstanceId) {
    const auto id = Upper(pnpInstanceId);
    constexpr std::wstring_view prefix = L"USB\\VID_046D&PID_0825";
    if (id.rfind(prefix, 0) != 0) return false;
    return id.size() == prefix.size() ||
           id[prefix.size()] == L'&' || id[prefix.size()] == L'\\';
}

inline bool IsPhysicalLogitechC270InterfacePath(std::wstring_view interfacePath) {
    const auto path = Upper(interfacePath);
    constexpr std::wstring_view win32Prefix = L"\\\\?\\USB#VID_046D&PID_0825";
    constexpr std::wstring_view ntPrefix = L"\\??\\USB#VID_046D&PID_0825";
    const auto p = path.rfind(win32Prefix, 0) == 0 ? win32Prefix :
                   path.rfind(ntPrefix, 0) == 0 ? ntPrefix : std::wstring_view{};
    if (p.empty()) return false;
    return path.size() == p.size() ||
           path[p.size()] == L'&' || path[p.size()] == L'#';
}

inline bool IsExistingPhysicalC270Interface(
    std::wstring_view originalInterfacePath,
    std::wstring_view chromeSelectedInterfacePath,
    std::wstring_view pnpInstanceId) {
    // Device interface symbolic links can be compared case-insensitively.
    // Two cameras that happen to share a friendly name are NOT identical.
    return !originalInterfacePath.empty() &&
           !chromeSelectedInterfacePath.empty() &&
           Upper(originalInterfacePath) == Upper(chromeSelectedInterfacePath) &&
           IsPhysicalLogitechC270InterfacePath(originalInterfacePath) &&
           IsPhysicalLogitechC270Instance(pnpInstanceId);
}

inline bool StrictCaptureAcceptance(
    bool deepLivePreviewChanging,
    bool obsProcessedFramesChanging,
    bool chromeReceivedProcessedFrames,
    bool deepLiveAndChromeSimultaneous,
    bool noCaptureLoop,
    bool fallbackRawAfterObsStops,
    bool originalCameraDriverPreserved,
    std::wstring_view physicalInterface,
    std::wstring_view chromeInterface,
    std::wstring_view pnpInstanceId) {
    return deepLivePreviewChanging && obsProcessedFramesChanging &&
           chromeReceivedProcessedFrames && deepLiveAndChromeSimultaneous &&
           noCaptureLoop && fallbackRawAfterObsStops &&
           originalCameraDriverPreserved &&
           IsExistingPhysicalC270Interface(
               physicalInterface, chromeInterface, pnpInstanceId);
}
} // namespace avcam::acceptance
