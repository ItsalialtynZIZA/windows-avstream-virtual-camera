#pragma once
/*
 * Portable Media Foundation camera selector.
 *
 * A Windows video capture friendly name is not unique. For the physical
 * Logitech C270 dry-run, require BOTH the expected display name and a
 * USB symbolic-link VID/PID so another camera cannot be selected silently.
 * This does not alter camera drivers or claim a virtual device is physical.
 */
#include <string>

namespace AvCamSourceSelection {

inline wchar_t AsciiLower(wchar_t ch) noexcept {
    return ch >= L'A' && ch <= L'Z' ?
        static_cast<wchar_t>(ch - L'A' + L'a') : ch;
}

inline std::wstring LowerAscii(const std::wstring& text) {
    std::wstring output;
    output.reserve(text.size());
    for (wchar_t ch : text) output.push_back(AsciiLower(ch));
    return output;
}

inline bool ContainsName(const std::wstring& name,
                         const std::wstring& desired) {
    return LowerAscii(name).find(LowerAscii(desired)) != std::wstring::npos;
}

/* Require a full VID_xxxx&PID_xxxx pair, not an ambiguous partial ID.
 * A boundary check prevents PID_0825 from matching PID_08250.
 */
inline bool IsVidPidPair(const std::wstring& pair) noexcept {
    if (pair.size() != 17u) return false;
    const std::wstring marker = L"vid_";
    const std::wstring product = L"pid_";
    for (unsigned i = 0; i < 4u; ++i) {
        if (AsciiLower(pair[i]) != marker[i] ||
            AsciiLower(pair[i + 9u]) != product[i]) return false;
    }
    if (pair[8] != L'&') return false;
    auto hex = [](wchar_t ch) {
        ch = AsciiLower(ch);
        return (ch >= L'0' && ch <= L'9') ||
               (ch >= L'a' && ch <= L'f');
    };
    for (unsigned i = 4; i < 8; ++i) if (!hex(pair[i])) return false;
    for (unsigned i = 13; i < 17; ++i) if (!hex(pair[i])) return false;
    return true;
}

inline bool HasUsbIdentity(const std::wstring& symbolicLink,
                           const std::wstring& vidPidPair) {
    if (!IsVidPidPair(vidPidPair)) return false;
    const std::wstring link = LowerAscii(symbolicLink);
    const std::wstring pair = LowerAscii(vidPidPair);
    std::size_t pos = link.find(pair);
    while (pos != std::wstring::npos) {
        const bool before = pos == 0u ||
            link[pos - 1u] == L'#' ||
            link[pos - 1u] == L'\\';
        const std::size_t end = pos + pair.size();
        const bool after = end == link.size() ||
            link[end] == L'&' ||
            link[end] == L'#' ||
            link[end] == L'\\';
        if (before && after) return true;
        pos = link.find(pair, pos + 1u);
    }
    return false;
}

inline bool MatchesSource(const std::wstring& name,
                          const std::wstring& symbolicLink,
                          const std::wstring& requestedName,
                          const std::wstring& requestedUsbId) {
    if (!ContainsName(name, requestedName)) return false;
    return requestedUsbId.empty() ||
        HasUsbIdentity(symbolicLink, requestedUsbId);
}

} // namespace AvCamSourceSelection
