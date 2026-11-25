#pragma once

#include "native_3446.h"
#include "runtime/game_image.h"
#include <safetyhook.hpp>
#include <cstring>
#include <type_traits>

namespace psm::game {

inline bool ExecutableSpan(HMODULE game, uintptr_t address, size_t length) noexcept {
    if (!game || !address || !length || length > UINTPTR_MAX - address) return false;
    psm::v3446::Image image;
    const auto base = reinterpret_cast<uintptr_t>(game);
    if (address < base || !image.Init(game, psm::v3446::ModuleSize(game)) ||
        !image.Exec(address - base, length)) return false;

    const auto end = address + length;
    while (address < end) {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(reinterpret_cast<const void*>(address), &region, sizeof(region)) ||
            region.AllocationBase != game || region.Type != MEM_IMAGE || region.State != MEM_COMMIT ||
            (region.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
        const auto protection = region.Protect & 0xFF;
        if (protection != PAGE_EXECUTE_READ && protection != PAGE_EXECUTE_READWRITE &&
            protection != PAGE_EXECUTE_WRITECOPY) return false;
        const auto next = reinterpret_cast<uintptr_t>(region.BaseAddress) + region.RegionSize;
        if (next <= address) return false;
        address = next;
    }
    return true;
}

// SafetyHook may decode a whole final x64 instruction (up to 15 bytes) while
// preparing either a 5-byte relative or a 14-byte absolute entry splice.
inline constexpr size_t kDecodeSpan = 14 + 15 - 1;

template<class Hook>
bool OriginalSpanUnchanged(const Hook& hook) noexcept {
    const auto& bytes = hook.original_bytes();
    return !bytes.empty() && ExecutableSpan(MainModule(), hook.target_address(), bytes.size()) &&
        !std::memcmp(hook.target(), bytes.data(), bytes.size());
}

template<class Hook>
bool EnablePreparedHook(Hook& hook) {
    return OriginalSpanUnchanged(hook) && hook.enable().has_value();
}

template<class Hook, class Destination>
auto PrepareHook(void* target, Destination callback, typename Hook::Flags flags) {
    using Error = typename Hook::Error;
    const auto reject = [&] {
        auto error = SafetyHookInline::Error::not_enough_space(static_cast<uint8_t*>(target));
        if constexpr (std::is_same_v<Hook, SafetyHookMid>) {
            return std::unexpected(Error::bad_inline_hook(error));
        } else {
            return std::unexpected(error);
        }
    };
    using Result = std::expected<Hook, Error>;
    const auto module = MainModule();
    const auto address = reinterpret_cast<uintptr_t>(target);
    if (!ExecutableSpan(module, address, kDecodeSpan)) return Result(reject());

    auto hook = Hook::create(target, callback, Hook::StartDisabled);
    if (!hook) return hook;
    const auto& original = hook->original_bytes();
    if (original.size() > kDecodeSpan || !OriginalSpanUnchanged(*hook)) return Result(reject());
    if (flags != Hook::StartDisabled) {
        const auto enabled = hook->enable();
        if (!enabled) return Result(std::unexpected(enabled.error()));
    }
    return hook;
}

inline auto CreateMid(void* target, safetyhook::MidHookFn callback,
                      SafetyHookMid::Flags flags = SafetyHookMid::Default) {
    return PrepareHook<SafetyHookMid>(target, callback, flags);
}

inline auto CreateInline(void* target, void* callback,
                         SafetyHookInline::Flags flags = SafetyHookInline::Default) {
    return PrepareHook<SafetyHookInline>(target, callback, flags);
}

}  // namespace psm::game
