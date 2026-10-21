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

inline bool HasExactC270VidPid(std::wstring_view source, wchar_t delimiter) {
    constexpr std::wstring_view kId = L"VID_046D&PID_0825";
    const auto pos = source.find(kId);
    if (pos == std::wstring_view::npos) return false;
    const auto end = pos + kId.size();
    return (pos == 0 || source[pos - 1] == delimiter ||
            source[pos - 1] == L'&') &&
           (end == source.size() || source[end] == delimiter ||
            source[end] == L'&');
}

inline bool IsPhysicalLogitechC270Instance(std::wstring_view pnpInstanceId) {
    auto id = Upper(pnpInstanceId);
    // A USB instance, not USBSTOR, ROOT, SWD, or a friendly-name alias.
    return id.rfind(L"USB\\", 0) == 0 &&
           HasExactC270VidPid(id, L'\\');
}

inline bool IsPhysicalLogitechC270InterfacePath(std::wstring_view interfacePath) {
    const auto path = Upper(interfacePath);
    // Require the actual USB device interface namespace and VID/PID tokens.
    // A friendly name, SWD virtual camera or ROOT software path is insufficient.
    return (path.rfind(L"\\\\?\\USB#", 0) == 0 ||
            path.rfind(L"\\??\\USB#", 0) == 0) &&
           HasExactC270VidPid(path, L'#');
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
