#pragma once
// A read-only PnP/Media Foundation enumeration check, NOT a capture test.
// Seeing two interfaces does not prove Chrome will successfully activate
// this device or that both API views refer to the same physical instance.
#include <cstddef>

namespace avcam::readiness {
enum class Result { Absent, Ambiguous, Enumerated };

constexpr Result Evaluate(std::size_t privateSinkInterfaces,
                          std::size_t mediaFoundationNames) noexcept {
    if (privateSinkInterfaces == 0 || mediaFoundationNames == 0)
        return Result::Absent;
    if (privateSinkInterfaces != 1 || mediaFoundationNames != 1)
        return Result::Ambiguous;
    return Result::Enumerated;
}
constexpr int ExitCode(Result result) noexcept {
    switch (result) {
    case Result::Enumerated: return 0;
    case Result::Absent: return 6;
    case Result::Ambiguous: return 7;
    }
    return 7;
}
constexpr const wchar_t* Label(Result result) noexcept {
    switch (result) {
    case Result::Enumerated: return L"ENUMERATED_NOT_ACTIVATED";
    case Result::Absent: return L"ABSENT";
    case Result::Ambiguous: return L"AMBIGUOUS";
    }
    return L"AMBIGUOUS";
}
} // namespace avcam::readiness
