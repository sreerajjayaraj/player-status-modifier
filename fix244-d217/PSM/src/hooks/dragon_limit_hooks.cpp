#include "hooks/game_hooks.h"
#include "hooks/hooks_internal.h"

#include "config.h"
#include "logger.h"
#include "mod_logic.h"
#include "scanner.h"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <utility>

namespace {

constexpr uintptr_t kZeroFlagMask = 1ull << 6;
std::atomic<std::uint32_t> g_dragon_village_summon_samples{0};
std::atomic<std::uint32_t> g_dragon_roof_restrict_samples{0};

bool IsPlayerContextReady() {
    return GetTrackedPlayerActor() >= kMinimumPointerAddress &&
           GetTrackedPlayerStatusMarker() >= kMinimumPointerAddress;
}

void DragonVillageSummonCallback(SafetyHookContext& ctx) {
    const auto config = GetConfig();
    if (!config.general.enabled || !config.dragon_limit.village_summon || !IsPlayerContextReady()) {
        return;
    }

    ctx.rflags |= kZeroFlagMask;
    if (ShouldLogSample(g_dragon_village_summon_samples, 16)) {
        Log("hooks: dragon village summon bypassed rip=0x%p player=0x%p marker=0x%p",
            reinterpret_cast<void*>(ctx.rip),
            reinterpret_cast<void*>(GetTrackedPlayerActor()),
            reinterpret_cast<void*>(GetTrackedPlayerStatusMarker()));
    }
}

// Replay the original two stores through SafetyHook. Only AL differs
// after the same mode, result and context-type checks as the original route.
void DragonFlyingRestrictCallback(SafetyHookContext& ctx) {
    if (static_cast<uint32_t>(ctx.rdi) != 0x247 || (ctx.rax & 0xFF)) return;
    __try {
        if (*reinterpret_cast<const uint32_t*>(ctx.rbx) == 6) ctx.rax |= 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
}

void DragonRoofRestrictCallback(SafetyHookContext& ctx) {
    const auto config = GetConfig();
    if (!config.general.enabled || !config.dragon_limit.roof_summon_experimental || !IsPlayerContextReady()) {
        return;
    }

    ctx.rbx = 0;
    if (ShouldLogSample(g_dragon_roof_restrict_samples, 16)) {
        Log("hooks: dragon roof summon experimental bypassed rip=0x%p player=0x%p marker=0x%p",
            reinterpret_cast<void*>(ctx.rip),
            reinterpret_cast<void*>(GetTrackedPlayerActor()),
            reinterpret_cast<void*>(GetTrackedPlayerStatusMarker()));
    }
}

bool InstallDragonVillageSummonHook() {
    const uintptr_t target = ScanForDragonVillageSummonJump();
    if (target == 0) {
        return false;
    }

    auto hook_result = psm::game::CreateMid(reinterpret_cast<void*>(target), DragonVillageSummonCallback);
    if (!hook_result.has_value()) {
        Log("hooks: failed to create dragon-village-summon mid hook");
        return false;
    }

    g_dragon_village_summon_hook = std::move(*hook_result);
    Log("hooks: installed dragon-village-summon hook at 0x%p", reinterpret_cast<void*>(target));
    return true;
}

bool InstallDragonFlyingRestrictHook() {
    const uintptr_t target = ScanForDragonFlyingRestrictWrite();
    if (target == 0) {
        return false;
    }

    auto hook = psm::game::CreateMid(reinterpret_cast<void*>(target), DragonFlyingRestrictCallback);
    if (!hook) {
        Log("hooks: failed to create dragon-flying-restrict mid hook");
        return false;
    }
    g_dragon_flying_restrict_hook = std::move(*hook);
    Log("hooks: installed dragon-flying-restrict hook at 0x%p", reinterpret_cast<void*>(target));
    return true;
}

bool InstallDragonRoofRestrictHook() {
    const uintptr_t target = ScanForDragonRoofRestrictTest();
    if (target == 0) {
        return false;
    }

    auto hook_result = psm::game::CreateMid(reinterpret_cast<void*>(target), DragonRoofRestrictCallback);
    if (!hook_result.has_value()) {
        Log("hooks: failed to create dragon-roof-restrict mid hook");
        return false;
    }

    g_dragon_roof_restrict_hook = std::move(*hook_result);
    Log("hooks: installed dragon-roof-restrict hook at 0x%p", reinterpret_cast<void*>(target));
    return true;
}

}  // namespace

bool InstallDragonLimitHooks() {
    const auto config = GetConfig();

    if (ShouldInstallDragonVillageSummonHook(config) && !InstallDragonVillageSummonHook()) {
        Log("hooks: dragon-village-summon unavailable; continuing without village summon bypass");
    }

    if (ShouldInstallDragonFlyingRestrictHook(config) && !InstallDragonFlyingRestrictHook()) {
        Log("hooks: dragon-flying-restrict unavailable; continuing without flying restrict bypass");
    }

    if (ShouldInstallDragonRoofRestrictHook(config) && !InstallDragonRoofRestrictHook()) {
        Log("hooks: dragon-roof-restrict unavailable; continuing without roof summon bypass");
    }

    return true;
}

void RemoveDragonLimitHooks() {
    if (g_dragon_roof_restrict_hook) {
        g_dragon_roof_restrict_hook.reset();
        Log("hooks: removed dragon-roof-restrict hook");
    }

    if (g_dragon_flying_restrict_hook) {
        g_dragon_flying_restrict_hook.reset();
        Log("hooks: removed dragon-flying-restrict hook");
    }

    if (g_dragon_village_summon_hook) {
        g_dragon_village_summon_hook.reset();
        Log("hooks: removed dragon-village-summon hook");
    }
}
