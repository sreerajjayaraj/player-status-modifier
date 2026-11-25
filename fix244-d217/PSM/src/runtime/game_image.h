#pragma once

#include <Windows.h>
#include <cwchar>

namespace psm::game {

inline constexpr wchar_t kExecutableName[] = L"CrimsonDesert.exe";

inline HMODULE MainModule() noexcept {
    const auto named = GetModuleHandleW(kExecutableName);
    return named && named == GetModuleHandleW(nullptr) ? named : nullptr;
}

inline bool IsHost() noexcept {
    wchar_t path[32768]{};
    const auto length = GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
    if (!length || length >= ARRAYSIZE(path)) return false;
    const wchar_t* basename = path;
    for (const auto* ch = path; *ch; ++ch) {
        if (*ch == L'\\' || *ch == L'/') basename = ch + 1;
    }
    return _wcsicmp(basename, kExecutableName) == 0 && MainModule();
}

}  // namespace psm::game
