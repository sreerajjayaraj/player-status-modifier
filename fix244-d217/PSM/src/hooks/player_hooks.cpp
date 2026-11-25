#include "hooks/game_hooks.h"
#include "runtime/ship_trace.h"
#include "hooks/hooks_internal.h"
#include "hooks/hud_hooks.h"
#include "hooks/mounted_hooks.h"
#include "hooks/oxygen_hooks.h"
#include "hooks/startup_mid_hook.h"
#include "runtime/oxygen_resource.h"
#include "runtime/resource_hud.h"

#include "config.h"
#include "logger.h"
#include "mod_logic.h"
#include "runtime/actor_resolve.h"
#include "runtime/resource_routing_policy.h"
#include "runtime/native_resource_scaling.h"
#include "runtime/mounted_resource.h"
#include "runtime/keyboard_state.h"
#include "runtime/stat_logic.h"
#include "scanner.h"

#include <Windows.h>

#include <array>
#include <algorithm>
#include <limits>
#include <cstring>
#include <utility>

namespace {

SafetyHookMid g_native_resource_rate_hook{};
std::atomic<bool> g_oxygen_rate_has_deadline{false};

// The rate hook runs before the reader's cached minimum-expiry branch.
bool ValidateOxygenRateDeadline(uintptr_t site) {
    static constexpr std::array<unsigned char, 8> load{
        0x4c, 0x8b, 0x43, 0x40, 0x48, 0x8d, 0x4b, 0x28};
    static constexpr std::array<unsigned char, 32> branch{
        0x4d, 0x85, 0xc0, 0x74, 0x1c, 0x48, 0x8d, 0x4b, 0x28, 0x4c, 0x3b, 0xc6,
        0x73, 0x13, 0x48, 0x8d, 0x4b, 0x28, 0x49, 0x3b, 0xc0, 0x73, 0x0a, 0x49,
        0x8b, 0xc0, 0x48, 0x8d, 0x4b, 0x28, 0x4d, 0x8b};
    constexpr std::size_t load_distance = 0x39;
    const auto module = psm::game::MainModule();
    return site >= load_distance &&
        psm::game::ExecutableSpan(module, site - load_distance, load_distance + branch.size() + 1) &&
        !std::memcmp(reinterpret_cast<const void*>(site - load_distance), load.data(), load.size()) &&
        !std::memcmp(reinterpret_cast<const void*>(site), branch.data(), branch.size()) &&
        *reinterpret_cast<const unsigned char*>(site + branch.size()) == 0xca;
}

// Keep debits and refills independently observable per resource.
std::array<std::atomic<std::uint32_t>, 4> g_native_rate_samples{};
std::array<std::atomic<std::uint32_t>, 4> g_native_cost_samples{};
std::atomic<uintptr_t> g_native_stamina_binding{0};
std::atomic<uintptr_t> g_native_spirit_binding{0};

bool TryScaleNativeResource(const uintptr_t entry, const int64_t value,
                            int64_t* scaled, const bool rate) {
    if (!g_runtime_enabled.load(std::memory_order_acquire) || !scaled ||
        !IsAuthoritativePlayerResourceDeltaActive()) return false;
    const auto role = psm::routing::SelectAuthoritativeSharedDeltaRoute(
        entry, GetTrackedPlayerStaminaEntry(), GetTrackedPlayerSpiritEntry());
    if (role == psm::routing::DiscoveredPlayerResourceRole::None) return false;
    const auto config = GetConfig();
    const bool stamina = role == psm::routing::DiscoveredPlayerResourceRole::Stamina;
    const bool use_stamina_config = stamina != config.advanced.swap_player_stamina_spirit;
    const auto& stat = use_stamina_config ? config.stamina : config.spirit;
    if (!config.general.enabled || !stat.enabled) return false;
    auto& binding = stamina ? g_native_stamina_binding : g_native_spirit_binding;
    const auto role_channel = stamina ? 0u : 2u;
    if (binding.exchange(entry, std::memory_order_relaxed) != entry) {
        for (auto channel = role_channel; channel != role_channel + 2; ++channel) {
            g_native_rate_samples[channel].store(0, std::memory_order_relaxed);
            g_native_cost_samples[channel].store(0, std::memory_order_relaxed);
        }
        Log("hooks: native-resource-bind role=%s entry=0x%p consumption=%.3f heal=%.3f config-swap=%d",
            stamina ? "stamina" : "spirit", reinterpret_cast<void*>(entry),
            stat.consumption_multiplier, stat.heal_multiplier,
            config.advanced.swap_player_stamina_spirit ? 1 : 0);
    }
    *scaled = psm::native_resource::ScaleSigned(
        value, stat.consumption_multiplier, stat.heal_multiplier);
    if (!value) return true;
    auto& samples = rate ? g_native_rate_samples : g_native_cost_samples;
    const auto channel = role_channel + (value < 0 ? 0u : 1u);
    const auto sample = samples[channel].fetch_add(1, std::memory_order_relaxed);
    if (sample < 16 || (sample % 32768) == 0) {
        Log("hooks: native-resource-%s role=%s entry=0x%p original=%lld scaled=%lld consumption=%.3f heal=%.3f stored-rate-unchanged=1",
            rate ? "rate" : "cost", stamina ? "stamina" : "spirit",
            reinterpret_cast<void*>(entry), static_cast<long long>(value),
            static_cast<long long>(*scaled), stat.consumption_multiplier, stat.heal_multiplier);
    }
    return true;
}

void NativeResourceRateCallback(SafetyHookContext& ctx) {
    int64_t scaled = static_cast<int64_t>(ctx.r11);
    if (TryLockPlayerOxygen(ctx.rbx, scaled, &scaled, true)) {
        ctx.r11 = static_cast<uintptr_t>(scaled);
        // The game predicts a zero crossing from the stored rate. Ignore that
        // cached deadline for this negative projection while holding Breath.
        if (g_oxygen_rate_has_deadline.load(std::memory_order_acquire)) ctx.r8 = 0;
        return;
    }
    if (TryLockNativeMountedResource(ctx.rbx, scaled, &scaled, true) ||
        TryScaleNativeResource(ctx.rbx, scaled, &scaled, true)) {
        ctx.r11 = static_cast<uintptr_t>(scaled);
    }
}

void NativeResourceCostCallback(SafetyHookContext& ctx) {
    int64_t scaled = static_cast<int64_t>(ctx.rbx);
    if (TryLockPlayerOxygen(ctx.rsi, scaled, &scaled, false) ||
        TryLockNativeMountedResource(ctx.rsi, scaled, &scaled, false) ||
        TryScaleNativeResource(ctx.rsi, scaled, &scaled, false)) {
        ctx.rbx = static_cast<uintptr_t>(scaled);
    }
}

std::atomic<std::uint32_t> g_player_pointer_change_logs{0};
std::atomic<uintptr_t> g_last_player_pointer_actor{0};
std::atomic<uintptr_t> g_last_player_pointer_marker{0};
std::atomic<PlayerPointerCaptureKind> g_player_pointer_capture_kind{
    PlayerPointerCaptureKind::LegacyMarkerRsi};
std::atomic<uintptr_t> g_last_stats_entry{0};
std::atomic<uintptr_t> g_last_stats_component{0};
std::atomic<uintptr_t> g_last_stats_rip{0};
std::atomic<std::uint32_t> g_special_mount_health_write_samples{0};
std::atomic<std::uint32_t> g_focus_guard_logs{0};
std::atomic<DWORD> g_last_focus_combo_tick{0};
std::atomic<std::uint32_t> g_resource_write_candidate_logs{0};
std::atomic<std::uint32_t> g_untracked_spirit_delta_logs{0};
std::atomic<std::uint32_t> g_spirit_register_scan_logs{0};
std::atomic<std::uint32_t> g_shared_commit_spirit_candidate_logs{0};
std::atomic<std::uint32_t> g_resource_authority_trace_logs{0};
std::atomic<std::uint32_t> g_large_spirit_ab00_samples{0};
std::atomic<uintptr_t> g_last_resource_commit_entry{0};
std::atomic<DWORD> g_last_resource_commit_tick{0};
std::atomic<bool> g_spirit_delta_selector_contract{false};
std::atomic<std::uint32_t> g_explicit_resource_delta_logs{0};
StatWriteValueRegister g_stat_write_value_register = StatWriteValueRegister::Rbx;
std::atomic<bool> g_stat_write_enforce_native_bounds{false};

constexpr int64_t kPlayerStaminaShapeMin =
    psm::routing::kSupportedPlayerStaminaMinimum;
constexpr int64_t kPlayerStaminaShapeMax =
    psm::routing::kSupportedPlayerStaminaMaximum;
constexpr int64_t kPlayerSpiritShapeMin =
    psm::routing::kSupportedPlayerSpiritMinimum;
constexpr int64_t kPlayerSpiritShapeMax =
    psm::routing::kSupportedPlayerSpiritMaximum;
constexpr int64_t kLooseMountResourceMin = 180000;
constexpr int64_t kLooseMountResourceMax = 10000000;
constexpr uintptr_t kSpiritDeltaSelectorStackOffset = 0x38;
constexpr DWORD kExplicitSpiritCommitWindowMs = 100;
constexpr DWORD kDirectStaminaCommitWindowMs = 10;
constexpr DWORD kSharedResourceDeltaOwnershipWindowMs = 20;

struct PendingExplicitSpiritDelta {
    DWORD thread_id = 0;
    uintptr_t entry = 0;
    uintptr_t alternate_entry = 0;
    uintptr_t context = 0;
    int64_t adjusted_delta = 0;
    DWORD tick = 0;
    bool active = false;
};

constexpr size_t kPendingExplicitSpiritDeltaSlots = 8;
SRWLOCK g_pending_explicit_spirit_delta_lock = SRWLOCK_INIT;
std::array<PendingExplicitSpiritDelta, kPendingExplicitSpiritDeltaSlots>
    g_pending_explicit_spirit_deltas{};

struct PendingDirectStaminaDelta {
    DWORD thread_id = 0;
    uintptr_t entry = 0;
    DWORD tick = 0;
    bool active = false;
};

constexpr size_t kPendingDirectStaminaDeltaSlots = 8;
SRWLOCK g_pending_direct_stamina_delta_lock = SRWLOCK_INIT;
std::array<PendingDirectStaminaDelta, kPendingDirectStaminaDeltaSlots>
    g_pending_direct_stamina_deltas{};

struct PendingSharedResourceDelta {
    DWORD thread_id = 0;
    uintptr_t entry = 0;
    psm::routing::DiscoveredPlayerResourceRole route =
        psm::routing::DiscoveredPlayerResourceRole::None;
    DWORD tick = 0;
    bool active = false;
};

constexpr size_t kPendingSharedResourceDeltaSlots = 8;
SRWLOCK g_pending_shared_resource_delta_lock = SRWLOCK_INIT;
std::array<PendingSharedResourceDelta, kPendingSharedResourceDeltaSlots>
    g_pending_shared_resource_deltas{};
std::atomic<std::uint32_t> g_stamina_route_authority_logs{0};

#ifndef VK_GAMEPAD_LEFT_THUMBSTICK_BUTTON
#define VK_GAMEPAD_LEFT_THUMBSTICK_BUTTON 0xC8
#endif

#ifndef VK_GAMEPAD_RIGHT_THUMBSTICK_BUTTON
#define VK_GAMEPAD_RIGHT_THUMBSTICK_BUTTON 0xC9
#endif


bool IsFocusComboWindowActive() {
    constexpr DWORD kFocusComboWindowMs = 750;
    const DWORD now = GetTickCount();

    const psm::input::ForegroundInput focus;
    const bool pressed = psm::input::IsKeyDown(focus, 'X') ||
        (psm::input::IsKeyDown(focus, VK_GAMEPAD_LEFT_THUMBSTICK_BUTTON) &&
         psm::input::IsKeyDown(focus, VK_GAMEPAD_RIGHT_THUMBSTICK_BUTTON));
    if (pressed && focus.Active()) {
        g_last_focus_combo_tick.store(now, std::memory_order_release);
        RecordFocusRecoveryWindow();
        return true;
    }

    const DWORD last_tick = g_last_focus_combo_tick.load(std::memory_order_acquire);
    return last_tick != 0 && now - last_tick <= kFocusComboWindowMs;
}

bool TryReadHookStatEntry(const uintptr_t entry,
                          int32_t* const stat_type,
                          int64_t* const current_value,
                          int64_t* const max_value) {
    if (stat_type == nullptr ||
        current_value == nullptr ||
        max_value == nullptr ||
        entry < kMinimumPointerAddress) {
        return false;
    }

    __try {
        *stat_type = *reinterpret_cast<const int32_t*>(entry);
        *current_value = *reinterpret_cast<const int64_t*>(entry + 0x08);
        *max_value = *reinterpret_cast<const int64_t*>(entry + 0x18);
        return *max_value > 0 && *current_value >= 0 && *current_value <= *max_value;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ValidateSpiritDeltaSelectorContract(const uintptr_t target) {
    if (target < kMinimumPointerAddress + 0x21) {
        return false;
    }

    static constexpr std::array<std::uint8_t, 5> kSelectorStore = {
        0x66, 0x89, 0x54, 0x24, 0x10,
    };
    static constexpr std::array<std::uint8_t, 5> kStackFrame = {
        0x57, 0x48, 0x83, 0xEC, 0x20,
    };
    static constexpr std::array<std::uint8_t, 12> kCallSetup = {
        0x49, 0x89, 0xD8,
        0x48, 0x89, 0xFA,
        0x48, 0x89, 0xC1,
        0x48, 0x89, 0xC6,
    };

    __try {
        return std::memcmp(reinterpret_cast<const void*>(target - 0x21),
                           kSelectorStore.data(),
                           kSelectorStore.size()) == 0 &&
               std::memcmp(reinterpret_cast<const void*>(target - 0x1C),
                           kStackFrame.data(),
                           kStackFrame.size()) == 0 &&
               std::memcmp(reinterpret_cast<const void*>(target - 0x0C),
                           kCallSetup.data(),
                           kCallSetup.size()) == 0 &&
               *reinterpret_cast<const std::uint8_t*>(target) == 0xE8;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool TryReadSpiritDeltaSelector(const SafetyHookContext& ctx, std::uint16_t* const selector) {
    if (selector == nullptr ||
        !g_spirit_delta_selector_contract.load(std::memory_order_acquire) ||
        ctx.rsp < kMinimumPointerAddress ||
        ctx.rsp > UINTPTR_MAX - kSpiritDeltaSelectorStackOffset) {
        return false;
    }

    __try {
        *selector = *reinterpret_cast<const std::uint16_t*>(
            ctx.rsp + kSpiritDeltaSelectorStackOffset);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void MarkPendingExplicitSpiritDelta(const uintptr_t entry,
                                    const uintptr_t alternate_entry,
                                    const uintptr_t context,
                                    const int64_t adjusted_delta) {
    const DWORD now = GetTickCount();
    const DWORD thread_id = GetCurrentThreadId();

    AcquireSRWLockExclusive(&g_pending_explicit_spirit_delta_lock);
    PendingExplicitSpiritDelta* selected = nullptr;
    PendingExplicitSpiritDelta* oldest = &g_pending_explicit_spirit_deltas.front();
    for (auto& pending : g_pending_explicit_spirit_deltas) {
        if (pending.active && pending.thread_id == thread_id) {
            selected = &pending;
            break;
        }
        if (!pending.active || now - pending.tick > kExplicitSpiritCommitWindowMs) {
            selected = &pending;
            break;
        }
        if (pending.tick < oldest->tick) {
            oldest = &pending;
        }
    }
    if (selected == nullptr) {
        selected = oldest;
    }
    *selected = {
        thread_id,
        entry,
        alternate_entry,
        context,
        adjusted_delta,
        now,
        true,
    };
    ReleaseSRWLockExclusive(&g_pending_explicit_spirit_delta_lock);
}

bool ConsumePendingExplicitSpiritCommit(const uintptr_t entry,
                                        const uintptr_t context,
                                        int64_t* const adjusted_delta) {
    const DWORD now = GetTickCount();
    const DWORD thread_id = GetCurrentThreadId();
    bool matched = false;

    AcquireSRWLockExclusive(&g_pending_explicit_spirit_delta_lock);
    for (auto& pending : g_pending_explicit_spirit_deltas) {
        if (!pending.active || pending.thread_id != thread_id) {
            continue;
        }

        const DWORD elapsed_ms = now - pending.tick;
        if (elapsed_ms > kExplicitSpiritCommitWindowMs) {
            pending = {};
            break;
        }

        matched = psm::routing::ShouldSkipSharedCommitForExplicitDelta(
            true,
            entry,
            pending.entry,
            pending.alternate_entry,
            context,
            pending.context,
            elapsed_ms,
            kExplicitSpiritCommitWindowMs);
        if (matched) {
            if (adjusted_delta != nullptr) {
                *adjusted_delta = pending.adjusted_delta;
            }
            pending = {};
        }
        break;
    }
    ReleaseSRWLockExclusive(&g_pending_explicit_spirit_delta_lock);
    return matched;
}

void MarkPendingDirectStaminaDelta(const uintptr_t entry) {
    const DWORD now = GetTickCount();
    const DWORD thread_id = GetCurrentThreadId();

    AcquireSRWLockExclusive(&g_pending_direct_stamina_delta_lock);
    PendingDirectStaminaDelta* selected = nullptr;
    PendingDirectStaminaDelta* oldest = &g_pending_direct_stamina_deltas.front();
    for (auto& pending : g_pending_direct_stamina_deltas) {
        if (pending.active &&
            pending.thread_id == thread_id &&
            pending.entry == entry) {
            selected = &pending;
            break;
        }
        if (!pending.active || now - pending.tick > kDirectStaminaCommitWindowMs) {
            selected = &pending;
            break;
        }
        if (pending.tick < oldest->tick) {
            oldest = &pending;
        }
    }
    if (selected == nullptr) {
        selected = oldest;
    }
    *selected = {thread_id, entry, now, true};
    ReleaseSRWLockExclusive(&g_pending_direct_stamina_delta_lock);
}

bool ConsumePendingDirectStaminaCommit(const uintptr_t entry) {
    const DWORD now = GetTickCount();
    const DWORD thread_id = GetCurrentThreadId();
    bool matched = false;

    AcquireSRWLockExclusive(&g_pending_direct_stamina_delta_lock);
    for (auto& pending : g_pending_direct_stamina_deltas) {
        if (!pending.active) {
            continue;
        }

        const DWORD elapsed_ms = now - pending.tick;
        if (elapsed_ms > kDirectStaminaCommitWindowMs) {
            pending = {};
            continue;
        }

        if (psm::routing::ShouldSkipStatWriteRescaleAfterDirectStaminaDelta(
                true,
                pending.entry,
                entry,
                pending.thread_id,
                thread_id,
                elapsed_ms,
                kDirectStaminaCommitWindowMs)) {
            matched = true;
            pending = {};
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_pending_direct_stamina_delta_lock);
    return matched;
}

void MarkPendingSharedResourceDelta(
    const uintptr_t entry,
    const psm::routing::DiscoveredPlayerResourceRole route) {
    if (entry < kMinimumPointerAddress ||
        route == psm::routing::DiscoveredPlayerResourceRole::None) {
        return;
    }

    const DWORD now = GetTickCount();
    const DWORD thread_id = GetCurrentThreadId();

    AcquireSRWLockExclusive(&g_pending_shared_resource_delta_lock);
    PendingSharedResourceDelta* selected = nullptr;
    PendingSharedResourceDelta* oldest = &g_pending_shared_resource_deltas.front();
    for (auto& pending : g_pending_shared_resource_deltas) {
        if (pending.active &&
            pending.thread_id == thread_id &&
            pending.entry == entry) {
            selected = &pending;
            break;
        }
        if (!pending.active ||
            now - pending.tick > kSharedResourceDeltaOwnershipWindowMs) {
            selected = &pending;
            break;
        }
        if (pending.tick < oldest->tick) {
            oldest = &pending;
        }
    }
    if (selected == nullptr) {
        selected = oldest;
    }
    *selected = {thread_id, entry, route, now, true};
    ReleaseSRWLockExclusive(&g_pending_shared_resource_delta_lock);
}

bool HasRecentSharedResourceDelta(
    const uintptr_t entry,
    const psm::routing::DiscoveredPlayerResourceRole route) {
    if (!g_shared_resource_delta_hook ||
        entry < kMinimumPointerAddress ||
        route == psm::routing::DiscoveredPlayerResourceRole::None) {
        return false;
    }

    const DWORD now = GetTickCount();
    const DWORD thread_id = GetCurrentThreadId();
    bool matched = false;

    AcquireSRWLockExclusive(&g_pending_shared_resource_delta_lock);
    for (auto& pending : g_pending_shared_resource_deltas) {
        if (!pending.active) {
            continue;
        }

        const DWORD elapsed_ms = now - pending.tick;
        if (elapsed_ms > kSharedResourceDeltaOwnershipWindowMs) {
            pending = {};
            continue;
        }

        if (psm::routing::ShouldSuppressFallbackPlayerResourceScaling(
                true,
                route,
                pending.entry,
                entry,
                pending.thread_id,
                thread_id,
                elapsed_ms,
                kSharedResourceDeltaOwnershipWindowMs) &&
            pending.route == route) {
            matched = true;
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_pending_shared_resource_delta_lock);
    return matched;
}

bool IsPlayerStaminaMaxValueForHook(const int64_t max_value) {
    return max_value >= kPlayerStaminaShapeMin && max_value <= kPlayerStaminaShapeMax;
}

bool IsType20StaminaShapedForHook(const int32_t stat_type, const int64_t max_value) {
    return IsPrimarySpiritStatId(stat_type) && IsPlayerStaminaMaxValueForHook(max_value);
}

bool IsStaminaTypeOrShapeForHook(const int32_t stat_type, const int64_t max_value) {
    return IsStaminaStatId(stat_type) || IsType20StaminaShapedForHook(stat_type, max_value);
}

bool IsSpiritTypeOrShapeForHook(const int32_t stat_type, const int64_t max_value) {
    if (stat_type == kLegacySpiritId || stat_type == kLegacySpiritId2) {
        return true;
    }
    return psm::routing::IsCombatSpiritResourceShape(
        stat_type,
        max_value,
        GetResolvedSpiritStatId(),
        kPlayerSpiritShapeMin,
        kPlayerSpiritShapeMax,
        GetResolvedAlternateSpiritStatId());
}

bool HookOwnerMatchesTrackedPlayer(const uintptr_t owner) {
    const ActorResolveSnapshot player = g_player_resolve;
    return psm::routing::MatchesPlayerDamageIdentity(owner,
                                                     player.actor,
                                                     player.marker,
                                                     player.root,
                                                     player.damage_target,
                                                     player.damage_source);
}

bool HasAdjacentPlayerStaminaForSpiritHook(const uintptr_t spirit_entry) {
    constexpr uintptr_t delta = kSpiritEntryOffsetFromHealth - kStaminaEntryOffsetFromHealth;
    if (spirit_entry < delta) {
        return false;
    }

    int32_t stamina_type = -1;
    int64_t stamina_current = 0;
    int64_t stamina_max = 0;
    return TryReadHookStatEntry(spirit_entry - delta, &stamina_type, &stamina_current, &stamina_max) &&
           IsStaminaTypeOrShapeForHook(stamina_type, stamina_max);
}

bool HasAdjacentPlayerSpiritForStaminaHook(const uintptr_t stamina_entry) {
    constexpr uintptr_t delta = kSpiritEntryOffsetFromHealth - kStaminaEntryOffsetFromHealth;
    int32_t spirit_type = -1;
    int64_t spirit_current = 0;
    int64_t spirit_max = 0;
    return TryReadHookStatEntry(stamina_entry + delta, &spirit_type, &spirit_current, &spirit_max) &&
           psm::routing::IsCombatSpiritResourceShape(
               spirit_type,
               spirit_max,
               GetResolvedSpiritStatId(),
               kPlayerSpiritShapeMin,
               kPlayerSpiritShapeMax,
               GetResolvedAlternateSpiritStatId());
}

void LogSpiritSizedValuesNearPointer(const char* const name,
                                     const uintptr_t base,
                                     const uintptr_t spirit_entry,
                                     const int64_t spirit_current,
                                     const int64_t spirit_max,
                                     std::uint32_t* const emitted) {
    if (name == nullptr || emitted == nullptr || *emitted >= 96 || base < kMinimumPointerAddress) {
        return;
    }

    for (uintptr_t offset = 0; offset <= 0x1800 && *emitted < 160; offset += 4) {
        const uintptr_t address = base + offset;
        __try {
            const int32_t dword_value = *reinterpret_cast<const int32_t*>(address);
            const int64_t qword_value = *reinterpret_cast<const int64_t*>(address);
            uintptr_t ptr_value = 0;
            if ((offset % sizeof(uintptr_t)) == 0) {
                ptr_value = *reinterpret_cast<const uintptr_t*>(address);
            }

            const bool dword_match =
                dword_value == 170 ||
                dword_value == 100 ||
                dword_value == 170000 ||
                dword_value == 100000 ||
                dword_value == static_cast<int32_t>(spirit_current) ||
                dword_value == static_cast<int32_t>(spirit_max);
            const bool qword_match =
                qword_value == spirit_current ||
                qword_value == spirit_max ||
                qword_value == 170 ||
                qword_value == 100 ||
                qword_value == 170000 ||
                qword_value == 100000;
            const bool ptr_match = ptr_value == spirit_entry;

            if (!dword_match && !qword_match && !ptr_match) {
                continue;
            }

            Log("hooks: spirit register scan base=%s ptr=0x%p offset=0x%llX addr=0x%p dword=%d qword=%lld ptr_value=0x%p match=%s%s%s",
                name,
                reinterpret_cast<void*>(base),
                static_cast<unsigned long long>(offset),
                reinterpret_cast<void*>(address),
                dword_value,
                static_cast<long long>(qword_value),
                reinterpret_cast<void*>(ptr_value),
                dword_match ? "dword" : "",
                qword_match ? "|qword" : "",
                ptr_match ? "|ptr" : "");
            ++(*emitted);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
}

void LogSpiritDeltaRegisterContext(const SafetyHookContext& ctx,
                                   const uintptr_t entry,
                                   const int64_t original_delta) {
    if (original_delta >= 0) {
        return;
    }

    const auto scan_index = g_spirit_register_scan_logs.fetch_add(1, std::memory_order_acq_rel);
    if (scan_index >= 4) {
        return;
    }

    int64_t spirit_current = 0;
    int64_t spirit_max = 0;
    int32_t spirit_type = 0;
    TryReadHookStatEntry(entry, &spirit_type, &spirit_current, &spirit_max);

    Log("hooks: spirit-delta regs entry=0x%p delta=%lld type=%d current=%lld max=%lld rax=0x%p rbx=0x%p rcx=0x%p rdx=0x%p rsi=0x%p rdi=0x%p r8=0x%p r9=0x%p r10=0x%p r11=0x%p r12=0x%p r13=0x%p r14=0x%p r15=0x%p rip=0x%p",
        reinterpret_cast<void*>(entry),
        static_cast<long long>(original_delta),
        spirit_type,
        static_cast<long long>(spirit_current),
        static_cast<long long>(spirit_max),
        reinterpret_cast<void*>(ctx.rax),
        reinterpret_cast<void*>(ctx.rbx),
        reinterpret_cast<void*>(ctx.rcx),
        reinterpret_cast<void*>(ctx.rdx),
        reinterpret_cast<void*>(ctx.rsi),
        reinterpret_cast<void*>(ctx.rdi),
        reinterpret_cast<void*>(ctx.r8),
        reinterpret_cast<void*>(ctx.r9),
        reinterpret_cast<void*>(ctx.r10),
        reinterpret_cast<void*>(ctx.r11),
        reinterpret_cast<void*>(ctx.r12),
        reinterpret_cast<void*>(ctx.r13),
        reinterpret_cast<void*>(ctx.r14),
        reinterpret_cast<void*>(ctx.r15),
        reinterpret_cast<void*>(ctx.rip));

    std::uint32_t emitted = 0;
    LogSpiritSizedValuesNearPointer("rdx", ctx.rdx, entry, spirit_current, spirit_max, &emitted);
    LogSpiritSizedValuesNearPointer("rsi", ctx.rsi, entry, spirit_current, spirit_max, &emitted);
    LogSpiritSizedValuesNearPointer("rdi", ctx.rdi, entry, spirit_current, spirit_max, &emitted);
    LogSpiritSizedValuesNearPointer("r14", ctx.r14, entry, spirit_current, spirit_max, &emitted);
    LogSpiritSizedValuesNearPointer("r15", ctx.r15, entry, spirit_current, spirit_max, &emitted);
    LogSpiritSizedValuesNearPointer("rbx", ctx.rbx, entry, spirit_current, spirit_max, &emitted);
    LogSpiritSizedValuesNearPointer("r13", ctx.r13, entry, spirit_current, spirit_max, &emitted);
    LogSpiritSizedValuesNearPointer("tracked-player-actor", GetTrackedPlayerActor(), entry, spirit_current, spirit_max, &emitted);
    LogSpiritSizedValuesNearPointer("tracked-player-root", GetTrackedPlayerStatRoot(), entry, spirit_current, spirit_max, &emitted);
    LogSpiritSizedValuesNearPointer("tracked-player-marker", GetTrackedPlayerStatusMarker(), entry, spirit_current, spirit_max, &emitted);
    LogSpiritSizedValuesNearPointer("tracked-player-health", GetTrackedPlayerHealthEntry(), entry, spirit_current, spirit_max, &emitted);
    Log("hooks: spirit register scan complete entry=0x%p emitted=%u", reinterpret_cast<void*>(entry), emitted);
}

void LogSpecialMountHealthWriteCandidate(const SafetyHookContext& ctx) {
    int32_t stat_type = 0;
    int64_t current_value = 0;
    int64_t max_value = 0;
    if (!TryReadHookStatEntry(ctx.rdi, &stat_type, &current_value, &max_value) ||
        stat_type != kHealthId ||
        max_value < 10000 ||
        max_value > 750000) {
        return;
    }

    const int64_t requested_value = static_cast<int64_t>(ctx.rbx);
    if (requested_value >= current_value || requested_value < 0) {
        return;
    }

    const uintptr_t tracked_player_health = GetTrackedPlayerHealthEntry();
    const uintptr_t tracked_mount_health = g_mount_resolve.health_entry;
    if (ctx.rdi == tracked_player_health || ctx.rdi == tracked_mount_health) {
        return;
    }

    const auto current = g_special_mount_health_write_samples.fetch_add(1, std::memory_order_acq_rel);
    if (current >= 64) {
        return;
    }

    Log("hooks: special-mount health candidate entry=0x%p old=%lld requested=%lld max=%lld tracked_mount_health=0x%p tracked_mount_stamina=0x%p regs rcx=0x%p rdx=0x%p r8=0x%p r9=0x%p rsi=0x%p r14=0x%p rip=0x%p",
        reinterpret_cast<void*>(ctx.rdi),
        static_cast<long long>(current_value),
        static_cast<long long>(requested_value),
        static_cast<long long>(max_value),
        reinterpret_cast<void*>(tracked_mount_health),
        reinterpret_cast<void*>(g_mount_resolve.stamina_entry),
        reinterpret_cast<void*>(ctx.rcx),
        reinterpret_cast<void*>(ctx.rdx),
        reinterpret_cast<void*>(ctx.r8),
        reinterpret_cast<void*>(ctx.r9),
        reinterpret_cast<void*>(ctx.rsi),
        reinterpret_cast<void*>(ctx.r14),
        reinterpret_cast<void*>(ctx.rip));
}

bool TryRedirectPlayerStaminaCallToSpirit(SafetyHookContext& ctx,
                                          const uintptr_t stamina_entry,
                                          const int64_t original_delta) {
    static_cast<void>(ctx);
    static_cast<void>(stamina_entry);
    static_cast<void>(original_delta);
    return false;
}

uintptr_t ReadStackCaller(const uintptr_t rsp) {
    if (rsp < kMinimumPointerAddress) {
        return 0;
    }

    __try {
        return *reinterpret_cast<const uintptr_t*>(rsp);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}


void LogResourceWriteCandidate(const SafetyHookContext& ctx,
                               const uintptr_t tracked_player_health,
                               const uintptr_t tracked_player_stamina,
                               const uintptr_t tracked_player_spirit,
                               const bool player_stat_context) {
    if (player_stat_context) {
        return;
    }

    int32_t stat_type = 0;
    int64_t current_value = 0;
    int64_t max_value = 0;
    if (!TryReadHookStatEntry(ctx.rdi, &stat_type, &current_value, &max_value) ||
        (!IsStaminaStatId(stat_type) && !IsSpiritStatId(stat_type))) {
        return;
    }

    const int64_t requested_value = static_cast<int64_t>(ctx.rbx);
    const bool player_sized_resource =
        (IsStaminaStatId(stat_type) && max_value >= 250000 && max_value <= 600000) ||
        (IsSpiritStatId(stat_type) && max_value >= 100000 && max_value <= 220000);
    if (!player_sized_resource && requested_value == current_value) {
        return;
    }

    const auto current = g_resource_write_candidate_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current >= 96) {
        return;
    }

    const int64_t delta = requested_value - current_value;
    Log("hooks: resource-write candidate entry=0x%p type=%d current=%lld requested=%lld delta=%lld max=%lld tracked_health=0x%p tracked_stamina=0x%p tracked_spirit=0x%p mount_health=0x%p mount_stamina=0x%p mount_spirit=0x%p regs rcx=0x%p rdx=0x%p rsi=0x%p r8=0x%p r9=0x%p r14=0x%p rip=0x%p",
        reinterpret_cast<void*>(ctx.rdi),
        stat_type,
        static_cast<long long>(current_value),
        static_cast<long long>(requested_value),
        static_cast<long long>(delta),
        static_cast<long long>(max_value),
        reinterpret_cast<void*>(tracked_player_health),
        reinterpret_cast<void*>(tracked_player_stamina),
        reinterpret_cast<void*>(tracked_player_spirit),
        reinterpret_cast<void*>(g_mount_resolve.health_entry),
        reinterpret_cast<void*>(g_mount_resolve.stamina_entry),
        reinterpret_cast<void*>(g_mount_resolve.spirit_entry),
        reinterpret_cast<void*>(ctx.rcx),
        reinterpret_cast<void*>(ctx.rdx),
        reinterpret_cast<void*>(ctx.rsi),
        reinterpret_cast<void*>(ctx.r8),
        reinterpret_cast<void*>(ctx.r9),
        reinterpret_cast<void*>(ctx.r14),
        reinterpret_cast<void*>(ctx.rip));
}
void PlayerPointerCallback(SafetyHookContext& ctx) {
    // ALWAYS log first few calls to verify hook is working
    static std::atomic<int> call_count{0};
    const int count = call_count.fetch_add(1, std::memory_order_relaxed);
    const PlayerPointerCaptureKind capture_kind =
        g_player_pointer_capture_kind.load(std::memory_order_acquire);
    const bool authoritative_capture =
        IsAuthoritativePlayerPointerCapture(capture_kind);

    uintptr_t actor = ctx.rax;
    uintptr_t status_marker = capture_kind == PlayerPointerCaptureKind::LegacyMarkerRdx
        ? ctx.rdx
        : ctx.rsi;

    if (authoritative_capture) {
        psm::shiptrace::NoteAuthoritativePlayerCapture();
        const uintptr_t capture_base = ctx.rax;
        uintptr_t actor_container = 0;
        if (capture_base < kMinimumPointerAddress ||
            !TryReadPointer(capture_base + 0x20, &status_marker) ||
            !TryReadPointer(capture_base + 0x40, &actor_container) ||
            actor_container < kMinimumPointerAddress ||
            !TryReadPointer(actor_container + 0x08, &actor)) {
            if (ShouldLogSample(g_player_pointer_samples, 8) || count < 10) {
                Log("hooks: authoritative player capture unreadable base=0x%p component=0x%p container=0x%p actor=0x%p",
                    reinterpret_cast<void*>(capture_base),
                    reinterpret_cast<void*>(status_marker),
                    reinterpret_cast<void*>(actor_container),
                    reinterpret_cast<void*>(actor));
            }
            return;
        }
    }

    const char* const capture_source = authoritative_capture
        ? "capture-base-rax"
        : (capture_kind == PlayerPointerCaptureKind::LegacyMarkerRdx ? "rdx" : "rsi");

    if (count < 10) {
        Log("hooks: player-pointer callback TRIGGERED #%d source=%s rax=0x%p rsi=0x%p rdx=0x%p marker=0x%p",
            count,
            capture_source,
            reinterpret_cast<void*>(ctx.rax),
            reinterpret_cast<void*>(ctx.rsi),
            reinterpret_cast<void*>(ctx.rdx),
            reinterpret_cast<void*>(status_marker));
    }

    if (actor < kMinimumPointerAddress || status_marker < kMinimumPointerAddress) {
        if (ShouldLogSample(g_player_pointer_samples, 8) || count < 10) {
            Log("hooks: player-pointer skipped, actor/marker below threshold source=%s rax=0x%p rsi=0x%p rdx=0x%p marker=0x%p",
                capture_source,
                reinterpret_cast<void*>(ctx.rax),
                reinterpret_cast<void*>(ctx.rsi),
                reinterpret_cast<void*>(ctx.rdx),
                reinterpret_cast<void*>(status_marker));
        }
        return;
    }

    const uintptr_t tracked_actor = GetTrackedPlayerActor();
    const uintptr_t tracked_marker = GetTrackedPlayerStatusMarker();
    const uintptr_t last_logged_actor = g_last_player_pointer_actor.load(std::memory_order_acquire);
    const uintptr_t last_logged_marker = g_last_player_pointer_marker.load(std::memory_order_acquire);
    if ((tracked_actor != actor || tracked_marker != status_marker) &&
        (last_logged_actor != actor || last_logged_marker != status_marker) &&
        ShouldLogSample(g_player_pointer_change_logs, 8)) {
        g_last_player_pointer_actor.store(actor, std::memory_order_release);
        g_last_player_pointer_marker.store(status_marker, std::memory_order_release);
        Log("hooks: player-pointer callback actor=0x%p marker=0x%p source=%s rax=0x%p rsi=0x%p rdx=0x%p",
            reinterpret_cast<void*>(actor),
            reinterpret_cast<void*>(status_marker),
            capture_source,
            reinterpret_cast<void*>(ctx.rax),
            reinterpret_cast<void*>(ctx.rsi),
            reinterpret_cast<void*>(ctx.rdx));
    }

    UpdateTrackedPlayerStatusComponent(actor, status_marker, authoritative_capture);
    if (authoritative_capture) psm::shiptrace::ObserveAuthoritativePlayer(actor, status_marker);
}

void StatsCallback(SafetyHookContext& ctx) {
    const uintptr_t entry = ctx.rax;
    const uintptr_t component = ctx.rsi;
    const uintptr_t rip = ctx.rip;
    const bool player_ready = GetTrackedPlayerStatusMarker() >= kMinimumPointerAddress;
    const uintptr_t last_entry = g_last_stats_entry.load(std::memory_order_acquire);
    const uintptr_t last_component = g_last_stats_component.load(std::memory_order_acquire);
    const uintptr_t last_rip = g_last_stats_rip.load(std::memory_order_acquire);
    if (player_ready &&
        (last_entry != entry || last_component != component || last_rip != rip) &&
        ShouldLogSample(g_stats_samples, 24)) {
        g_last_stats_entry.store(entry, std::memory_order_release);
        g_last_stats_component.store(component, std::memory_order_release);
        g_last_stats_rip.store(rip, std::memory_order_release);
        Log("hooks: stats callback entry=0x%p component=0x%p rip=0x%p",
            reinterpret_cast<void*>(entry),
            reinterpret_cast<void*>(component),
            reinterpret_cast<void*>(rip));
    }

    if (entry < kMinimumPointerAddress || component < kMinimumPointerAddress) {
        return;
    }

    __try {
        ObserveStatEntry(entry, component);
        ObservePlayerOxygenEntry(component, entry);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (!g_reported_stats_exception.exchange(true, std::memory_order_acq_rel)) {
            Log("hooks: exception 0x%08lX inside stats hook, disabling runtime processing", GetExceptionCode());
        }

        DisableRuntimeProcessing();
    }
}

void SharedResourceDeltaCallback(SafetyHookContext& ctx) {
    const uintptr_t entry = ctx.rcx;
    const int64_t original_delta = static_cast<int64_t>(ctx.r8);
    if (entry < kMinimumPointerAddress || original_delta == 0) {
        return;
    }

    __try {
        const uintptr_t tracked_stamina = GetTrackedPlayerStaminaEntry();
        const uintptr_t tracked_spirit = GetTrackedPlayerSpiritEntry();
        const auto route = psm::routing::SelectAuthoritativeSharedDeltaRoute(
            entry,
            tracked_stamina,
            tracked_spirit);
        if (route == psm::routing::DiscoveredPlayerResourceRole::None) {
            return;
        }

        if (psm::routing::ShouldDeferUpstreamResourceMutationToCommit(
                static_cast<bool>(g_spirit_value_write_hook),
                route)) {
            const auto sample =
                g_shared_resource_delta_samples.fetch_add(1, std::memory_order_acq_rel);
            if (sample < 96) {
                Log("hooks: shared-resource-delta observed route=%d entry=0x%p token=0x%p delta=%lld note=authoritative-commit-owns-mutation",
                    static_cast<int>(route),
                    reinterpret_cast<void*>(entry),
                    reinterpret_cast<void*>(ctx.rdx),
                    static_cast<long long>(original_delta));
            }
            return;
        }

        int64_t adjusted_delta = original_delta;
        const bool adjusted = TryAdjustAuthoritativePlayerResourceDelta(
            route == psm::routing::DiscoveredPlayerResourceRole::Stamina
                ? AuthoritativeResourceKind::Stamina
                : AuthoritativeResourceKind::Spirit,
            entry,
            &adjusted_delta);

        // The shared function owns this complete update even when the configured
        // multiplier produces no numerical change. Its synchronous commit and
        // generic stat-write callbacks must not rescale the same event.
        MarkPendingSharedResourceDelta(entry, route);
        if (adjusted) {
            ctx.r8 = static_cast<uintptr_t>(adjusted_delta);
        }

        const auto sample =
            g_shared_resource_delta_samples.fetch_add(1, std::memory_order_acq_rel);
        if (sample < 96) {
            const auto& config = GetConfig();
            const char* const name =
                route == psm::routing::DiscoveredPlayerResourceRole::Stamina
                    ? "stamina"
                    : "spirit";
            const double consumption =
                route == psm::routing::DiscoveredPlayerResourceRole::Stamina
                    ? config.stamina.consumption_multiplier
                    : config.spirit.consumption_multiplier;
            const double heal =
                route == psm::routing::DiscoveredPlayerResourceRole::Stamina
                    ? config.stamina.heal_multiplier
                    : config.spirit.heal_multiplier;
            Log("hooks: shared-resource-delta route=%s entry=0x%p token=0x%p old-delta=%lld final-delta=%lld adjusted=%d consumption=%.3f heal=%.3f",
                name,
                reinterpret_cast<void*>(entry),
                reinterpret_cast<void*>(ctx.rdx),
                static_cast<long long>(original_delta),
                static_cast<long long>(adjusted_delta),
                adjusted ? 1 : 0,
                consumption,
                heal);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (!g_reported_shared_resource_delta_exception.exchange(
                true,
                std::memory_order_acq_rel)) {
            Log("hooks: exception 0x%08lX inside shared-resource-delta hook, disabling runtime processing",
                GetExceptionCode());
        }
        DisableRuntimeProcessing();
    }
}

// This contract belongs only to the exact validated 3446 writer profile.
// The game clamps R8 into RBX before our hook. Any later configured adjustment
// must retain the game's floor and base+bonus ceiling before its state/packet
// consumers run. Bounds failure retains the original native-clamped register.
struct NativeWriterBounds {
    int64_t base = 0, bonus = 0, minimum = 0, upper = 0;
    bool readable = false, valid = false;
};

template<class T> bool ReadStatWriteMemory(const uintptr_t address, T* value) {
    if (!value || address < 0x10000 || sizeof(T) > UINTPTR_MAX - address) return false;
    __try { *value = *reinterpret_cast<const T*>(address); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

template<class T> bool ReadStatWriteField(const uintptr_t entry, const size_t offset, T* value) {
    return entry >= kMinimumPointerAddress && offset <= UINTPTR_MAX - entry &&
        ReadStatWriteMemory(entry + offset, value);
}

NativeWriterBounds ReadNativeWriterBounds(const uintptr_t entry, const int64_t native_value) {
    NativeWriterBounds bounds{};
    bounds.readable = ReadStatWriteField(entry, 0x18, &bounds.base) &&
        ReadStatWriteField(entry, 0x20, &bounds.bonus) &&
        ReadStatWriteField(entry, 0x28, &bounds.minimum);
    if (!bounds.readable ||
        (bounds.bonus > 0 && bounds.base > (std::numeric_limits<int64_t>::max)() - bounds.bonus) ||
        (bounds.bonus < 0 && bounds.base < (std::numeric_limits<int64_t>::min)() - bounds.bonus)) return bounds;
    bounds.upper = bounds.base + bounds.bonus;
    bounds.valid = bounds.minimum <= bounds.upper && native_value >= bounds.minimum &&
        native_value <= bounds.upper;
    return bounds;
}

std::atomic<unsigned> g_native_bound_diagnostic_samples{0};
void LogNativeWriterBounds(const SafetyHookContext& ctx, const int32_t type,
                          const int64_t native_value, const int64_t requested_adjustment,
                          const int64_t final_value, const NativeWriterBounds& bounds) {
    if (bounds.valid && requested_adjustment > 0 && requested_adjustment > bounds.minimum &&
        requested_adjustment == final_value) return;
    const auto sample = g_native_bound_diagnostic_samples.fetch_add(1, std::memory_order_relaxed);
    if (sample >= 48) return;
    int64_t old = 0, threshold = 0, recovery = 0, sentinel = 0;
    uint64_t revision = 0; uint16_t carry = 0; uint8_t state = 0, frozen = 0;
    const bool extra_readable = ReadStatWriteField(ctx.rdi, 8, &old) &&
        ReadStatWriteField(ctx.rdi, 0x30, &threshold) && ReadStatWriteField(ctx.rdi, 0x40, &recovery) &&
        ReadStatWriteField(ctx.rdi, 0x48, &revision) && ReadStatWriteField(ctx.rdi, 0x50, &carry) &&
        ReadStatWriteField(ctx.rdi, 0x52, &state) && ReadStatWriteField(ctx.rdi, 0x53, &frozen);
    uintptr_t caller = 0;
    const bool caller_readable = ctx.rsp <= UINTPTR_MAX - 0x68 &&
        ReadStatWriteMemory(ctx.rsp + 0x68, &caller);
    const auto module = reinterpret_cast<uintptr_t>(GetModuleHandleW(L"CrimsonDesert.exe"));
    const bool sentinel_readable = true; // 3663 native terminal zero is validated by WriterFields.
    const auto caller_rva = module && caller >= module && caller - module < 0x144E9000
        ? caller - module : 0;
    Log("hooks: native-stat-bound sample=%u entry=0x%p type=%d old=%lld native-request=%lld r8=%lld adjusted=%lld final=%lld minimum=%lld base=%lld bonus=%lld upper=%lld bounds-readable=%d bounds-valid=%d threshold=%lld recovery=%lld revision=%llu carry=%u state52=%u freeze53=%u extra-readable=%d sentinel=%lld sentinel-readable=%d caller=0x%p caller-rva=0x%llX caller-readable=%d",
        sample, reinterpret_cast<void*>(ctx.rdi), type, static_cast<long long>(old),
        static_cast<long long>(native_value), static_cast<long long>(static_cast<int64_t>(ctx.r8)),
        static_cast<long long>(requested_adjustment), static_cast<long long>(final_value),
        static_cast<long long>(bounds.minimum), static_cast<long long>(bounds.base),
        static_cast<long long>(bounds.bonus), static_cast<long long>(bounds.upper),
        bounds.readable ? 1 : 0, bounds.valid ? 1 : 0, static_cast<long long>(threshold),
        static_cast<long long>(recovery), static_cast<unsigned long long>(revision),
        static_cast<unsigned>(carry), static_cast<unsigned>(state), static_cast<unsigned>(frozen),
        extra_readable ? 1 : 0, static_cast<long long>(sentinel), sentinel_readable ? 1 : 0,
        reinterpret_cast<void*>(caller), static_cast<unsigned long long>(caller_rva), caller_readable ? 1 : 0);
}


void StatWriteCallback(SafetyHookContext& ctx) {
    if (ctx.rdi < kMinimumPointerAddress) {
        return;
    }

    __try {
        const uintptr_t tracked_player_health = GetTrackedPlayerHealthEntry();
        const uintptr_t tracked_player_stamina = GetTrackedPlayerStaminaEntry();
        const uintptr_t tracked_player_spirit = GetTrackedPlayerSpiritEntry();
        const bool exact_tracked_player_entry =
            (tracked_player_health >= kMinimumPointerAddress && ctx.rdi == tracked_player_health) ||
            (tracked_player_stamina >= kMinimumPointerAddress && ctx.rdi == tracked_player_stamina) ||
            (tracked_player_spirit >= kMinimumPointerAddress && ctx.rdi == tracked_player_spirit);
        const bool exact_tracked_player_stamina =
            tracked_player_stamina >= kMinimumPointerAddress && ctx.rdi == tracked_player_stamina;
        const auto exact_resource_route =
            psm::routing::SelectAuthoritativeSharedDeltaRoute(
                ctx.rdi,
                tracked_player_stamina,
                tracked_player_spirit);
        if (IsAuthoritativePlayerResourceDeltaActive() &&
            exact_resource_route != psm::routing::DiscoveredPlayerResourceRole::None) {
            return; // Native evaluator and explicit cost already own this result.
        }
        if (HasRecentSharedResourceDelta(ctx.rdi, exact_resource_route)) {
            if (ShouldLogSample(g_stat_write_samples, 24)) {
                Log("hooks: stat-write yielded to shared-resource-delta entry=0x%p route=%d",
                    reinterpret_cast<void*>(ctx.rdi),
                    static_cast<int>(exact_resource_route));
            }
            return;
        }
        const uintptr_t shared_commit_entry =
            g_last_resource_commit_entry.load(std::memory_order_acquire);
        const DWORD shared_commit_tick =
            g_last_resource_commit_tick.load(std::memory_order_acquire);
        const DWORD shared_commit_elapsed_ms = shared_commit_tick == 0
            ? 0xFFFFFFFFu
            : GetTickCount() - shared_commit_tick;
        if (psm::routing::ShouldSkipConditionalResourceWriteAfterCommit(
                static_cast<bool>(g_spirit_value_write_hook),
                exact_resource_route !=
                    psm::routing::DiscoveredPlayerResourceRole::None,
                shared_commit_entry == ctx.rdi,
                shared_commit_elapsed_ms,
                250)) {
            if (ShouldLogSample(g_stat_write_samples, 24)) {
                Log("hooks: stat-write deferred tracked resource to authoritative commit entry=0x%p route=%d elapsed_ms=%lu",
                    reinterpret_cast<void*>(ctx.rdi),
                    static_cast<int>(exact_resource_route),
                    static_cast<unsigned long>(shared_commit_elapsed_ms));
            }
            return;
        }
        const int64_t observed_value = SelectStatWriteObservedValue(
            g_stat_write_value_register,
            static_cast<int64_t>(ctx.rbx),
            static_cast<int64_t>(ctx.r8),
            static_cast<int64_t>(ctx.rsi));
        uintptr_t ship_trace_caller_raw = 0;
        if (ctx.rsp <= UINTPTR_MAX - 0x68) ReadStatWriteMemory(ctx.rsp + 0x68, &ship_trace_caller_raw);
        psm::shiptrace::ObserveWriter(ctx.rdi, observed_value, ship_trace_caller_raw);
        int32_t trace_type = -1;
        int64_t trace_current = 0;
        int64_t trace_maximum = 0;
        int64_t trace_delta = 0;
        const bool trace_read_ok = TryReadHookStatEntry(
                ctx.rdi,
                &trace_type,
                &trace_current,
                &trace_maximum);
        if (trace_read_ok) {
            trace_delta = observed_value - trace_current;
            if (psm::routing::ShouldTraceResourceAuthorityCandidate(
                    trace_type,
                    trace_maximum,
                    trace_delta)) {
                const auto trace_index =
                    g_resource_authority_trace_logs.fetch_add(1, std::memory_order_acq_rel);
                if (trace_index < 256) {
                    Log("hooks: resource-authority trace path=stat-write entry=0x%p caller-residue=0x%p caller=0x%p type=%d current=%lld requested=%lld delta=%lld max=%lld tracked=%d rip=0x%p",
                        reinterpret_cast<void*>(ctx.rdi),
                        reinterpret_cast<void*>(ctx.r14),
                        reinterpret_cast<void*>(ReadStackCaller(ctx.rsp)),
                        trace_type,
                        static_cast<long long>(trace_current),
                        static_cast<long long>(observed_value),
                        static_cast<long long>(trace_delta),
                        static_cast<long long>(trace_maximum),
                        exact_tracked_player_entry ? 1 : 0,
                        reinterpret_cast<void*>(ctx.rip));
                }
            }
        }

        const bool correlated_direct_stamina_commit =
            exact_tracked_player_stamina &&
            ConsumePendingDirectStaminaCommit(ctx.rdi);
        const bool log_sample = exact_tracked_player_entry && ShouldLogSample(g_stat_write_samples, 24);
        if (log_sample) {
            Log("hooks: stat-write callback entry=0x%p value=%lld source=%s rbx=%lld rsi=%lld r8=%lld tracked_player_health=0x%p tracked_player_stamina=0x%p tracked_player_spirit=0x%p matched=%d rip=0x%p",
                reinterpret_cast<void*>(ctx.rdi),
                static_cast<long long>(observed_value),
                "rbx-contract",
                static_cast<long long>(ctx.rbx),
                static_cast<long long>(ctx.rsi),
                static_cast<long long>(ctx.r8),
                reinterpret_cast<void*>(tracked_player_health),
                reinterpret_cast<void*>(tracked_player_stamina),
                reinterpret_cast<void*>(tracked_player_spirit),
                exact_tracked_player_entry ? 1 : 0,
                reinterpret_cast<void*>(ctx.rip));
        }

        if (!exact_tracked_player_entry) {
            LogResourceWriteCandidate(ctx, tracked_player_health, tracked_player_stamina, tracked_player_spirit, false);
            LogSpecialMountHealthWriteCandidate(ctx);
        }

        int64_t adjusted_value = observed_value;
        if (g_stat_write_value_register == StatWriteValueRegister::Rbx &&
            g_stat_write_enforce_native_bounds.load(std::memory_order_acquire) &&
            TryPreservePlayerOxygenWrite(ctx.rdi, observed_value, &adjusted_value)) {
            ctx.rbx = static_cast<uintptr_t>(adjusted_value);
            return;
        }
        // Exact current-ride Ship health owns this entry before the source-free
        // outgoing fallback. Native equal/loss requests stay native. Positive
        // native healing uses the separate ShipHealing setting once at RBX,
        // before native threshold and terminal notifications.
        if (!exact_tracked_player_entry &&
            g_stat_write_enforce_native_bounds.load(std::memory_order_acquire) &&
            g_runtime_enabled.load(std::memory_order_acquire)) {
            const auto ship_config = GetConfig();
            if (ship_config.general.enabled &&
                (psm::shiptrace::TryAdjustOccupiedShipHealthWrite(
                    ctx.rdi, observed_value,
                    ship_config.ship_healing.enabled ? ship_config.ship_healing.multiplier : 1.0,
                    &adjusted_value) ||
                 psm::shiptrace::IsObservedOccupiedShipHealthEntry(ctx.rdi))) {
                ctx.rbx = static_cast<uintptr_t>(adjusted_value);
                return;
            }
        }
        if (correlated_direct_stamina_commit) {
            RecordAuthoritativeResourceAdjustment(
                AuthoritativeResourceKind::Stamina,
                ctx.rdi,
                observed_value);
            const auto sample =
                g_stamina_route_authority_logs.fetch_add(1, std::memory_order_acq_rel);
            if (sample < 24) {
                Log("hooks: stat-write accepted direct stamina commit without rescale entry=0x%p current=%lld final=%lld",
                    reinterpret_cast<void*>(ctx.rdi),
                    static_cast<long long>(trace_current),
                    static_cast<long long>(observed_value));
            }
            return;
        }

        // 1.0.0.2330 disassembly confirms R14 is untouched caller residue at
        // this site. Exact resource-entry identity owns this write.
        const bool enforce_native_bounds = g_stat_write_enforce_native_bounds.load(std::memory_order_acquire);
        if (TryAdjustStatWrite(ctx.rdi, &adjusted_value, 0, enforce_native_bounds)) {
            if (enforce_native_bounds) {
                const auto bounds = ReadNativeWriterBounds(ctx.rdi, observed_value);
                const auto requested_adjustment = adjusted_value;
                adjusted_value = bounds.valid
                    ? (std::clamp)(adjusted_value, bounds.minimum, bounds.upper)
                    : observed_value;
                LogNativeWriterBounds(ctx, trace_type, observed_value, requested_adjustment, adjusted_value, bounds);
            }
            ctx.rbx = static_cast<uintptr_t>(adjusted_value);
            if (ctx.rdi == GetTrackedPlayerStaminaEntry()) {
                RecordAuthoritativeResourceAdjustment(
                    AuthoritativeResourceKind::Stamina,
                    ctx.rdi,
                    adjusted_value);
            } else if (ctx.rdi == GetTrackedPlayerSpiritEntry()) {
                RecordAuthoritativeResourceAdjustment(
                    AuthoritativeResourceKind::Spirit,
                    ctx.rdi,
                    adjusted_value);
            }
            if (log_sample) {
                Log("hooks: stat-write adjusted entry=0x%p final=%lld source=%s",
                    reinterpret_cast<void*>(ctx.rdi),
                    static_cast<long long>(adjusted_value),
                    "rbx-contract");
            } else if (!exact_tracked_player_entry) {
                const auto current = g_resource_write_candidate_logs.fetch_add(1, std::memory_order_acq_rel);
                if (current < 160) {
                    Log("hooks: stat-write adjusted candidate rbx entry=0x%p old=%lld final=%lld r8=%lld rsi=%lld rip=0x%p",
                        reinterpret_cast<void*>(ctx.rdi),
                        static_cast<long long>(observed_value),
                        static_cast<long long>(adjusted_value),
                        static_cast<long long>(ctx.r8),
                        static_cast<long long>(ctx.rsi),
                        reinterpret_cast<void*>(ctx.rip));
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (!g_reported_stat_write_exception.exchange(true, std::memory_order_acq_rel)) {
            Log("hooks: exception 0x%08lX inside stat-write hook, disabling runtime processing", GetExceptionCode());
        }

        DisableRuntimeProcessing();
    }
}

void SpiritValueWriteCallback(SafetyHookContext& ctx) {
    const uintptr_t entry = ctx.rcx;
    if (entry < kMinimumPointerAddress) {
        return;
    }

    const auto exact_resource_route =
        psm::routing::SelectAuthoritativeSharedDeltaRoute(
            entry,
            GetTrackedPlayerStaminaEntry(),
            GetTrackedPlayerSpiritEntry());
    if (IsAuthoritativePlayerResourceDeltaActive()) {
        // Do not rescale elapsed recovery together with a one-time cost here.
        // Health and mount policies remain on the generic stat-write path.
        return;
    }
    if (HasRecentSharedResourceDelta(entry, exact_resource_route)) {
        if (ShouldLogSample(g_spirit_value_write_samples, 48)) {
            Log("hooks: shared-resource-commit yielded to shared-resource-delta entry=0x%p route=%d",
                reinterpret_cast<void*>(entry),
                static_cast<int>(exact_resource_route));
        }
        g_last_resource_commit_entry.store(entry, std::memory_order_release);
        g_last_resource_commit_tick.store(GetTickCount(), std::memory_order_release);
        return;
    }

    int64_t pending_adjusted_delta = 0;
    if (ConsumePendingExplicitSpiritCommit(
            entry,
            ctx.rdx,
            &pending_adjusted_delta)) {
        __try {
            int32_t actual_type = -1;
            int64_t current_value = 0;
            int64_t max_value = 0;
            if (TryReadHookStatEntry(entry, &actual_type, &current_value, &max_value)) {
                const int64_t requested_value = static_cast<int64_t>(ctx.r8);
                const int64_t final_value = psm::routing::ResolveExplicitDeltaCommitValue(
                    true,
                    current_value,
                    pending_adjusted_delta,
                    requested_value,
                    max_value);
                RecordAuthoritativePlayerSpiritCommit(
                    entry,
                    current_value,
                    final_value);

                const auto sample =
                    g_explicit_resource_delta_logs.fetch_add(1, std::memory_order_acq_rel);
                if (sample < 64) {
                    Log("hooks: shared-resource-commit passthrough after explicit selector delta entry=0x%p token=0x%p current=%lld game-requested=%lld adjusted-delta=%lld observed-final=%lld",
                        reinterpret_cast<void*>(entry),
                        reinterpret_cast<void*>(ctx.rdx),
                        static_cast<long long>(current_value),
                        static_cast<long long>(requested_value),
                        static_cast<long long>(pending_adjusted_delta),
                        static_cast<long long>(final_value));
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        g_last_resource_commit_entry.store(entry, std::memory_order_release);
        g_last_resource_commit_tick.store(GetTickCount(), std::memory_order_release);
        return;
    }

    __try {
        int32_t actual_type = -1;
        int64_t current_value = 0;
        int64_t max_value = 0;
        if (!TryReadHookStatEntry(entry, &actual_type, &current_value, &max_value)) {
            return;
        }

        const int64_t requested_value = static_cast<int64_t>(ctx.r8);
        const int64_t original_delta = requested_value - current_value;
        if (original_delta == 0) {
            return;
        }

        const uintptr_t caller = ReadStackCaller(ctx.rsp);
        if (psm::routing::ShouldTraceResourceAuthorityCandidate(
                actual_type,
                max_value,
                original_delta)) {
            const auto trace_index =
                g_resource_authority_trace_logs.fetch_add(1, std::memory_order_acq_rel);
            if (trace_index < 256) {
                Log("hooks: resource-authority trace path=shared-commit entry=0x%p token=0x%p caller=0x%p type=%d current=%lld requested=%lld delta=%lld max=%lld tracked=%d rip=0x%p",
                    reinterpret_cast<void*>(entry),
                    reinterpret_cast<void*>(ctx.rdx),
                    reinterpret_cast<void*>(caller),
                    actual_type,
                    static_cast<long long>(current_value),
                    static_cast<long long>(requested_value),
                    static_cast<long long>(original_delta),
                    static_cast<long long>(max_value),
                    entry == GetTrackedPlayerSpiritEntry() ? 1 : 0,
                    reinterpret_cast<void*>(ctx.rip));
            }
        }

        const auto authoritative_route =
            psm::routing::SelectAuthoritativeSharedDeltaRoute(
                entry,
                GetTrackedPlayerStaminaEntry(),
                GetTrackedPlayerSpiritEntry());
        if (psm::routing::AuthoritativeResourceCommitOwnsTrackedRole(
                static_cast<bool>(g_spirit_value_write_hook),
                authoritative_route)) {
            const auto kind =
                authoritative_route ==
                        psm::routing::DiscoveredPlayerResourceRole::Stamina
                    ? AuthoritativeResourceKind::Stamina
                    : AuthoritativeResourceKind::Spirit;
            int64_t adjusted_delta = original_delta;
            const bool adjusted = TryAdjustAuthoritativePlayerResourceDelta(
                kind,
                entry,
                &adjusted_delta);
            const int64_t adjusted_requested =
                psm::routing::AdjustedRequestedValueFromDelta(
                    current_value,
                    adjusted_delta,
                    max_value);
            ctx.r8 = static_cast<uintptr_t>(adjusted_requested);
            g_last_resource_commit_entry.store(entry, std::memory_order_release);
            g_last_resource_commit_tick.store(GetTickCount(), std::memory_order_release);

            const auto sample =
                g_spirit_value_write_samples.fetch_add(1, std::memory_order_acq_rel);
            if (sample < 160) {
                const auto& config = GetConfig();
                const bool stamina =
                    authoritative_route ==
                    psm::routing::DiscoveredPlayerResourceRole::Stamina;
                Log("hooks: authoritative-resource-commit route=%s entry=0x%p token=0x%p current=%lld requested=%lld original-delta=%lld final=%lld final-delta=%lld adjusted=%d consumption=%.3f heal=%.3f",
                    stamina ? "stamina" : "spirit",
                    reinterpret_cast<void*>(entry),
                    reinterpret_cast<void*>(ctx.rdx),
                    static_cast<long long>(current_value),
                    static_cast<long long>(requested_value),
                    static_cast<long long>(original_delta),
                    static_cast<long long>(adjusted_requested),
                    static_cast<long long>(adjusted_delta),
                    adjusted ? 1 : 0,
                    stamina
                        ? config.stamina.consumption_multiplier
                        : config.spirit.consumption_multiplier,
                    stamina
                        ? config.stamina.heal_multiplier
                        : config.spirit.heal_multiplier);
            }
            return;
        }

        uintptr_t tracked_player_spirit = GetTrackedPlayerSpiritEntry();
        bool already_tracked_player_spirit =
            tracked_player_spirit >= kMinimumPointerAddress && entry == tracked_player_spirit;
        // RDX is the commit token persisted into entry+0x38, not an actor or
        // component owner. Route only by the accepted pair and adjacency.
        constexpr bool owner_matches_player = false;
        const bool has_adjacent_player_stamina = HasAdjacentPlayerStaminaForSpiritHook(entry);
        const bool large_tracked_player_spirit =
            IsType20StaminaShapedForHook(actual_type, max_value) &&
            entry == GetTrackedPlayerSpiritEntry() &&
            entry != GetTrackedPlayerStaminaEntry();
        const bool combat_spirit_resource =
            psm::routing::IsCombatSpiritResourceShape(
                actual_type,
                max_value,
                GetResolvedSpiritStatId(),
                kPlayerSpiritShapeMin,
                kPlayerSpiritShapeMax,
                GetResolvedAlternateSpiritStatId()) ||
            large_tracked_player_spirit;
        if (combat_spirit_resource) {
            const auto candidate =
                g_shared_commit_spirit_candidate_logs.fetch_add(1, std::memory_order_acq_rel);
            if (candidate < 64) {
                Log("hooks: shared-resource-commit candidate entry=0x%p token=0x%p caller=0x%p type=%d current=%lld requested=%lld delta=%lld max=%lld tracked=%d adjacent_stamina=%d owner_match=%d",
                    reinterpret_cast<void*>(entry),
                    reinterpret_cast<void*>(ctx.rdx),
                    reinterpret_cast<void*>(caller),
                    actual_type,
                    static_cast<long long>(current_value),
                    static_cast<long long>(requested_value),
                    static_cast<long long>(original_delta),
                    static_cast<long long>(max_value),
                    already_tracked_player_spirit ? 1 : 0,
                    has_adjacent_player_stamina ? 1 : 0,
                    owner_matches_player ? 1 : 0);
            }
        }
        if (!large_tracked_player_spirit &&
            !psm::routing::ShouldRouteResourceValueWriteToPlayerSpirit(
                actual_type,
                max_value,
                GetResolvedSpiritStatId(),
                kPlayerSpiritShapeMin,
                kPlayerSpiritShapeMax,
                has_adjacent_player_stamina,
                owner_matches_player,
                already_tracked_player_spirit,
                GetResolvedAlternateSpiritStatId())) {
            return;
        }

        if (!already_tracked_player_spirit) {
            bool promoted = false;
            if (has_adjacent_player_stamina) {
                promoted = TryPromotePlayerCombatResourcesFromSpiritEntry(
                    entry,
                    "shared-resource-commit adjacent combat pair");
            }
            static_cast<void>(promoted);
        }

        if (GetTrackedPlayerSpiritEntry() != entry) {
            const auto sample = g_spirit_value_write_samples.fetch_add(1, std::memory_order_acq_rel);
            if (sample < 32) {
                Log("hooks: shared-resource-commit rejected untracked candidate entry=0x%p token=0x%p type=%d current=%lld requested=%lld max=%lld adjacent_stamina=%d owner_match=%d",
                    reinterpret_cast<void*>(entry),
                    reinterpret_cast<void*>(ctx.rdx),
                    actual_type,
                    static_cast<long long>(current_value),
                    static_cast<long long>(requested_value),
                    static_cast<long long>(max_value),
                    has_adjacent_player_stamina ? 1 : 0,
                    owner_matches_player ? 1 : 0);
            }
            return;
        }

        int64_t adjusted_delta = original_delta;
        const bool adjusted = TryAdjustSpiritDelta(entry, &adjusted_delta);
        const auto sample = g_spirit_value_write_samples.fetch_add(1, std::memory_order_acq_rel);
        if (adjusted) {
            g_last_resource_commit_entry.store(entry, std::memory_order_release);
            g_last_resource_commit_tick.store(GetTickCount(), std::memory_order_release);
            const int64_t adjusted_requested = psm::routing::AdjustedRequestedValueFromDelta(
                current_value,
                adjusted_delta,
                max_value);
            ctx.r8 = static_cast<uintptr_t>(adjusted_requested);
            RecordAuthoritativeResourceAdjustment(
                AuthoritativeResourceKind::Spirit,
                entry,
                adjusted_requested);
            if (sample < 64) {
                const auto& config = GetConfig();
                Log("hooks: shared-resource-commit adjusted entry=0x%p token=0x%p old=%lld requested=%lld delta=%lld final=%lld final_delta=%lld max=%lld consumption=%.3f heal=%.3f",
                    reinterpret_cast<void*>(entry),
                    reinterpret_cast<void*>(ctx.rdx),
                    static_cast<long long>(current_value),
                    static_cast<long long>(requested_value),
                    static_cast<long long>(original_delta),
                    static_cast<long long>(adjusted_requested),
                    static_cast<long long>(adjusted_delta),
                    static_cast<long long>(max_value),
                    config.spirit.consumption_multiplier,
                    config.spirit.heal_multiplier);
            }
        } else if (sample < 32) {
            Log("hooks: shared-resource-commit observed no adjustment entry=0x%p token=0x%p old=%lld requested=%lld delta=%lld max=%lld",
                reinterpret_cast<void*>(entry),
                reinterpret_cast<void*>(ctx.rdx),
                static_cast<long long>(current_value),
                static_cast<long long>(requested_value),
                static_cast<long long>(original_delta),
                static_cast<long long>(max_value));
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (!g_reported_spirit_value_write_exception.exchange(true, std::memory_order_acq_rel)) {
            Log("hooks: exception 0x%08lX inside shared-resource-commit hook, disabling runtime processing",
                GetExceptionCode());
        }
        DisableRuntimeProcessing();
    }
}

void SpiritDeltaCallback(SafetyHookContext& ctx) {
    const uintptr_t entry = ctx.rcx;
    if (entry < kMinimumPointerAddress) {
        return;
    }

    __try {
        int32_t actual_type = -1;
        int64_t current_value = 0;
        int64_t max_value = 0;
        if (!TryReadHookStatEntry(entry, &actual_type, &current_value, &max_value)) {
            return;
        }

        const int64_t original_delta = static_cast<int64_t>(ctx.r8);

        std::uint16_t explicit_selector = 0;
        const bool has_explicit_selector =
            TryReadSpiritDeltaSelector(ctx, &explicit_selector);
        const bool adopted_explicit_pair =
            has_explicit_selector &&
            TryAdoptExplicitPlayerResourcePair(
                entry,
                explicit_selector,
                original_delta,
                "explicit resource selector contract");
        std::uint16_t stamina_selector =
            static_cast<std::uint16_t>(GetResolvedStaminaStatId());
        std::uint16_t spirit_selector =
            static_cast<std::uint16_t>(GetResolvedSpiritStatId());
        uintptr_t selector_calibration_base =
            psm::routing::SelectExplicitResourceSelectorCalibrationBase(
                entry,
                GetTrackedPlayerStaminaEntry());
        bool live_selectors_calibrated =
            TryResolvePlayerResourceSelectorsFromBase(
                selector_calibration_base,
                &stamina_selector,
                &spirit_selector);
        if (!live_selectors_calibrated && selector_calibration_base != entry) {
            selector_calibration_base = entry;
            live_selectors_calibrated =
                TryResolvePlayerResourceSelectorsFromBase(
                    selector_calibration_base,
                    &stamina_selector,
                    &spirit_selector);
        }
        const auto explicit_route = has_explicit_selector
            ? psm::routing::SelectExplicitResourceDeltaRoute(
                  explicit_selector,
                  stamina_selector,
                  spirit_selector)
            : psm::routing::ExplicitResourceDeltaRoute::Unknown;

        if (has_explicit_selector &&
            live_selectors_calibrated &&
            ShouldLogSample(g_explicit_resource_delta_logs, 16)) {
            Log("hooks: calibrated explicit resource selectors base=0x%p stamina=%u spirit=%u observed=%u route=%d",
                reinterpret_cast<void*>(selector_calibration_base),
                static_cast<unsigned>(stamina_selector),
                static_cast<unsigned>(spirit_selector),
                static_cast<unsigned>(explicit_selector),
                static_cast<int>(explicit_route));
        }

        if (has_explicit_selector &&
            explicit_route == psm::routing::ExplicitResourceDeltaRoute::Unknown) {
            return;
        }

        if (explicit_route == psm::routing::ExplicitResourceDeltaRoute::Stamina) {
            const auto& config = GetConfig();
            if (!g_runtime_enabled.load(std::memory_order_acquire) ||
                !config.general.enabled ||
                !config.stamina.enabled ||
                original_delta == 0) {
                return;
            }

            const uintptr_t tracked_player_stamina = GetTrackedPlayerStaminaEntry();
            const ActorResolveSnapshot mount_snapshot = g_mount_resolve;
            const bool tracked_mount_resource =
                mount_snapshot.valid() &&
                g_mount_track_confidence.load(std::memory_order_acquire) >= 3 &&
                (entry == mount_snapshot.health_entry ||
                 entry == mount_snapshot.stamina_entry ||
                 entry == mount_snapshot.spirit_entry);
            if (tracked_mount_resource ||
                tracked_player_stamina < kMinimumPointerAddress ||
                entry != tracked_player_stamina) {
                const auto sample =
                    g_explicit_resource_delta_logs.fetch_add(1, std::memory_order_acq_rel);
                if (sample < 64) {
                    Log("hooks: explicit stamina selector rejected entry=0x%p token=0x%p selector=%u adopted=%d tracked=0x%p mount=%d delta=%lld",
                        reinterpret_cast<void*>(entry),
                        reinterpret_cast<void*>(ctx.rdx),
                        static_cast<unsigned>(explicit_selector),
                        adopted_explicit_pair ? 1 : 0,
                        reinterpret_cast<void*>(tracked_player_stamina),
                        tracked_mount_resource ? 1 : 0,
                        static_cast<long long>(original_delta));
                }
                return;
            }

            if (g_stat_write_hook) {
                const auto sample =
                    g_explicit_resource_delta_logs.fetch_add(1, std::memory_order_acq_rel);
                if (sample < 64) {
                    Log("hooks: explicit stamina selector deferred to stat-write entry=0x%p token=0x%p selector=%u adopted=%d delta=%lld",
                        reinterpret_cast<void*>(entry),
                        reinterpret_cast<void*>(ctx.rdx),
                        static_cast<unsigned>(explicit_selector),
                        adopted_explicit_pair ? 1 : 0,
                        static_cast<long long>(original_delta));
                }
                return;
            }

            int64_t adjusted_delta = original_delta;
            if (TryAdjustStaminaDelta(entry, &adjusted_delta)) {
                ctx.r8 = static_cast<uintptr_t>(adjusted_delta);
                const auto sample =
                    g_explicit_resource_delta_logs.fetch_add(1, std::memory_order_acq_rel);
                if (sample < 64) {
                    Log("hooks: explicit stamina selector adjusted entry=0x%p token=0x%p selector=%u adopted=%d old-delta=%lld final-delta=%lld",
                        reinterpret_cast<void*>(entry),
                        reinterpret_cast<void*>(ctx.rdx),
                        static_cast<unsigned>(explicit_selector),
                        adopted_explicit_pair ? 1 : 0,
                        static_cast<long long>(original_delta),
                        static_cast<long long>(adjusted_delta));
                }
            }
            return;
        }

        if (explicit_route == psm::routing::ExplicitResourceDeltaRoute::Spirit) {
            const auto& config = GetConfig();
            if (!g_runtime_enabled.load(std::memory_order_acquire) ||
                !config.general.enabled ||
                !config.spirit.enabled ||
                original_delta == 0) {
                return;
            }

            uintptr_t tracked_player_stamina = GetTrackedPlayerStaminaEntry();
            uintptr_t tracked_player_spirit = GetTrackedPlayerSpiritEntry();
            bool currently_tracked_player_resource =
                entry == tracked_player_stamina;
            const ActorResolveSnapshot mount_snapshot = g_mount_resolve;
            const bool tracked_mount_resource =
                mount_snapshot.valid() &&
                g_mount_track_confidence.load(std::memory_order_acquire) >= 3 &&
                (entry == mount_snapshot.health_entry ||
                 entry == mount_snapshot.stamina_entry ||
                 entry == mount_snapshot.spirit_entry);
            const bool combat_spirit_shape =
                psm::routing::IsCombatSpiritResourceShape(
                    actual_type,
                    max_value,
                    static_cast<int32_t>(spirit_selector),
                    kPlayerSpiritShapeMin,
                    kPlayerSpiritShapeMax,
                    static_cast<int32_t>(spirit_selector) + 1);
            bool direct_tracked_spirit =
                psm::routing::ShouldUseDirectTrackedSpiritEntry(
                    explicit_route,
                    combat_spirit_shape,
                    entry == tracked_player_spirit,
                    tracked_mount_resource);
            if (!direct_tracked_spirit &&
                (!currently_tracked_player_resource || tracked_mount_resource)) {
                const auto sample =
                    g_explicit_resource_delta_logs.fetch_add(1, std::memory_order_acq_rel);
                if (sample < 64) {
                    Log("hooks: explicit spirit selector rejected entry=0x%p token=0x%p adopted=%d tracked=%d mount=%d delta=%lld",
                        reinterpret_cast<void*>(entry),
                        reinterpret_cast<void*>(ctx.rdx),
                        adopted_explicit_pair ? 1 : 0,
                        currently_tracked_player_resource ? 1 : 0,
                        tracked_mount_resource ? 1 : 0,
                        static_cast<long long>(original_delta));
                }
                return;
            }

            const double multiplier =
                psm::routing::SelectExplicitResourceDeltaMultiplier(
                    explicit_route,
                    original_delta,
                    config.stamina.consumption_multiplier,
                    config.stamina.heal_multiplier,
                    config.spirit.consumption_multiplier,
                    config.spirit.heal_multiplier);
            const int64_t adjusted_delta = original_delta < 0
                ? -ScaleDelta(-original_delta, multiplier)
                : ScaleDelta(original_delta, multiplier);

            if (direct_tracked_spirit) {
                MarkPendingExplicitSpiritDelta(entry, 0, ctx.rdx, adjusted_delta);
                ctx.r8 = static_cast<uintptr_t>(adjusted_delta);

                const auto sample =
                    g_explicit_resource_delta_logs.fetch_add(1, std::memory_order_acq_rel);
                if (sample < 128) {
                    Log("hooks: direct explicit spirit selector adjusted entry=0x%p token=0x%p selector=%u old-delta=%lld final-delta=%lld multiplier=%.3f current=%lld max=%lld",
                        reinterpret_cast<void*>(entry),
                        reinterpret_cast<void*>(ctx.rdx),
                        static_cast<unsigned>(explicit_selector),
                        static_cast<long long>(original_delta),
                        static_cast<long long>(adjusted_delta),
                        multiplier,
                        static_cast<long long>(current_value),
                        static_cast<long long>(max_value));
                }
                return;
            }

            if (GetTrackedPlayerStaminaEntry() != entry) {
                Log("hooks: explicit spirit selector lost accepted combat pair entry=0x%p token=0x%p",
                    reinterpret_cast<void*>(entry),
                    reinterpret_cast<void*>(ctx.rdx));
                return;
            }

            constexpr uintptr_t kCompactSpiritDelta =
                kSpiritEntryOffsetFromHealth - kStaminaEntryOffsetFromHealth;
            const uintptr_t compact_spirit_entry = GetTrackedPlayerSpiritEntry();
            int32_t compact_spirit_type = -1;
            int64_t compact_spirit_current = 0;
            int64_t compact_spirit_max = 0;
            if (compact_spirit_entry != entry + kCompactSpiritDelta ||
                !TryReadHookStatEntry(compact_spirit_entry,
                                      &compact_spirit_type,
                                      &compact_spirit_current,
                                      &compact_spirit_max) ||
                !psm::routing::IsCombatSpiritResourceShape(
                    compact_spirit_type,
                    compact_spirit_max,
                    GetResolvedSpiritStatId(),
                    kPlayerSpiritShapeMin,
                    kPlayerSpiritShapeMax,
                    GetResolvedAlternateSpiritStatId())) {
                Log("hooks: explicit spirit selector rejected invalid compact sibling base=0x%p compact=0x%p expected=0x%p type=%d current=%lld max=%lld",
                    reinterpret_cast<void*>(entry),
                    reinterpret_cast<void*>(compact_spirit_entry),
                    reinterpret_cast<void*>(entry + kCompactSpiritDelta),
                    compact_spirit_type,
                    static_cast<long long>(compact_spirit_current),
                    static_cast<long long>(compact_spirit_max));
                return;
            }

            MarkPendingExplicitSpiritDelta(
                entry,
                compact_spirit_entry,
                ctx.rdx,
                adjusted_delta);
            ctx.r8 = static_cast<uintptr_t>(adjusted_delta);

            const auto sample =
                g_explicit_resource_delta_logs.fetch_add(1, std::memory_order_acq_rel);
            if (sample < 128) {
                Log("hooks: explicit spirit selector adjusted base=0x%p compact=0x%p token=0x%p selector=%u old-delta=%lld final-delta=%lld multiplier=%.3f compact-current=%lld compact-max=%lld",
                    reinterpret_cast<void*>(entry),
                    reinterpret_cast<void*>(compact_spirit_entry),
                    reinterpret_cast<void*>(ctx.rdx),
                    static_cast<unsigned>(explicit_selector),
                    static_cast<long long>(original_delta),
                    static_cast<long long>(adjusted_delta),
                    multiplier,
                    static_cast<long long>(compact_spirit_current),
                    static_cast<long long>(compact_spirit_max));
            }
            return;
        }

        const uintptr_t tracked_player_stamina = GetTrackedPlayerStaminaEntry();
        const uintptr_t tracked_player_spirit_before = GetTrackedPlayerSpiritEntry();
        const bool already_tracked_player_resource =
            entry == tracked_player_stamina || entry == tracked_player_spirit_before;
        const bool owner_matches_player = HookOwnerMatchesTrackedPlayer(ctx.r15);
        const bool has_adjacent_player_pair = HasAdjacentPlayerSpiritForStaminaHook(entry);
        const bool loose_mount_stamina = psm::routing::ShouldTreatType20ResourceAsLooseMountStamina(
            actual_type,
            max_value,
            GetResolvedSpiritStatId(),
            kPlayerSpiritShapeMax,
            owner_matches_player,
            has_adjacent_player_pair,
            already_tracked_player_resource,
            kLooseMountResourceMin,
            kLooseMountResourceMax,
            kPlayerStaminaShapeMin);

        if (loose_mount_stamina) {
            UpdateTrackedMountFromStaminaContext(entry, ctx.r15);
            int64_t adjusted_delta = original_delta;
            if (TryAdjustStaminaDelta(entry, &adjusted_delta)) {
                ctx.r8 = static_cast<uintptr_t>(adjusted_delta);
                if (ShouldLogSample(g_spirit_delta_samples, 32)) {
                    Log("hooks: spirit-delta routed loose mount stamina entry=0x%p owner=0x%p old-delta=%lld final-delta=%lld max=%lld",
                        reinterpret_cast<void*>(entry),
                        reinterpret_cast<void*>(ctx.r15),
                        static_cast<long long>(original_delta),
                        static_cast<long long>(adjusted_delta),
                        static_cast<long long>(max_value));
                }
            }
            return;
        }

        const bool large_type20_resource =
            IsType20StaminaShapedForHook(actual_type, max_value);
        bool large_tracked_player_spirit =
            large_type20_resource &&
            entry != tracked_player_stamina &&
            entry == tracked_player_spirit_before;
        if (large_type20_resource &&
            !large_tracked_player_spirit &&
            entry != tracked_player_stamina &&
            original_delta < 0 &&
            TryBindObservedSpiritToConfirmedPlayer(
                entry,
                ctx.r15,
                original_delta,
                "spirit-delta large spirit mirror")) {
            large_tracked_player_spirit = entry == GetTrackedPlayerSpiritEntry();
        }

        if (large_type20_resource && !large_tracked_player_spirit) {
            if (ctx.r15 >= kMinimumPointerAddress && original_delta < 0) {
                TryActivateCorrelatedPlayerOwnerCandidate(
                    ctx.r15,
                    entry,
                    original_delta,
                    "spirit-delta active stamina owner");
            } else if (ctx.r15 >= kMinimumPointerAddress) {
                ObservePlayerStaminaOwnerCandidate(ctx.r15, entry);
            }

            const uintptr_t active_player_stamina = GetTrackedPlayerStaminaEntry();
            if (ctx.r15 >= kMinimumPointerAddress &&
                entry != active_player_stamina) {
                UpdateTrackedMountFromStaminaContext(entry, ctx.r15);
            }

            int64_t adjusted_delta = original_delta;
            if (TryAdjustStaminaDelta(entry, &adjusted_delta)) {
                ctx.r8 = static_cast<uintptr_t>(adjusted_delta);
                if (ShouldLogSample(g_spirit_delta_samples, 24)) {
                    Log("hooks: spirit-delta routed stamina-shaped entry=0x%p old-delta=%lld final-delta=%lld max=%lld",
                        reinterpret_cast<void*>(entry),
                        static_cast<long long>(original_delta),
                        static_cast<long long>(adjusted_delta),
                        static_cast<long long>(max_value));
                }
            }
            return;
        }

        const bool authoritative_primary = psm::routing::ShouldHandleAuthoritativeSpiritDelta(
            actual_type,
            max_value,
            original_delta,
            GetResolvedStaminaStatId(),
            GetResolvedSpiritStatId(),
            kPlayerStaminaShapeMin,
            kPlayerStaminaShapeMax,
            GetResolvedAlternateSpiritStatId());
        if (!authoritative_primary &&
            !large_tracked_player_spirit &&
            !IsSpiritTypeOrShapeForHook(actual_type, max_value)) {
            return;
        }

        if (g_spirit_value_write_hook &&
            psm::routing::IsCombatSpiritResourceShape(
                actual_type,
                max_value,
                GetResolvedSpiritStatId(),
                kPlayerSpiritShapeMin,
                kPlayerSpiritShapeMax,
                GetResolvedAlternateSpiritStatId())) {
            const uintptr_t value_write_entry =
                g_last_resource_commit_entry.load(std::memory_order_acquire);
            const DWORD value_write_tick =
                g_last_resource_commit_tick.load(std::memory_order_acquire);
            const DWORD elapsed_ms = value_write_tick == 0
                ? 0xFFFFFFFFu
                : GetTickCount() - value_write_tick;
            if (psm::routing::ShouldDeferSpiritDeltaToRecentValueWrite(
                    true,
                    value_write_entry == entry,
                    elapsed_ms,
                    250)) {
                return;
            }
        }

        uintptr_t tracked_player_spirit = GetTrackedPlayerSpiritEntry();
        if (original_delta < 0 &&
            TryBindObservedSpiritToConfirmedPlayer(
                entry,
                ctx.r15,
                original_delta,
                "spirit-delta active combat spirit")) {
            tracked_player_spirit = GetTrackedPlayerSpiritEntry();
        }
        if (tracked_player_spirit < kMinimumPointerAddress) {
            TryBootstrapPlayerSpiritFromEntry(entry);
            tracked_player_spirit = GetTrackedPlayerSpiritEntry();
        }
        const ActorResolveSnapshot observed_mount = g_mount_resolve;
        const bool trusted_mount_resource =
            observed_mount.valid() &&
            g_mount_track_confidence.load(std::memory_order_acquire) >= 3 &&
            (entry == observed_mount.health_entry ||
             entry == observed_mount.stamina_entry ||
             entry == observed_mount.spirit_entry ||
             ctx.r15 == observed_mount.root ||
             ctx.r15 == observed_mount.damage_target);
        const bool exact_active_player_owner =
            IsExactConfirmedTrackedPlayerOwner(ctx.r15);
        const bool observed_player_resource_shape =
            authoritative_primary ||
            large_tracked_player_spirit ||
            IsSpiritTypeOrShapeForHook(actual_type, max_value);
        if ((tracked_player_spirit < kMinimumPointerAddress || entry != tracked_player_spirit) &&
            ctx.r15 >= kMinimumPointerAddress &&
            psm::routing::ShouldPromoteObservedPlayerResourceOwner(
                !HasTrackedPlayerDamageOwner(),
                exact_active_player_owner,
                observed_player_resource_shape,
                trusted_mount_resource) &&
            TryPromotePlayerObservedResourceOwner(
                entry,
                ctx.r15,
                actual_type,
                "spirit-delta observed owner")) {
            tracked_player_spirit = GetTrackedPlayerSpiritEntry();
        }

        const auto& config = GetConfig();
        const uintptr_t tracked_mount_spirit = g_mount_resolve.spirit_entry;
        const bool is_player_spirit = entry == tracked_player_spirit;
        const bool is_mount_spirit = tracked_mount_spirit >= kMinimumPointerAddress &&
                                     entry == tracked_mount_spirit;
        const bool is_untracked_mp_candidate =
            !is_player_spirit &&
            !is_mount_spirit &&
            config.spirit.enabled &&
            original_delta < 0;
        const bool is_untracked_focus_recovery_candidate =
            !is_player_spirit &&
            !is_mount_spirit &&
            config.spirit.enabled &&
            original_delta > 0 &&
            IsFocusRecoveryWindowActive();
        if (!is_player_spirit &&
            !is_mount_spirit &&
            !is_untracked_mp_candidate &&
            !is_untracked_focus_recovery_candidate) {
            return;
        }

        const char* const match_name = is_mount_spirit
            ? "mount"
            : (is_player_spirit
                ? "player"
                : (is_untracked_focus_recovery_candidate
                    ? "untracked-focus-recovery-candidate"
                    : "untracked-mp-candidate"));
        const bool log_sample = ShouldLogSample(g_spirit_delta_samples, 48);
        if (log_sample) {
            Log("hooks: spirit-delta callback entry=0x%p delta=%lld matched=%s owner=0x%p type=%d current=%lld max=%lld tracked_player_spirit=0x%p tracked_mount_spirit=0x%p rip=0x%p",
                reinterpret_cast<void*>(entry),
                static_cast<long long>(original_delta),
                match_name,
                reinterpret_cast<void*>(ctx.r15),
                actual_type,
                static_cast<long long>(current_value),
                static_cast<long long>(max_value),
                reinterpret_cast<void*>(tracked_player_spirit),
                reinterpret_cast<void*>(tracked_mount_spirit),
                reinterpret_cast<void*>(ctx.rip));
        }
        LogSpiritDeltaRegisterContext(ctx, entry, original_delta);

        const auto commit_route =
            psm::routing::SelectAuthoritativeSharedDeltaRoute(
                entry,
                GetTrackedPlayerStaminaEntry(),
                GetTrackedPlayerSpiritEntry());
        if (psm::routing::ShouldDeferUpstreamResourceMutationToCommit(
                static_cast<bool>(g_spirit_value_write_hook),
                commit_route)) {
            if (log_sample) {
                Log("hooks: spirit-delta deferred to authoritative commit entry=0x%p route=%d delta=%lld",
                    reinterpret_cast<void*>(entry),
                    static_cast<int>(commit_route),
                    static_cast<long long>(original_delta));
            }
            return;
        }

        int64_t adjusted_delta = original_delta;
        if (TryAdjustSpiritDelta(entry, &adjusted_delta)) {
            ctx.r8 = static_cast<uintptr_t>(adjusted_delta);
            if (log_sample) {
                Log("hooks: spirit-delta adjusted entry=0x%p old-delta=%lld final-delta=%lld",
                    reinterpret_cast<void*>(entry),
                    static_cast<long long>(original_delta),
                    static_cast<long long>(adjusted_delta));
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (!g_reported_spirit_delta_exception.exchange(true, std::memory_order_acq_rel)) {
            Log("hooks: exception 0x%08lX inside spirit-delta hook, disabling runtime processing",
                GetExceptionCode());
        }

        DisableRuntimeProcessing();
    }
}

      void StaminaAb00Callback(SafetyHookContext& ctx) {
          static std::atomic<int> raw_call_count{0};
          const uintptr_t entry = ctx.rax;
    if (entry < kMinimumPointerAddress) {
        return;
    }

    const int raw_count = raw_call_count.fetch_add(1, std::memory_order_relaxed);
    if (raw_count < 16) {
        Log("hooks: stamina-ab00 raw callback #%d rax=0x%p rbx=%lld r14=0x%p rdi=0x%p rip=0x%p",
            raw_count,
            reinterpret_cast<void*>(ctx.rax),
            static_cast<long long>(ctx.rbx),
            reinterpret_cast<void*>(ctx.r14),
            reinterpret_cast<void*>(ctx.rdi),
            reinterpret_cast<void*>(ctx.rip));
    }

    __try {
        int32_t actual_type = -1;
        int64_t hook_current = 0;
        int64_t hook_max = 0;
        if (!TryReadHookStatEntry(entry, &actual_type, &hook_current, &hook_max)) {
            return;
        }

        {
            const bool recognized_before_bootstrap =
                IsStaminaTypeOrShapeForHook(actual_type, hook_max);
            const bool has_authoritative_player_owner = HasTrackedPlayerDamageOwner();
            const bool exact_authoritative_owner =
                IsExactConfirmedTrackedPlayerOwner(ctx.r14);
            const uintptr_t tracked_stamina_before = GetTrackedPlayerStaminaEntry();
            const uintptr_t tracked_spirit_before = GetTrackedPlayerSpiritEntry();
            const bool active_pair_is_valid =
                tracked_stamina_before >= kMinimumPointerAddress &&
                tracked_spirit_before >= kMinimumPointerAddress;
            std::uint16_t discovered_stamina_selector = 0;
            std::uint16_t discovered_spirit_selector = 0;
            const bool candidate_pair_is_valid =
                TryResolvePlayerResourceSelectorsFromBase(
                    entry,
                    &discovered_stamina_selector,
                    &discovered_spirit_selector);
            const ActorResolveSnapshot mount_snapshot = g_mount_resolve;
            const bool mount_is_trusted =
                mount_snapshot.valid() &&
                g_mount_track_confidence.load(std::memory_order_acquire) >= 3;
            const bool overlaps_trusted_mount =
                mount_is_trusted &&
                (ctx.r14 == mount_snapshot.root ||
                 ctx.r14 == mount_snapshot.damage_target ||
                 entry == mount_snapshot.health_entry ||
                 entry == mount_snapshot.stamina_entry ||
                 entry == mount_snapshot.spirit_entry);

            if (psm::routing::ShouldBootstrapShiftedResourcePairBeforeTypeGate(
                    recognized_before_bootstrap,
                    has_authoritative_player_owner,
                    exact_authoritative_owner,
                    candidate_pair_is_valid,
                    overlaps_trusted_mount,
                    active_pair_is_valid) &&
                TryPromotePlayerObservedResourceOwner(
                    entry,
                    ctx.r14,
                    actual_type,
                    "stamina-ab00 shifted exact-owner pre-gate bootstrap")) {
                const auto sample =
                    g_stamina_ab00_samples.fetch_add(1, std::memory_order_acq_rel);
                if (sample < 24) {
                    Log("hooks: stamina-ab00 pre-gate pair bootstrap owner=0x%p stamina=0x%p spirit=0x%p selectors=%u/%u",
                        reinterpret_cast<void*>(ctx.r14),
                        reinterpret_cast<void*>(GetTrackedPlayerStaminaEntry()),
                        reinterpret_cast<void*>(GetTrackedPlayerSpiritEntry()),
                        static_cast<unsigned>(discovered_stamina_selector),
                        static_cast<unsigned>(discovered_spirit_selector));
                }
            }
        }

        const auto exact_shared_route =
            psm::routing::SelectAuthoritativeSharedDeltaRoute(
                entry,
                GetTrackedPlayerStaminaEntry(),
                GetTrackedPlayerSpiritEntry());
        if (IsAuthoritativePlayerResourceDeltaActive()) {
            // AB00 adds RBX to entry+0x10 and +0x88: this is a reversible
            // rate modifier, not a resource expense. Never scale it.
            ObserveResourceHudModifiers(ctx.r14);
            if (exact_shared_route == psm::routing::DiscoveredPlayerResourceRole::None &&
                !IsExactConfirmedTrackedPlayerOwner(ctx.r14)) {
                UpdateTrackedMountFromStaminaContext(entry, ctx.r14);
            }
            return;
        }
        if (exact_shared_route !=
            psm::routing::DiscoveredPlayerResourceRole::None) {
            const int64_t original_delta = static_cast<int64_t>(ctx.rbx);
            if (psm::routing::ShouldDeferUpstreamResourceMutationToCommit(
                    static_cast<bool>(g_spirit_value_write_hook),
                    exact_shared_route)) {
                const auto sample =
                    g_stamina_ab00_samples.fetch_add(1, std::memory_order_acq_rel);
                if (sample < 48) {
                    Log("hooks: stamina-ab00 observed authoritative player resource entry=0x%p route=%d delta=%lld owner=0x%p note=authoritative-commit-owns-mutation",
                        reinterpret_cast<void*>(entry),
                        static_cast<int>(exact_shared_route),
                        static_cast<long long>(original_delta),
                        reinterpret_cast<void*>(ctx.r14));
                }
                return;
            }

            int64_t adjusted_delta = original_delta;
            const auto kind =
                exact_shared_route ==
                        psm::routing::DiscoveredPlayerResourceRole::Stamina
                    ? AuthoritativeResourceKind::Stamina
                    : AuthoritativeResourceKind::Spirit;
            const bool adjusted = TryAdjustAuthoritativePlayerResourceDelta(
                kind,
                entry,
                &adjusted_delta);
            if (adjusted) {
                ctx.rbx = static_cast<uintptr_t>(adjusted_delta);
            }

            const auto sample =
                g_stamina_ab00_samples.fetch_add(1, std::memory_order_acq_rel);
            if (sample < 48) {
                Log("hooks: stamina-ab00 authoritative player resource entry=0x%p route=%d old-delta=%lld final-delta=%lld adjusted=%d",
                    reinterpret_cast<void*>(entry),
                    static_cast<int>(exact_shared_route),
                    static_cast<long long>(original_delta),
                    static_cast<long long>(adjusted_delta),
                    adjusted ? 1 : 0);
            }
            return;
        }

        if (ctx.r14 >= kMinimumPointerAddress &&
            TryActivateCorrelatedPlayerOwnerCandidate(
                ctx.r14,
                entry,
                static_cast<int64_t>(ctx.rbx),
                "validated stamina resource use")) {
            const auto sample = g_stamina_ab00_samples.fetch_add(1, std::memory_order_acq_rel);
            if (sample < 24) {
                Log("hooks: stamina-ab00 activated validated player owner=0x%p entry=0x%p type=%d max=%lld delta=%lld",
                    reinterpret_cast<void*>(ctx.r14),
                    reinterpret_cast<void*>(entry),
                    actual_type,
                    static_cast<long long>(hook_max),
                    static_cast<long long>(ctx.rbx));
            }
        }

        {
            const int64_t original_delta = static_cast<int64_t>(ctx.rbx);
            const uintptr_t tracked_player_stamina = GetTrackedPlayerStaminaEntry();
            uintptr_t tracked_player_spirit = GetTrackedPlayerSpiritEntry();
            const bool large_type20_resource =
                IsType20StaminaShapedForHook(actual_type, hook_max);
            if (large_type20_resource && entry != tracked_player_stamina) {
                bool is_active_player_spirit = entry == tracked_player_spirit;
                if (!is_active_player_spirit && original_delta < 0) {
                    const bool owner_correlates_with_player =
                        ObservedResourceOwnerMatchesConfirmedPlayerToken(ctx.r14);
                    if (owner_correlates_with_player &&
                        TryBindObservedSpiritToConfirmedPlayer(
                            entry,
                            ctx.r14,
                            original_delta,
                            "stamina-ab00 correlated active spirit cost")) {
                        tracked_player_spirit = GetTrackedPlayerSpiritEntry();
                        is_active_player_spirit = entry == tracked_player_spirit;
                    }
                    if (!is_active_player_spirit) {
                        UpdateTrackedMountFromStaminaContext(entry, ctx.r14);
                    }
                    if (!is_active_player_spirit &&
                        entry != g_mount_resolve.stamina_entry &&
                        entry != g_mount_resolve.spirit_entry &&
                        TryBindObservedSpiritToConfirmedPlayer(
                            entry,
                            ctx.r14,
                            original_delta,
                            "stamina-ab00 active large spirit cost")) {
                        tracked_player_spirit = GetTrackedPlayerSpiritEntry();
                        is_active_player_spirit = entry == tracked_player_spirit;
                    }
                }

                if (is_active_player_spirit) {
                    int64_t adjusted_delta = original_delta;
                    const bool adjusted = TryAdjustSpiritDelta(entry, &adjusted_delta);
                    if (adjusted) {
                        ctx.rbx = static_cast<uintptr_t>(adjusted_delta);
                    }

                    const auto sample =
                        g_large_spirit_ab00_samples.fetch_add(1, std::memory_order_acq_rel);
                    if (sample < 64) {
                        Log("hooks: stamina-ab00 routed active large spirit entry=0x%p old-delta=%lld final-delta=%lld adjusted=%d owner=0x%p max=%lld",
                            reinterpret_cast<void*>(entry),
                            static_cast<long long>(original_delta),
                            static_cast<long long>(adjusted_delta),
                            adjusted ? 1 : 0,
                            reinterpret_cast<void*>(ctx.r14),
                            static_cast<long long>(hook_max));
                    }
                    return;
                }
            }
        }

        if (!IsStaminaTypeOrShapeForHook(actual_type, hook_max)) {
            if (IsSpiritTypeOrShapeForHook(actual_type, hook_max)) {
                const bool log_sample = ShouldLogSample(g_stamina_ab00_samples, 24);
                const int64_t original_delta = static_cast<int64_t>(ctx.rbx);
                const auto& config = GetConfig();
                uintptr_t tracked_player_spirit = GetTrackedPlayerSpiritEntry();
                if ((tracked_player_spirit < kMinimumPointerAddress || entry != tracked_player_spirit) &&
                    ctx.r14 >= kMinimumPointerAddress &&
                    TryPromotePlayerObservedResourceOwner(entry, ctx.r14, actual_type, "stamina-ab00 spirit observed owner")) {
                    tracked_player_spirit = GetTrackedPlayerSpiritEntry();
                }
                const uintptr_t tracked_mount_spirit = g_mount_resolve.spirit_entry;
                int64_t adjusted_delta = original_delta;
                if (TryAdjustSpiritDelta(entry, &adjusted_delta)) {
                    ctx.rbx = static_cast<uintptr_t>(adjusted_delta);
                    if (log_sample || raw_count < 16) {
                        Log("hooks: stamina-ab00 spirit adjusted entry=0x%p old-delta=%lld final-delta=%lld owner=0x%p tracked_player_spirit=0x%p tracked_mount_spirit=0x%p rip=0x%p",
                            reinterpret_cast<void*>(entry),
                            static_cast<long long>(original_delta),
                            static_cast<long long>(adjusted_delta),
                            reinterpret_cast<void*>(ctx.r14),
                            reinterpret_cast<void*>(tracked_player_spirit),
                            reinterpret_cast<void*>(g_mount_resolve.spirit_entry),
                            reinterpret_cast<void*>(ctx.rip));
                    }
                    return;
                }

                const bool mounted_spirit_stamina_cost =
                    config.mount.enabled &&
                    (config.mount.lock_spirit_stamina ||
                     (config.mount.special_enabled && config.mount.lock_special_spirit_stamina)) &&
                    original_delta < 0 &&
                    g_mount_resolve.valid() &&
                    entry == tracked_mount_spirit;
                if (mounted_spirit_stamina_cost) {
                    ctx.rbx = 0;
                    if (log_sample || raw_count < 16) {
                        Log("hooks: stamina-ab00 mounted spirit-stamina locked entry=0x%p old-delta=%lld final-delta=0 owner=0x%p tracked_player_spirit=0x%p tracked_mount_spirit=0x%p matched=%d rip=0x%p",
                            reinterpret_cast<void*>(entry),
                            static_cast<long long>(original_delta),
                            reinterpret_cast<void*>(ctx.r14),
                            reinterpret_cast<void*>(tracked_player_spirit),
                            reinterpret_cast<void*>(tracked_mount_spirit),
                            entry == tracked_player_spirit ? 1 : (entry == tracked_mount_spirit ? 2 : 0),
                            reinterpret_cast<void*>(ctx.rip));
                    }
                    return;
                }

                if (raw_count < 16) {
                    Log("hooks: stamina-ab00 raw skipped spirit type=%d entry=0x%p owner=0x%p tracked_player_spirit=0x%p tracked_mount_spirit=0x%p",
                        actual_type,
                        reinterpret_cast<void*>(entry),
                        reinterpret_cast<void*>(ctx.r14),
                        reinterpret_cast<void*>(tracked_player_spirit),
                        reinterpret_cast<void*>(g_mount_resolve.spirit_entry));
                }
                return;
            }
            if (raw_count < 16) {
                Log("hooks: stamina-ab00 raw skipped non-stamina type=%d entry=0x%p", actual_type, reinterpret_cast<void*>(entry));
            }
            return;
        }

        uintptr_t tracked_player_stamina = GetTrackedPlayerStaminaEntry();
        uintptr_t tracked_player_spirit = GetTrackedPlayerSpiritEntry();
        const bool has_authoritative_player_owner = HasTrackedPlayerDamageOwner();
        const bool exact_authoritative_owner =
            IsExactConfirmedTrackedPlayerOwner(ctx.r14);
        const bool active_pair_is_valid =
            tracked_player_stamina >= kMinimumPointerAddress &&
            tracked_player_spirit >= kMinimumPointerAddress;
        std::uint16_t discovered_stamina_selector = 0;
        std::uint16_t discovered_spirit_selector = 0;
        const bool candidate_pair_is_valid =
            TryResolvePlayerResourceSelectorsFromBase(
                entry,
                &discovered_stamina_selector,
                &discovered_spirit_selector);
        const ActorResolveSnapshot mount_snapshot = g_mount_resolve;
        const bool mount_is_trusted =
            mount_snapshot.valid() &&
            g_mount_track_confidence.load(std::memory_order_acquire) >= 3;
        const bool overlaps_trusted_mount =
            mount_is_trusted &&
            (ctx.r14 == mount_snapshot.root ||
             ctx.r14 == mount_snapshot.damage_target ||
             entry == mount_snapshot.health_entry ||
             entry == mount_snapshot.stamina_entry ||
             entry == mount_snapshot.spirit_entry);
        if (psm::routing::ShouldBootstrapExactOwnerPlayerResourcePair(
                has_authoritative_player_owner,
                exact_authoritative_owner,
                candidate_pair_is_valid,
                overlaps_trusted_mount,
                active_pair_is_valid) &&
            TryPromotePlayerObservedResourceOwner(
                entry,
                ctx.r14,
                actual_type,
                "stamina-ab00 exact authoritative owner bootstrap")) {
            tracked_player_stamina = GetTrackedPlayerStaminaEntry();
            tracked_player_spirit = GetTrackedPlayerSpiritEntry();
        }
        if (tracked_player_stamina < kMinimumPointerAddress &&
            !has_authoritative_player_owner) {
            TryBootstrapPlayerStaminaFromEntry(entry);
            tracked_player_stamina = GetTrackedPlayerStaminaEntry();
        }
        const bool stat_write_installed = static_cast<bool>(g_stat_write_hook);
        if (psm::routing::DirectStaminaMayPromoteObservedOwner(stat_write_installed) &&
            (tracked_player_stamina < kMinimumPointerAddress || entry != tracked_player_stamina) &&
            ctx.r14 >= kMinimumPointerAddress &&
            TryPromotePlayerObservedResourceOwner(entry, ctx.r14, actual_type, "stamina-ab00 observed owner")) {
            tracked_player_stamina = GetTrackedPlayerStaminaEntry();
        }

        const bool log_sample = ShouldLogSample(g_stamina_ab00_samples, 24);
        if (log_sample) {
            Log("hooks: stamina-ab00 callback entry=0x%p delta=%lld owner=0x%p status=%u tracked_player_stamina=0x%p rip=0x%p",
                reinterpret_cast<void*>(entry),
                static_cast<long long>(ctx.rbx),
                reinterpret_cast<void*>(ctx.r14),
                static_cast<unsigned>(ctx.rdi & 0xFFFFu),
                reinterpret_cast<void*>(tracked_player_stamina),
                reinterpret_cast<void*>(ctx.rip));
        }

        if (entry != tracked_player_stamina) {
            int64_t redirected_delta = static_cast<int64_t>(ctx.rbx);
            if (TryRedirectCombatStaminaCostToAdjacentSpirit(entry, &redirected_delta)) {
                ctx.rbx = static_cast<uintptr_t>(redirected_delta);
                if (log_sample) {
                    Log("hooks: stamina-ab00 redirected adjacent combat resource entry=0x%p final-delta=%lld",
                        reinterpret_cast<void*>(entry),
                        static_cast<long long>(redirected_delta));
                }
                return;
            }
            UpdateTrackedMountFromStaminaContext(entry, ctx.r14);
        }

        if (entry == tracked_player_stamina &&
            static_cast<int64_t>(ctx.rbx) < 0 &&
            IsFocusComboWindowActive()) {
            const int64_t focus_delta = static_cast<int64_t>(ctx.rbx);
            ctx.rbx = 0;
            const auto focus_log = g_focus_guard_logs.fetch_add(1, std::memory_order_acq_rel);
            if (focus_log < 16) {
                Log("hooks: stamina-ab00 suppressed focus combo stamina-shaped cost entry=0x%p old_delta=%lld rip=0x%p",
                    reinterpret_cast<void*>(entry),
                    static_cast<long long>(focus_delta),
                    reinterpret_cast<void*>(ctx.rip));
            }
            return;
        }

        if (entry == tracked_player_stamina &&
            TryRedirectPlayerStaminaCallToSpirit(ctx, entry, static_cast<int64_t>(ctx.rbx))) {
            return;
        }

        const bool post_rematch_detached_stamina_cost =
            entry == tracked_player_stamina &&
            static_cast<int64_t>(ctx.rbx) < 0 &&
            IsPostRematchDetachedPlayerStaminaMirror(entry);

        const bool direct_stamina_owns_delta =
            psm::routing::DirectStaminaOwnsObservedPlayerDelta(
                entry == tracked_player_stamina,
                true);
        if (entry == tracked_player_stamina &&
            !direct_stamina_owns_delta) {
            if (log_sample) {
                Log("hooks: stamina-ab00 player stamina passthrough entry=0x%p delta=%lld note=exact-entry-stat-write-authoritative",
                    reinterpret_cast<void*>(entry),
                    static_cast<long long>(ctx.rbx));
            }
            return;
        }

        if (post_rematch_detached_stamina_cost && log_sample) {
            Log("hooks: stamina-ab00 routing post-rematch detached player stamina through delta scaler entry=0x%p delta=%lld",
                reinterpret_cast<void*>(entry),
                static_cast<long long>(ctx.rbx));
        }

        int64_t adjusted_delta = static_cast<int64_t>(ctx.rbx);
        if (TryAdjustStaminaDelta(entry, &adjusted_delta)) {
            MarkPendingDirectStaminaDelta(entry);
            ctx.rbx = static_cast<uintptr_t>(adjusted_delta);
            if (log_sample) {
                Log("hooks: stamina-ab00 adjusted entry=0x%p final-delta=%lld",
                    reinterpret_cast<void*>(entry),
                    static_cast<long long>(adjusted_delta));
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (!g_reported_stamina_ab00_exception.exchange(true, std::memory_order_acq_rel)) {
            Log("hooks: exception 0x%08lX inside stamina-ab00 hook, disabling runtime processing", GetExceptionCode());
        }

        DisableRuntimeProcessing();
    }
}

bool InstallSpiritDeltaHook() {
    const uintptr_t target = ScanForSpiritDeltaAccess();
    if (target == 0) {
        return false;
    }

    const bool selector_contract = ValidateSpiritDeltaSelectorContract(target);
    if (!selector_contract) {
        Log("hooks: explicit resource-delta selector contract validation failed at 0x%p; failing closed",
            reinterpret_cast<void*>(target));
        return false;
    }
    g_spirit_delta_selector_contract.store(true, std::memory_order_release);

    auto hook_result = psm::game::CreateMid(reinterpret_cast<void*>(target), SpiritDeltaCallback);
    if (!hook_result.has_value()) {
        g_spirit_delta_selector_contract.store(false, std::memory_order_release);
        Log("hooks: failed to create spirit-delta mid hook");
        return false;
    }

    g_spirit_delta_hook = std::move(*hook_result);
    Log("hooks: installed explicit resource-delta hook at 0x%p contract=selector-rsp+0x%llX rcx-entry rdx-context r8-delta",
        reinterpret_cast<void*>(target),
        static_cast<unsigned long long>(kSpiritDeltaSelectorStackOffset));
    return true;
}

bool InstallSharedResourceDeltaHook() {
    const uintptr_t target = ScanForSharedResourceDeltaAccess();
    if (target == 0) {
        return false;
    }

    auto hook_result = psm::game::CreateMid(
        reinterpret_cast<void*>(target),
        SharedResourceDeltaCallback);
    if (!hook_result.has_value()) {
        Log("hooks: failed to create shared-resource-delta mid hook");
        return false;
    }

    g_shared_resource_delta_hook = std::move(*hook_result);
    SetAuthoritativePlayerResourceDeltaActive(true);
    Log("hooks: installed shared-resource-delta hook at 0x%p contract=rcx-exact-entry rdx-token r8-signed-delta authority=exact-tracked-pair",
        reinterpret_cast<void*>(target));
    return true;
}

bool InstallSpiritValueWriteHook() {
    const uintptr_t target = ScanForSpiritValueWriteAccess();
    if (target == 0) {
        return false;
    }

    auto hook_result = psm::game::CreateMid(reinterpret_cast<void*>(target), SpiritValueWriteCallback);
    if (!hook_result.has_value()) {
        Log("hooks: failed to create shared-resource-commit mid hook");
        return false;
    }

    g_spirit_value_write_hook = std::move(*hook_result);
    Log("hooks: installed shared-resource-commit hook at 0x%p contract=rcx-entry-rdx-context-r8-requested",
        reinterpret_cast<void*>(target));
    return true;
}

bool InstallPlayerPointerHook() {
    const auto target = ScanForPlayerPointerCapture();
    if (target.address == 0) {
        return false;
    }

    g_player_pointer_capture_kind.store(target.capture_kind, std::memory_order_release);

    auto hook_result = psm::game::CreateMid(reinterpret_cast<void*>(target.address), PlayerPointerCallback);
    if (!hook_result.has_value()) {
        Log("hooks: failed to create player-pointer mid hook");
        return false;
    }

    g_player_pointer_hook = std::move(*hook_result);
    const char* const capture_source =
        target.capture_kind == PlayerPointerCaptureKind::CurrentPlayerCaptureBaseRax
            ? "capture-base-rax"
            : (target.capture_kind == PlayerPointerCaptureKind::LegacyMarkerRdx ? "rdx" : "rsi");
    Log("hooks: installed player-pointer hook at 0x%p marker-source=%s authoritative=%d",
        reinterpret_cast<void*>(target.address),
        capture_source,
        IsAuthoritativePlayerPointerCapture(target.capture_kind) ? 1 : 0);
    return true;
}

bool InstallStatsHook(const uintptr_t target) {
    if (target == 0) {
        return false;
    }

    auto hook_result = psm::game::CreateMid(reinterpret_cast<void*>(target), StatsCallback);
    if (!hook_result.has_value()) {
        Log("hooks: failed to create stats mid hook");
        return false;
    }

    g_stats_hook = std::move(*hook_result);
    Log("hooks: installed stats hook at 0x%p", reinterpret_cast<void*>(target));
    return true;
}

bool InstallStatWriteHook() {
    StatWriteCaptureTarget target{};
    auto hook = psm::startup::CreateValidatedMid(
        [&]() -> uintptr_t {
            target = ScanForStatWriteAccess();
            if (!target.address) {
                Log("hooks: stat-write native contract unavailable; no stale target retry");
                return 0;
            }
            // A newly enabled hook may be entered before the owning handle moves.
            g_stat_write_value_register = target.value_register;
            g_stat_write_enforce_native_bounds.store(target.enforce_native_bounds, std::memory_order_release);
            return target.address;
        },
        [](const uintptr_t address) {
            return psm::game::CreateMid(reinterpret_cast<void*>(address), StatWriteCallback);
        },
        [](const unsigned attempt, const uintptr_t address, const safetyhook::MidHook::Error& error) {
            const auto detail = psm::startup::Describe(error);
            MEMORY_BASIC_INFORMATION region{};
            const bool queried = VirtualQuery(reinterpret_cast<const void*>(address), &region, sizeof(region)) != 0;
            const auto image = reinterpret_cast<uintptr_t>(GetModuleHandleW(L"CrimsonDesert.exe"));
            Log("hooks: stat-write create failed attempt=%u/%u site=0x%p rva=0x%llX reason=%s mid=%d inline=%d allocator=%d instruction=0x%p query=%d state=0x%lX protect=0x%lX type=0x%lX; exact native contract required before retry",
                attempt, psm::startup::kMaxMidAttempts, reinterpret_cast<void*>(address),
                static_cast<unsigned long long>(image && address >= image ? address - image : 0),
                detail.category, detail.mid_code, detail.inline_code, detail.allocator_code,
                reinterpret_cast<void*>(detail.instruction), queried ? 1 : 0,
                region.State, region.Protect, region.Type);
        },
        [](const unsigned delay_ms) { Sleep(delay_ms); });
    if (!hook) {
        Log("hooks: stat-write startup attempts exhausted or native contract lost; health/mount protection inactive");
        return false;
    }
    g_stat_write_hook = std::move(hook);
    Log("hooks: installed stat-write pre-notify hook at 0x%p value-register=rbx",
        reinterpret_cast<void*>(target.address));
    return true;
}

      bool InstallStaminaAb00Hook() {
          const uintptr_t target = ScanForStaminaAb00Access();
          if (target == 0) {
              return false;
    }

    auto hook_result = psm::game::CreateMid(reinterpret_cast<void*>(target), StaminaAb00Callback);
    if (!hook_result.has_value()) {
        Log("hooks: failed to create stamina-ab00 mid hook");
        return false;
    }

          g_stamina_ab00_hook = std::move(*hook_result);
          Log("hooks: installed stamina-ab00 hook at 0x%p", reinterpret_cast<void*>(target));
          return true;
      }

}  // namespace

#ifdef PSM_STAT_WRITE_TESTING
void TestStatWriteCallback(SafetyHookContext& ctx, bool current_3446) {
    g_stat_write_enforce_native_bounds.store(current_3446, std::memory_order_release);
    StatWriteCallback(ctx);
}
#endif
#ifdef PSM_MOUNT_TESTING
void TestNativeResourceRateCallback(SafetyHookContext& ctx) { NativeResourceRateCallback(ctx); }
void TestNativeResourceCostCallback(SafetyHookContext& ctx) { NativeResourceCostCallback(ctx); }
#endif

bool InstallPlayerHooks() {
    return InstallPlayerPointerHook();
}

bool InstallPlayerStatHooks() {
    const auto config = GetConfig();
    // Rate and cost form one ABI contract. A partial install must not fall
    // back to treating reversible rate changes as consumable resource deltas.
    const uintptr_t rate_site = ScanForNativeResourceRateAccess();
    const uintptr_t cost_site = ScanForNativeResourceCostAccess();
    if (!rate_site || !cost_site || !ValidateNativeResourceGroup(rate_site, cost_site)) {
        Log("hooks: required native resource group unresolved; refusing legacy resource fallbacks");
        return false;
    }
    const auto hud = PrepareResourceHudHooks(cost_site);
    const auto mounted = PrepareNativeMountedResourceHooks();
    const auto oxygen = PreparePlayerOxygenHooks();
    const uintptr_t stats_site = ScanForStatsAccess();
    const bool oxygen_deadline = ValidateOxygenRateDeadline(rate_site);
    g_oxygen_rate_has_deadline.store(oxygen_deadline, std::memory_order_release);
    auto rate_hook = psm::game::CreateMid(reinterpret_cast<void*>(rate_site), NativeResourceRateCallback);
    auto cost_hook = psm::game::CreateMid(reinterpret_cast<void*>(cost_site), NativeResourceCostCallback);
    if (!rate_hook.has_value() || !cost_hook.has_value()) {
        g_oxygen_rate_has_deadline.store(false, std::memory_order_release);
        Log("hooks: native resource group creation failed; rolling back both hooks");
        return false;
    }
    g_native_resource_rate_hook = std::move(*rate_hook);
    g_shared_resource_delta_hook = std::move(*cost_hook);
    SetAuthoritativePlayerResourceDeltaActive(true);
    Log("oxygen: native cached-minimum override=%d", oxygen_deadline ? 1 : 0);
    Log("hooks: native resource group installed rate=0x%p contract=rbx-entry-r11-rate cost=0x%p contract=rsi-entry-rbx-delta; no stored-rate or HUD writes",
        reinterpret_cast<void*>(rate_site), reinterpret_cast<void*>(cost_site));
    if (!InstallStatsHook(stats_site)) {
        Log("hooks: stats discovery hook unavailable; continuing with direct resource hooks");
    }
    // The generic write hook retains health/damage/mount policies. Player
    // stamina and Spirit bypass it because the native rate/cost paths own them.
    if (ShouldInstallLegacyStatWriteHook(config) && !InstallStatWriteHook()) {
        Log("hooks: required health/mount stat-write hook unavailable; installation failed");
        return false;
    }

    if ((ShouldInstallStaminaHook(config) || config.stamina.enabled || config.spirit.enabled) &&
        !InstallStaminaAb00Hook()) {
        Log("hooks: stamina-ab00 hook unavailable; continuing without direct stamina scaling");
    }

    auto available_hud = hud;
    available_hud.modifiers = available_hud.modifiers && static_cast<bool>(g_stamina_ab00_hook);
    InstallResourceHudHooks(available_hud);
    InstallNativeMountedResourceHooks(mounted);
    InstallPlayerOxygenHooks(oxygen, g_stats_hook && oxygen.stats_after_lookup == stats_site);

    Log("hooks: feature readiness stats=%d shared-delta=%d stat-write=%d stamina=%d spirit-delta=%d spirit-value=%d",
        g_stats_hook ? 1 : 0,
        g_shared_resource_delta_hook ? 1 : 0,
        g_stat_write_hook ? 1 : 0,
        g_stamina_ab00_hook ? 1 : 0,
        g_spirit_delta_hook ? 1 : 0,
        g_spirit_value_write_hook ? 1 : 0);
    return true;
}
      
void RemovePlayerStatHooks() {
    RemovePlayerOxygenHooks();
    RemoveNativeMountedResourceHooks();
    RemoveResourceHudHooks();
    SetAuthoritativePlayerResourceDeltaActive(false);
    g_native_resource_rate_hook.reset();
    g_oxygen_rate_has_deadline.store(false, std::memory_order_release);
      if (g_stamina_ab00_hook) {
          g_stamina_ab00_hook.reset();
          Log("hooks: removed stamina-ab00 hook");
    }

    if (g_spirit_delta_hook) {
        g_spirit_delta_hook.reset();
        g_spirit_delta_selector_contract.store(false, std::memory_order_release);
        Log("hooks: removed spirit-delta hook");
    }

    if (g_spirit_value_write_hook) {
        g_spirit_value_write_hook.reset();
        Log("hooks: removed spirit-value-write hook");
    }

    if (g_stat_write_hook) {
        g_stat_write_hook.reset();
        Log("hooks: removed stat-write hook");
    }

    if (g_shared_resource_delta_hook) {
        SetAuthoritativePlayerResourceDeltaActive(false);
        g_shared_resource_delta_hook.reset();
        Log("hooks: removed shared-resource-delta hook");
    }

    if (g_stats_hook) {
        g_stats_hook.reset();
        Log("hooks: removed stats hook");
    }
}

void RemovePlayerHooks() {
    RemovePlayerStatHooks();

    if (g_player_pointer_hook) {
        g_player_pointer_hook.reset();
        Log("hooks: removed player-pointer hook");
    }
}


