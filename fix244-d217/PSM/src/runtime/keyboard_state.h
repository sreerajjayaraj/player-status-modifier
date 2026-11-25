#pragma once

#include "runtime/foreground_input.h"

namespace psm::input {

using GetAsyncKeyStateFn = SHORT(WINAPI*)(int);
inline INIT_ONCE g_key_state_once = INIT_ONCE_STATIC_INIT;
inline GetAsyncKeyStateFn g_key_state_function = nullptr;

inline BOOL CALLBACK InitializeKeyState(PINIT_ONCE, PVOID, PVOID*) {
    const HMODULE user32 = LoadLibraryW(L"user32.dll");
    if (!user32) return FALSE;
    g_key_state_function = reinterpret_cast<GetAsyncKeyStateFn>(GetProcAddress(user32, "GetAsyncKeyState"));
    return TRUE;
}

inline GetAsyncKeyStateFn ResolveGetAsyncKeyState() {
    // First use is on a worker/game thread, not during DllMain. INIT_ONCE
    // avoids compiler TLS guards without weakening thread-safe initialization.
    if (!InitOnceExecuteOnce(&g_key_state_once, InitializeKeyState, nullptr, nullptr)) return nullptr;
    return g_key_state_function;
}

inline bool IsKeyDown(const ForegroundInput& focus, int key) noexcept {
    if (!key || !focus.Active()) return false;
    const auto poll = ResolveGetAsyncKeyState();
    return poll && focus.ReadButton([&] { return (poll(key) & 0x8000) != 0; });
}

}  // namespace psm::input
