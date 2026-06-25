#include "runtime/stat_logic.h"

#include "config.h"
#include "logger.h"
#include "mod_logic.h"
#include "runtime/actor_resolve.h"
#include "runtime/runtime_state.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <Windows.h>
#include <Xinput.h>

namespace {

constexpr int64_t kMountHealthStatWriteMinMax = 2500000;
constexpr int64_t kMountStaminaStatWriteMinMax = 300000;
constexpr int64_t kGroundMountHealthCandidateMinMax = 10000;
constexpr int64_t kGroundMountHealthCandidateMaxMax = 600000;
constexpr int64_t kSpecialMountHealthCandidateMaxMax = 250000;
constexpr int64_t kSpecialMountStaminaMinMax = 180000;
constexpr int64_t kSpecialMountStaminaMaxMax = 299999;
constexpr DWORD kVisibleSpiritCostWindowMs = 1000;
constexpr uint32_t kRecoveryBoostAfterCostDelayMs = 1000;
constexpr int64_t kPlayerSpiritMinimumFloor = 1;
constexpr int64_t kPassiveRegenMaxPermille = 30;
constexpr intptr_t kPostRematchAltStaminaOffsetFromHealth = -0x260;
constexpr uintptr_t kPostRematchAltSpiritOffsetFromHealth = 0x9B0;
constexpr int64_t kPostRematchHealthMax = 300000;
constexpr int64_t kPostRematchResourceMax = 100000;
constexpr int64_t kPostRematchDetachedStaminaMinMax = 400000;
constexpr int64_t kPostRematchDetachedStaminaMaxMax = 500000;
constexpr int64_t kPostRematchDetachedSpiritMinMax = 100000;
constexpr int64_t kPostRematchDetachedSpiritMaxMax = 220000;

#ifndef VK_GAMEPAD_LEFT_THUMBSTICK_BUTTON
#define VK_GAMEPAD_LEFT_THUMBSTICK_BUTTON 0xC8
#endif

#ifndef VK_GAMEPAD_RIGHT_THUMBSTICK_BUTTON
#define VK_GAMEPAD_RIGHT_THUMBSTICK_BUTTON 0xC9
#endif

using GetAsyncKeyStateFn = SHORT(WINAPI*)(int);

GetAsyncKeyStateFn ResolveGetAsyncKeyState() {
    static HMODULE user32 = LoadLibraryW(L"user32.dll");
    static GetAsyncKeyStateFn fn = user32
        ? reinterpret_cast<GetAsyncKeyStateFn>(GetProcAddress(user32, "GetAsyncKeyState"))
        : nullptr;
    return fn;
}

bool IsKeyDown(const int vk) {
    const auto get_async_key_state = ResolveGetAsyncKeyState();
    return vk != 0 && get_async_key_state != nullptr && (get_async_key_state(vk) & 0x8000) != 0;
}

bool SamplePhysicalFocusInput(bool* const keyboard_focus_out = nullptr,
                              bool* const left_stick_out = nullptr,
                              bool* const right_stick_out = nullptr,
                              bool* const xinput_focus_out = nullptr) {
    bool xinput_focus = false;
    for (DWORD index = 0; index < XUSER_MAX_COUNT; ++index) {
        XINPUT_STATE state{};
        if (XInputGetState(index, &state) != ERROR_SUCCESS) {
            continue;
        }

        constexpr WORD kFocusButtons = XINPUT_GAMEPAD_LEFT_THUMB | XINPUT_GAMEPAD_RIGHT_THUMB;
        if ((state.Gamepad.wButtons & kFocusButtons) == kFocusButtons) {
            xinput_focus = true;
            break;
        }
    }

    const bool keyboard_focus = IsKeyDown('X');
    const bool left_stick = IsKeyDown(VK_GAMEPAD_LEFT_THUMBSTICK_BUTTON);
    const bool right_stick = IsKeyDown(VK_GAMEPAD_RIGHT_THUMBSTICK_BUTTON);

    if (keyboard_focus_out != nullptr) {
        *keyboard_focus_out = keyboard_focus;
    }
    if (left_stick_out != nullptr) {
        *left_stick_out = left_stick;
    }
    if (right_stick_out != nullptr) {
        *right_stick_out = right_stick;
    }
    if (xinput_focus_out != nullptr) {
        *xinput_focus_out = xinput_focus;
    }

    return keyboard_focus || (left_stick && right_stick) || xinput_focus;
}

std::atomic<std::uint32_t> g_mount_stat_write_logs{0};
std::atomic<std::uint32_t> g_mount_candidate_health_logs{0};
std::atomic<std::uint32_t> g_resistance_apply_logs{0};
std::atomic<std::uint32_t> g_stamina_spirit_diag_logs{0};
std::atomic<std::uint32_t> g_player_stat_profile_logs{0};
std::atomic<std::uint32_t> g_player_stat_dense_profile_logs{0};
std::atomic<std::uint32_t> g_spirit_mirror_logs{0};
std::atomic<uintptr_t> g_last_spirit_mirror_entry{0};
std::atomic<int64_t> g_last_spirit_mirror_value{0};
std::atomic<DWORD> g_last_spirit_mirror_tick{0};
std::atomic<uintptr_t> g_last_rerouted_spirit_entry{0};
std::atomic<int64_t> g_last_rerouted_spirit_value{0};
std::atomic<DWORD> g_last_rerouted_spirit_tick{0};
std::atomic<uintptr_t> g_last_player_spirit_consume_entry{0};
std::atomic<DWORD> g_last_player_spirit_consume_tick{0};
std::atomic<uintptr_t> g_last_player_stamina_consume_entry{0};
std::atomic<DWORD> g_last_player_stamina_consume_tick{0};
std::atomic<uintptr_t> g_last_player_stamina_prenotify_entry{0};
std::atomic<int64_t> g_last_player_stamina_prenotify_value{0};
std::atomic<DWORD> g_last_player_stamina_prenotify_tick{0};
std::atomic<std::uint32_t> g_stamina_prenotify_accept_logs{0};
std::atomic<uintptr_t> g_stamina_ab00_virtual_entry{0};
std::atomic<int64_t> g_stamina_ab00_virtual_value{-1};
std::atomic<int64_t> g_stamina_ab00_virtual_max{0};
std::atomic<DWORD> g_stamina_ab00_virtual_tick{0};
std::atomic<std::uint32_t> g_stamina_ab00_virtual_logs{0};
std::atomic<DWORD> g_last_focus_recovery_tick{0};
std::atomic<bool> g_last_focus_recovery_state{false};
std::atomic<bool> g_focus_mode_active{false};
std::atomic<bool> g_focus_input_was_down{false};
std::atomic<std::uint32_t> g_focus_mode_state_logs{0};
std::atomic<std::uint32_t> g_spirit_recovery_block_logs{0};
std::atomic<std::uint32_t> g_legacy_spirit_mirror_apply_logs{0};
std::atomic<std::uint32_t> g_spirit_mirror_probe_logs{0};
std::atomic<uintptr_t> g_post_rematch_detached_stamina_entry{0};
std::atomic<uintptr_t> g_post_rematch_detached_spirit_entry{0};

bool TryReadStatEntryValues(const uintptr_t entry,
                            const int32_t expected_type,
                            int64_t* const current_value,
                            int64_t* const max_value) {
    if (current_value == nullptr || max_value == nullptr || entry < kMinimumPointerAddress) {
        return false;
    }

    __try {
        const int32_t actual_type = *reinterpret_cast<const int32_t*>(entry);
        const bool type_matches = expected_type == kStaminaId
            ? IsStaminaStatId(actual_type)
            : (expected_type == kSpiritId ? IsSpiritStatId(actual_type) : actual_type == expected_type);
        if (!type_matches) {
            return false;
        }

        const int64_t current = *reinterpret_cast<const int64_t*>(entry + 0x08);
        const int64_t maximum = *reinterpret_cast<const int64_t*>(entry + 0x18);
        if (maximum <= 0 || current < 0 || current > maximum) {
            return false;
        }

        *current_value = current;
        *max_value = maximum;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool IsPostRematchPlayerHealthProfile(const uintptr_t health_entry) {
    if (health_entry < kMinimumPointerAddress) {
        return false;
    }

    __try {
        const int32_t health_type = *reinterpret_cast<const int32_t*>(health_entry);
        const int64_t health_max = *reinterpret_cast<const int64_t*>(health_entry + 0x18);
        return health_type == kHealthId && health_max == kPostRematchHealthMax;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool IsPostRematchAltPlayerStaminaEntry(const uintptr_t entry) {
    const uintptr_t health_entry = GetTrackedPlayerHealthEntry();
    if (!IsPostRematchPlayerHealthProfile(health_entry)) {
        return false;
    }

    const uintptr_t expected =
        static_cast<uintptr_t>(static_cast<intptr_t>(health_entry) + kPostRematchAltStaminaOffsetFromHealth);
    if (entry != expected) {
        return false;
    }

    int64_t current_value = 0;
    int64_t max_value = 0;
    return TryReadStatEntryValues(entry, kStaminaId, &current_value, &max_value) &&
           max_value == kPostRematchResourceMax;
}

bool IsPostRematchAltPlayerSpiritEntry(const uintptr_t entry) {
    const uintptr_t health_entry = GetTrackedPlayerHealthEntry();
    if (!IsPostRematchPlayerHealthProfile(health_entry) ||
        entry != health_entry + kPostRematchAltSpiritOffsetFromHealth) {
        return false;
    }

    int64_t current_value = 0;
    int64_t max_value = 0;
    return TryReadStatEntryValues(entry, kSpiritId, &current_value, &max_value) &&
           max_value == kPostRematchResourceMax;
}

bool IsPostRematchDetachedPlayerResourceEntry(const uintptr_t entry, const int32_t actual_type) {
    if (entry < kMinimumPointerAddress ||
        !IsPostRematchPlayerHealthProfile(GetTrackedPlayerHealthEntry())) {
        return false;
    }

    const ActorResolveSnapshot mount_snapshot = g_mount_resolve;
    const bool mount_trusted =
        mount_snapshot.valid() &&
        g_mount_track_confidence.load(std::memory_order_acquire) >= 3;
    if (mount_trusted &&
        (entry == mount_snapshot.health_entry ||
         entry == mount_snapshot.stamina_entry ||
         entry == mount_snapshot.spirit_entry)) {
        return false;
    }

    int64_t current_value = 0;
    int64_t max_value = 0;
    if (IsStaminaStatId(actual_type)) {
        const uintptr_t learned_entry = g_post_rematch_detached_stamina_entry.load(std::memory_order_acquire);
        if (learned_entry >= kMinimumPointerAddress) {
            return entry == learned_entry;
        }

        if (!TryReadStatEntryValues(entry, kStaminaId, &current_value, &max_value)) {
            return false;
        }

        // The post-rematch gameplay path can continue writing the visible 440k
        // player stamina mirror while the tracked rematch combat block is 100k.
        if (max_value >= kPostRematchDetachedStaminaMinMax &&
            max_value <= kPostRematchDetachedStaminaMaxMax &&
            !mount_trusted) {
            g_post_rematch_detached_stamina_entry.store(entry, std::memory_order_release);
            return true;
        }

        return false;
    }

    if (!IsPrimarySpiritStatId(actual_type) ||
        !TryReadStatEntryValues(entry, kSpiritId, &current_value, &max_value)) {
        return false;
    }

    const uintptr_t learned_entry = g_post_rematch_detached_spirit_entry.load(std::memory_order_acquire);
    if (learned_entry >= kMinimumPointerAddress) {
        return entry == learned_entry;
    }

    // Focus recovery and some direct MP writes still hit the legacy 170k
    // player spirit mirror after rematch; treat only the primary spirit type as
    // player-owned here so unrelated type-17 rematch entries stay on the exact
    // offset path above.
    if (max_value >= kPostRematchDetachedSpiritMinMax &&
        max_value <= kPostRematchDetachedSpiritMaxMax &&
        !mount_trusted) {
        g_post_rematch_detached_spirit_entry.store(entry, std::memory_order_release);
        return true;
    }

    return false;
}

TrackedStatEntryKind ClassifyForResourceScaling(const uintptr_t entry,
                                                const TrackedStatEntryKind entry_kind,
                                                const int32_t actual_type) {
    if (entry_kind != TrackedStatEntryKind::None) {
        return entry_kind;
    }
    if (IsStaminaStatId(actual_type) && IsPostRematchAltPlayerStaminaEntry(entry)) {
        const auto current = g_stamina_spirit_diag_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 96) {
            Log("runtime: accepted post-rematch alt stamina as player resource entry=0x%p health=0x%p offset=-0x260",
                reinterpret_cast<void*>(entry),
                reinterpret_cast<void*>(GetTrackedPlayerHealthEntry()));
        }
        return TrackedStatEntryKind::PlayerStamina;
    }
    // The 440k post-rematch stamina entry is a visible/UI mirror, not the
    // authoritative stamina component.  It is still detected elsewhere so it
    // is not misclassified as a mount, but scaling it here causes background
    // mirror refreshes to become continuous stamina drain.
    if (IsSpiritStatId(actual_type) && IsPostRematchAltPlayerSpiritEntry(entry)) {
        const auto current = g_stamina_spirit_diag_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 96) {
            Log("runtime: accepted post-rematch alt spirit as player resource entry=0x%p health=0x%p offset=+0x9B0",
                reinterpret_cast<void*>(entry),
                reinterpret_cast<void*>(GetTrackedPlayerHealthEntry()));
        }
        return TrackedStatEntryKind::PlayerSpirit;
    }
    if (IsPrimarySpiritStatId(actual_type) && IsPostRematchDetachedPlayerResourceEntry(entry, actual_type)) {
        const auto current = g_stamina_spirit_diag_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 128) {
            Log("runtime: accepted post-rematch detached spirit as player resource entry=0x%p health=0x%p max-range=legacy-visible",
                reinterpret_cast<void*>(entry),
                reinterpret_cast<void*>(GetTrackedPlayerHealthEntry()));
        }
        return TrackedStatEntryKind::PlayerSpirit;
    }
    return entry_kind;
}

void RecordSpiritMirrorWrite(const uintptr_t entry, const int64_t final_value) {
    g_last_spirit_mirror_value.store(final_value, std::memory_order_release);
    g_last_spirit_mirror_tick.store(GetTickCount(), std::memory_order_release);
    g_last_spirit_mirror_entry.store(entry, std::memory_order_release);
}

void RecordPlayerSpiritConsumption(const uintptr_t entry) {
    if (entry < kMinimumPointerAddress) {
        return;
    }

    g_last_player_spirit_consume_tick.store(GetTickCount(), std::memory_order_release);
    g_last_player_spirit_consume_entry.store(entry, std::memory_order_release);
}

void RecordPlayerStaminaConsumption(const uintptr_t entry) {
    if (entry < kMinimumPointerAddress) {
        return;
    }

    g_last_player_stamina_consume_tick.store(GetTickCount(), std::memory_order_release);
    g_last_player_stamina_consume_entry.store(entry, std::memory_order_release);
}

void RecordPlayerStaminaPreNotifyWriteInternal(const uintptr_t entry, const int64_t final_value) {
    if (entry < kMinimumPointerAddress || final_value < 0) {
        return;
    }

    g_last_player_stamina_prenotify_value.store(final_value, std::memory_order_release);
    g_last_player_stamina_prenotify_tick.store(GetTickCount(), std::memory_order_release);
    g_last_player_stamina_prenotify_entry.store(entry, std::memory_order_release);
}

bool TryConsumePlayerStaminaPreNotifyWriteInternal(const uintptr_t entry,
                                                  const int64_t observed_value,
                                                  int64_t* const final_value) {
    constexpr DWORD kAcceptWindowMs = 350;
    if (final_value == nullptr || entry < kMinimumPointerAddress || observed_value < 0) {
        return false;
    }

    const uintptr_t recorded_entry = g_last_player_stamina_prenotify_entry.load(std::memory_order_acquire);
    if (recorded_entry != entry) {
        return false;
    }

    const int64_t recorded_value = g_last_player_stamina_prenotify_value.load(std::memory_order_acquire);
    if (recorded_value != observed_value) {
        return false;
    }

    const DWORD recorded_tick = g_last_player_stamina_prenotify_tick.load(std::memory_order_acquire);
    if (recorded_tick == 0 || GetTickCount() - recorded_tick > kAcceptWindowMs) {
        return false;
    }

    g_last_player_stamina_prenotify_entry.store(0, std::memory_order_release);
    g_last_player_stamina_prenotify_value.store(0, std::memory_order_release);
    g_last_player_stamina_prenotify_tick.store(0, std::memory_order_release);
    *final_value = observed_value;

    const auto log_index = g_stamina_prenotify_accept_logs.fetch_add(1, std::memory_order_acq_rel);
    if (log_index < 64) {
        Log("runtime: accepted pre-notify scaled stamina write entry=0x%p observed=%lld age-ms=%lu",
            reinterpret_cast<void*>(entry),
            static_cast<long long>(observed_value),
            static_cast<unsigned long>(GetTickCount() - recorded_tick));
    }
    return true;
}

int64_t ScaleVirtualStaminaRecovery(const int64_t delta, const double multiplier) {
    if (delta <= 0) {
        return 0;
    }

    const double scaled = static_cast<double>(delta) * multiplier;
    return std::max<int64_t>(1, static_cast<int64_t>(scaled + 0.5));
}

bool WasRecentConsumption(uintptr_t entry,
                          DWORD window_ms,
                          const std::atomic<uintptr_t>& last_entry_ref,
                          const std::atomic<DWORD>& last_tick_ref);

void ClearPlayerStaminaAb00VirtualState() {
    g_stamina_ab00_virtual_entry.store(0, std::memory_order_release);
    g_stamina_ab00_virtual_value.store(-1, std::memory_order_release);
    g_stamina_ab00_virtual_max.store(0, std::memory_order_release);
    g_stamina_ab00_virtual_tick.store(0, std::memory_order_release);
}

bool AdvancePlayerStaminaAb00VirtualState(const uintptr_t entry,
                                          const int64_t max_value,
                                          const ModConfig& config,
                                          const DWORD now,
                                          int64_t* const advanced_value) {
    if (advanced_value == nullptr ||
        entry < kMinimumPointerAddress ||
        max_value <= 0 ||
        !config.general.enabled ||
        !config.stamina.enabled) {
        return false;
    }

    const uintptr_t virtual_entry = g_stamina_ab00_virtual_entry.load(std::memory_order_acquire);
    const int64_t virtual_value = g_stamina_ab00_virtual_value.load(std::memory_order_acquire);
    const int64_t virtual_max = g_stamina_ab00_virtual_max.load(std::memory_order_acquire);
    const DWORD virtual_tick = g_stamina_ab00_virtual_tick.load(std::memory_order_acquire);
    if (virtual_entry != entry ||
        virtual_value < 0 ||
        virtual_value > max_value ||
        virtual_max != max_value ||
        virtual_tick == 0) {
        return false;
    }

    const DWORD elapsed_ms = now - virtual_tick;
    if (elapsed_ms > 3000) {
        ClearPlayerStaminaAb00VirtualState();
        return false;
    }

    int64_t value = virtual_value;
    if (WasRecentConsumption(entry, 900, g_last_player_stamina_consume_entry, g_last_player_stamina_consume_tick)) {
        g_stamina_ab00_virtual_tick.store(now, std::memory_order_release);
        *advanced_value = value;
        return true;
    }

    if (elapsed_ms > 0 && value < max_value) {
        const int64_t base_recovery = std::max<int64_t>(1, (max_value * static_cast<int64_t>(elapsed_ms)) / 8000);
        value = std::clamp(
            value + ScaleVirtualStaminaRecovery(base_recovery, config.stamina.heal_multiplier),
            int64_t{0},
            max_value);
        g_stamina_ab00_virtual_value.store(value, std::memory_order_release);
        g_stamina_ab00_virtual_tick.store(now, std::memory_order_release);
    }

    if (value >= max_value) {
        ClearPlayerStaminaAb00VirtualState();
    }

    *advanced_value = value;
    return true;
}

bool WasRecentConsumption(const uintptr_t entry,
                          const DWORD window_ms,
                          const std::atomic<uintptr_t>& last_entry_ref,
                          const std::atomic<DWORD>& last_tick_ref) {
    if (entry < kMinimumPointerAddress || window_ms == 0) {
        return false;
    }

    const uintptr_t last_entry = last_entry_ref.load(std::memory_order_acquire);
    const DWORD last_tick = last_tick_ref.load(std::memory_order_acquire);
    return last_entry == entry && last_tick != 0 && GetTickCount() - last_tick <= window_ms;
}

uintptr_t GetRecentPlayerSpiritConsumptionEntryInternal(const uint32_t window_ms) {
    if (window_ms == 0) {
        return 0;
    }

    const uintptr_t entry = g_last_player_spirit_consume_entry.load(std::memory_order_acquire);
    const DWORD tick = g_last_player_spirit_consume_tick.load(std::memory_order_acquire);
    if (entry < kMinimumPointerAddress || tick == 0 || GetTickCount() - tick > window_ms) {
        return 0;
    }

    return entry;
}

bool WasRecentPlayerSpiritConsumptionInternal(const uintptr_t entry, const uint32_t window_ms) {
    return WasRecentConsumption(entry, window_ms, g_last_player_spirit_consume_entry, g_last_player_spirit_consume_tick);
}

bool WasRecentPlayerStaminaConsumptionInternal(const uintptr_t entry, const uint32_t window_ms) {
    return WasRecentConsumption(entry, window_ms, g_last_player_stamina_consume_entry, g_last_player_stamina_consume_tick);
}

bool IsSpiritRecoveryLockoutActive(const ModConfig& config,
                                   const uintptr_t entry,
                                   DWORD* const elapsed_ms) {
    if (entry < kMinimumPointerAddress) {
        return false;
    }

    if (IsFocusModeActive()) {
        return false;
    }

    const uintptr_t last_entry = g_last_player_spirit_consume_entry.load(std::memory_order_acquire);
    const DWORD last_tick = g_last_player_spirit_consume_tick.load(std::memory_order_acquire);
    if (last_entry != entry || last_tick == 0) {
        return false;
    }

    const DWORD elapsed = GetTickCount() - last_tick;
    if (elapsed_ms != nullptr) {
        *elapsed_ms = elapsed;
    }

    const DWORD configured_lockout = config.spirit.recovery_lockout_ms;
    return configured_lockout > 0 && elapsed <= configured_lockout;
}

bool IsPassiveRecoveryTick(const int64_t delta, const int64_t max_value) {
    if (delta <= 0 || max_value <= 0) {
        return false;
    }

    const int64_t passive_ceiling = std::max<int64_t>(1000, (max_value * kPassiveRegenMaxPermille) / 1000);
    return delta <= passive_ceiling;
}

bool ShouldBoostSpiritRecoveryDelta(const ModConfig& config,
                                    const uintptr_t entry,
                                    const int64_t delta,
                                    const int64_t max_value) {
    if (IsFocusModeActive() || IsPassiveRecoveryTick(delta, max_value)) {
        return true;
    }

    static_cast<void>(config);
    static_cast<void>(entry);
    return false;
}

bool ShouldBoostStaminaRecoveryDelta(const int64_t delta, const int64_t max_value) {
    return delta > 0 && max_value > 0;
}

int64_t ScaleRecoveryDelta(const int64_t delta, const double multiplier) {
    if (multiplier <= 0.0) {
        return delta;
    }
    return ScaleDelta(delta, multiplier);
}

void LogPlayerNearbyStatProfile(const uintptr_t health_entry, const char* const reason) {
    if (health_entry < kMinimumPointerAddress) {
        return;
    }

    const auto profile_index = g_player_stat_profile_logs.fetch_add(1, std::memory_order_acq_rel);
    if (profile_index >= 4) {
        return;
    }

    Log("runtime: player stat profile begin reason=%s health=0x%p stamina=0x%p spirit=0x%p",
        reason != nullptr ? reason : "unknown",
        reinterpret_cast<void*>(health_entry),
        reinterpret_cast<void*>(GetTrackedPlayerStaminaEntry()),
        reinterpret_cast<void*>(GetTrackedPlayerSpiritEntry()));

    for (uintptr_t offset = 0; offset <= 0x900; offset += 0x90) {
        const uintptr_t entry = health_entry + offset;
        __try {
            const int32_t stat_type = *reinterpret_cast<const int32_t*>(entry);
            const int64_t current_value = *reinterpret_cast<const int64_t*>(entry + 0x08);
            const int64_t max_value = *reinterpret_cast<const int64_t*>(entry + 0x18);
            if (max_value <= 0 || current_value < 0 || current_value > max_value) {
                continue;
            }

            Log("runtime: player stat profile offset=0x%llX entry=0x%p type=%d current=%lld max=%lld tracked=%d",
                static_cast<unsigned long long>(offset),
                reinterpret_cast<void*>(entry),
                stat_type,
                static_cast<long long>(current_value),
                static_cast<long long>(max_value),
                static_cast<int>(ClassifyTrackedStatEntry(entry)));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    Log("runtime: player stat profile end reason=%s", reason != nullptr ? reason : "unknown");
}

void LogPlayerDenseResourceProfile(const uintptr_t health_entry, const char* const reason) {
    if (health_entry < kMinimumPointerAddress) {
        return;
    }

    const auto profile_index = g_player_stat_dense_profile_logs.fetch_add(1, std::memory_order_acq_rel);
    if (profile_index >= 8) {
        return;
    }

    const uintptr_t tracked_stamina = GetTrackedPlayerStaminaEntry();
    const uintptr_t tracked_spirit = GetTrackedPlayerSpiritEntry();
    Log("runtime: player dense resource profile begin reason=%s health=0x%p stamina=0x%p spirit=0x%p",
        reason != nullptr ? reason : "unknown",
        reinterpret_cast<void*>(health_entry),
        reinterpret_cast<void*>(tracked_stamina),
        reinterpret_cast<void*>(tracked_spirit));

    for (intptr_t signed_offset = -0x400; signed_offset <= 0xA00; signed_offset += 0x10) {
        const uintptr_t entry = static_cast<uintptr_t>(static_cast<intptr_t>(health_entry) + signed_offset);
        if (entry < kMinimumPointerAddress) {
            continue;
        }

        __try {
            const int32_t stat_type = *reinterpret_cast<const int32_t*>(entry);
            if (stat_type != kLegacySpiritId &&
                stat_type != kLegacySpiritId2 &&
                stat_type != kStaminaId &&
                stat_type != kSpiritId) {
                continue;
            }

            const int64_t current_value = *reinterpret_cast<const int64_t*>(entry + 0x08);
            const int64_t max_value = *reinterpret_cast<const int64_t*>(entry + 0x18);
            if (max_value <= 0 ||
                current_value < 0 ||
                current_value > max_value ||
                max_value > 1000000) {
                continue;
            }

            Log("runtime: player dense resource candidate offset=%+lld entry=0x%p type=%d current=%lld max=%lld tracked=%d near_stamina=%lld near_spirit=%lld",
                static_cast<long long>(signed_offset),
                reinterpret_cast<void*>(entry),
                stat_type,
                static_cast<long long>(current_value),
                static_cast<long long>(max_value),
                static_cast<int>(ClassifyTrackedStatEntry(entry)),
                static_cast<long long>(tracked_stamina >= kMinimumPointerAddress
                    ? static_cast<int64_t>(entry) - static_cast<int64_t>(tracked_stamina)
                    : 0),
                static_cast<long long>(tracked_spirit >= kMinimumPointerAddress
                    ? static_cast<int64_t>(entry) - static_cast<int64_t>(tracked_spirit)
                    : 0));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    Log("runtime: player dense resource profile end reason=%s", reason != nullptr ? reason : "unknown");
}

bool TryReadMirrorProbeFloat(const uintptr_t address, float* const value) {
    if (address < kMinimumPointerAddress || value == nullptr) {
        return false;
    }

    __try {
        *value = *reinterpret_cast<const float*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool TryWriteMirrorFloat(const uintptr_t address, const float value) {
    if (address < kMinimumPointerAddress) {
        return false;
    }

    __try {
        *reinterpret_cast<float*>(address) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SyncSpiritHudFloatMirrors(const int64_t primary_value,
                               const int64_t primary_max,
                               const char* const reason) {
    (void)primary_value;
    (void)primary_max;
    (void)reason;
    return false;
}

void LogSpiritMirrorProbe(const char* const reason,
                          const uintptr_t primary_entry,
                          const int64_t primary_value,
                          const int64_t primary_max) {
    (void)reason;
    (void)primary_entry;
    (void)primary_value;
    (void)primary_max;
}

bool TrySyncLegacySpiritMirrorFromPrimary(const uintptr_t primary_entry,
                                          const int64_t primary_value,
                                          const int64_t primary_max,
                                          const char* const reason) {
    LogSpiritMirrorProbe(reason, primary_entry, primary_value, primary_max);
    const ActorResolveSnapshot player_snapshot = g_player_resolve;
    if (primary_entry != player_snapshot.spirit_entry ||
        player_snapshot.health_entry < kMinimumPointerAddress ||
        primary_max <= 0 ||
        primary_value < 0 ||
        primary_value > primary_max) {
        return false;
    }

    const uintptr_t legacy_entry = player_snapshot.health_entry + kLegacySpiritEntryOffsetFromHealth;
    if (legacy_entry == primary_entry || legacy_entry < kMinimumPointerAddress) {
        return false;
    }

    __try {
        const int32_t legacy_type = *reinterpret_cast<const int32_t*>(legacy_entry);
        if (legacy_type != kLegacySpiritId && legacy_type != kLegacySpiritId2) {
            return false;
        }

        const int64_t legacy_old = *reinterpret_cast<const int64_t*>(legacy_entry + 0x08);
        const int64_t legacy_max = *reinterpret_cast<const int64_t*>(legacy_entry + 0x18);
        if (legacy_max <= 0 || legacy_old < 0 || legacy_old > legacy_max || legacy_max > 250000) {
            return false;
        }

        const int64_t legacy_target = ClampToRange((primary_value * legacy_max) / primary_max, 0, legacy_max);
        if (legacy_target == legacy_old) {
            SyncSpiritHudFloatMirrors(primary_value, primary_max, reason);
            const auto log_index = g_legacy_spirit_mirror_apply_logs.fetch_add(1, std::memory_order_acq_rel);
            if (log_index < 48) {
                Log("runtime: legacy spirit mirror already synced reason=%s primary=0x%p legacy=0x%p type=%d value=%lld max=%lld primary_value=%lld primary_max=%lld",
                    reason != nullptr ? reason : "unknown",
                    reinterpret_cast<void*>(primary_entry),
                    reinterpret_cast<void*>(legacy_entry),
                    legacy_type,
                    static_cast<long long>(legacy_old),
                    static_cast<long long>(legacy_max),
                    static_cast<long long>(primary_value),
                    static_cast<long long>(primary_max));
            }
            return false;
        }

        *reinterpret_cast<int64_t*>(legacy_entry + 0x08) = legacy_target;
        SyncSpiritHudFloatMirrors(primary_value, primary_max, reason);
        const auto log_index = g_legacy_spirit_mirror_apply_logs.fetch_add(1, std::memory_order_acq_rel);
        if (log_index < 48) {
            Log("runtime: synced legacy spirit mirror reason=%s primary=0x%p legacy=0x%p type=%d old=%lld final=%lld max=%lld primary_value=%lld primary_max=%lld",
                reason != nullptr ? reason : "unknown",
                reinterpret_cast<void*>(primary_entry),
                reinterpret_cast<void*>(legacy_entry),
                legacy_type,
                static_cast<long long>(legacy_old),
                static_cast<long long>(legacy_target),
                static_cast<long long>(legacy_max),
                static_cast<long long>(primary_value),
                static_cast<long long>(primary_max));
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

int64_t ClampMountLockValue(const int64_t requested_value, const int64_t max_value) {
    if (max_value <= 0) {
        return 0;
    }

    if (requested_value <= 0) {
        return max_value;
    }

    return requested_value > max_value ? max_value : requested_value;
}

bool IsSpecialMountStaminaEntry(const uintptr_t entry, const int64_t max_value) {
    const ActorResolveSnapshot mount_snapshot = g_mount_resolve;
    if (entry >= kMinimumPointerAddress &&
        mount_snapshot.valid() &&
        g_mount_track_confidence.load(std::memory_order_acquire) >= 3 &&
        mount_snapshot.stamina_entry == entry &&
        max_value >= kSpecialMountStaminaMinMax &&
        max_value <= kSpecialMountStaminaMaxMax) {
        return true;
    }

    return false;
}

bool IsCurrentMountSpecialProfile() {
    const ActorResolveSnapshot mount_snapshot = g_mount_resolve;
    if (!mount_snapshot.valid() ||
        g_mount_track_confidence.load(std::memory_order_acquire) < 3 ||
        mount_snapshot.stamina_entry < kMinimumPointerAddress) {
        return false;
    }

    int64_t current_value = 0;
    int64_t max_value = 0;
    return TryReadStatEntryValues(mount_snapshot.stamina_entry, kStaminaId, &current_value, &max_value) &&
           max_value >= kSpecialMountStaminaMinMax &&
           max_value <= kSpecialMountStaminaMaxMax;
}

bool IsSpecialMountHealthEntry(const uintptr_t entry) {
    const ActorResolveSnapshot mount_snapshot = g_mount_resolve;
    if (entry < kMinimumPointerAddress ||
        !mount_snapshot.valid() ||
        g_mount_track_confidence.load(std::memory_order_acquire) < 3 ||
        mount_snapshot.health_entry != entry ||
        mount_snapshot.stamina_entry < kMinimumPointerAddress) {
        return false;
    }

    int64_t current_value = 0;
    int64_t max_value = 0;
    return TryReadStatEntryValues(mount_snapshot.stamina_entry, kStaminaId, &current_value, &max_value) &&
           max_value >= kSpecialMountStaminaMinMax &&
           max_value <= kSpecialMountStaminaMaxMax;
}

bool IsSpecialMountHealthCandidate(const ModConfig& config, const uintptr_t entry, const int64_t max_value) {
    if (!config.mount.special_enabled ||
        !config.mount.lock_special_health ||
        !config.mount.lock_special_health_candidates ||
        entry < kMinimumPointerAddress ||
        max_value < kGroundMountHealthCandidateMinMax ||
        max_value > kSpecialMountHealthCandidateMaxMax ||
        entry == GetTrackedPlayerHealthEntry()) {
        return false;
    }

    return IsCurrentMountSpecialProfile();
}

bool LooksLikePlayerCombatStaminaEntry(const uintptr_t entry, const int64_t max_value) {
    if (entry < kMinimumPointerAddress ||
        max_value < 400000 ||
        max_value > 500000) {
        return false;
    }

    const uintptr_t adjacent_spirit =
        entry + (kSpiritEntryOffsetFromHealth - kStaminaEntryOffsetFromHealth);
    int64_t spirit_current = 0;
    int64_t spirit_max = 0;
    return TryReadStatEntryValues(adjacent_spirit, kSpiritId, &spirit_current, &spirit_max) &&
           spirit_max >= 100000 &&
           spirit_max <= 220000;
}

bool ShouldLockMountStaminaEntry(const ModConfig& config,
                                 const uintptr_t entry,
                                 const int64_t max_value,
                                 int64_t* const lock_value,
                                 const char** const profile_name) {
    if (lock_value == nullptr || profile_name == nullptr || !config.mount.enabled) {
        return false;
    }

    if (IsSpecialMountStaminaEntry(entry, max_value)) {
        *profile_name = "special";
        if (!config.mount.special_enabled || !config.mount.lock_special_stamina) {
            return false;
        }

        *lock_value = config.mount.special_lock_value;
        return true;
    }

    *profile_name = "normal";
    if (LooksLikePlayerCombatStaminaEntry(entry, max_value)) {
        const auto current = g_mount_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 32) {
            Log("runtime: rejected mount stamina lock for player-shaped combat resource entry=0x%p max=%lld",
                reinterpret_cast<void*>(entry),
                static_cast<long long>(max_value));
        }
        return false;
    }

    if (!config.mount.lock_stamina) {
        return false;
    }

    *lock_value = config.mount.lock_value;
    return true;
}

std::atomic<std::uint32_t> g_outgoing_stat_write_logs{0};



double EffectiveIncomingResistanceMultiplier(const ModConfig& config) {
    if (!config.resistance.enabled) {
        return 1.0;
    }

    const double effective_resistance = std::max(
        std::max(config.resistance.fire_resistance, config.resistance.ice_resistance),
        config.resistance.electricity_resistance);

    if (effective_resistance <= 0.0) {
        return 1.0;
    }

    if (effective_resistance >= 0.99) {
        return 0.01;
    }

    return 1.0 - effective_resistance;
}

bool ApplyIncomingResistanceToStatConfig(const ModConfig& config,
                                         const int64_t old_value,
                                         const int64_t requested_value,
                                         StatConfig* const stat_config) {
    if (stat_config == nullptr || requested_value >= old_value) {
        return false;
    }

    const double multiplier = EffectiveIncomingResistanceMultiplier(config);
    if (multiplier == 1.0) {
        return false;
    }

    stat_config->consumption_multiplier *= multiplier;

    const auto current = g_resistance_apply_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 32) {
        Log("runtime: applied incoming resistance old=%lld requested=%lld resistance_multiplier=%.3f final_consumption_multiplier=%.6f fire=%.3f ice=%.3f electricity=%.3f",
            static_cast<long long>(old_value),
            static_cast<long long>(requested_value),
            multiplier,
            stat_config->consumption_multiplier,
            config.resistance.fire_resistance,
            config.resistance.ice_resistance,
            config.resistance.electricity_resistance);
    }

    return true;
}

bool TryAdjustMountHealthWrite(const ModConfig& config,
                               const uintptr_t entry,
                               const TrackedStatEntryKind entry_kind,
                               const int32_t actual_type,
                               const int64_t old_value,
                               const int64_t requested_value,
                               const int64_t max_value,
                               int64_t* const value) {
    if (value == nullptr ||
        !config.mount.enabled ||
        actual_type != kHealthId ||
        entry_kind == TrackedStatEntryKind::PlayerHealth ||
        entry == GetTrackedPlayerHealthEntry() ||
        requested_value >= old_value ||
        old_value < 0 ||
        old_value > max_value ||
        requested_value < 0) {
        return false;
    }

    // Only lock health after the resolver has explicitly identified this exact
    // stat entry as the tracked mount. Bosses and large enemies can share the
    // old dragon-like max range, so untracked "large mount-like" health writes
    // are not safe to force back to full.
    const bool tracked_mount_health = entry_kind == TrackedStatEntryKind::MountHealth;
    const bool special_health_candidate = !tracked_mount_health &&
        IsSpecialMountHealthCandidate(config, entry, max_value);
    if (!tracked_mount_health && !special_health_candidate) {
        if (config.mount.enabled &&
            max_value >= kGroundMountHealthCandidateMinMax &&
            max_value <= kGroundMountHealthCandidateMaxMax) {
            const auto current = g_mount_candidate_health_logs.fetch_add(1, std::memory_order_acq_rel);
            if (current < 32) {
                Log("runtime: observed non-player health candidate entry=0x%p old=%lld requested=%lld max=%lld mount_tracked=%d note=not-locked-without-mount-context",
                    reinterpret_cast<void*>(entry),
                    static_cast<long long>(old_value),
                    static_cast<long long>(requested_value),
                    static_cast<long long>(max_value),
                    g_mount_resolve.health_entry == entry ? 1 : 0);
            }
        }
        return false;
    }

    const bool special_profile = special_health_candidate || IsSpecialMountHealthEntry(entry);
    if (special_profile) {
        if (!config.mount.special_enabled || !config.mount.lock_special_health) {
            return false;
        }
    } else if (!config.mount.lock_health) {
        return false;
    }

    const int64_t requested_lock_value = special_profile ? config.mount.special_lock_value : config.mount.lock_value;
    const int64_t locked_value = ClampMountLockValue(requested_lock_value, max_value);
    const int64_t adjusted_value = ClampToRange(locked_value, requested_value, max_value);
    if (adjusted_value == requested_value) {
        return false;
    }

    *value = adjusted_value;

    const auto current = g_mount_stat_write_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 32) {
        Log("runtime: locked mount health write profile=%s entry=0x%p old=%lld requested=%lld final=%lld max=%lld tracked=%d candidate=%d",
            special_profile ? "special" : "normal",
            reinterpret_cast<void*>(entry),
            static_cast<long long>(old_value),
            static_cast<long long>(requested_value),
            static_cast<long long>(adjusted_value),
            static_cast<long long>(max_value),
            tracked_mount_health ? 1 : 0,
            special_health_candidate ? 1 : 0);
    }

    return true;
}

bool TryAdjustMountStaminaDeltaFallback(const ModConfig& config,
                                        const uintptr_t entry,
                                        const TrackedStatEntryKind entry_kind,
                                        const int32_t actual_type,
                                        int64_t* const delta) {
    if (delta == nullptr ||
        entry_kind != TrackedStatEntryKind::MountStamina ||
        entry_kind == TrackedStatEntryKind::PlayerStamina ||
        !IsStaminaStatId(actual_type) ||
        *delta >= 0) {
        return false;
    }

    int64_t current_value = 0;
    int64_t max_value = 0;
    if (!TryReadStatEntryValues(entry, kStaminaId, &current_value, &max_value) ||
        max_value < kSpecialMountStaminaMinMax) {
        return false;
    }

    const char* profile_name = "unknown";
    int64_t requested_lock_value = 0;
    if (!ShouldLockMountStaminaEntry(config, entry, max_value, &requested_lock_value, &profile_name)) {
        return false;
    }

    const int64_t original_delta = *delta;
    const int64_t locked_value = ClampMountLockValue(requested_lock_value, max_value);
    *delta = locked_value - current_value;

    const auto current = g_mount_stat_write_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 32) {
        Log("runtime: locked mount-like stamina delta profile=%s entry=0x%p old_delta=%lld current=%lld lock=%lld max=%lld final_delta=%lld",
            profile_name,
            reinterpret_cast<void*>(entry),
            static_cast<long long>(original_delta),
            static_cast<long long>(current_value),
            static_cast<long long>(locked_value),
            static_cast<long long>(max_value),
            static_cast<long long>(*delta));
    }

    return true;
}

bool TryAdjustOutgoingHealthWrite(const ModConfig& config,
                                  const uintptr_t entry,
                                  const TrackedStatEntryKind entry_kind,
                                  const int32_t actual_type,
                                  const int64_t old_value,
                                  const int64_t requested_value,
                                  const int64_t max_value,
                                  int64_t* const value) {
    if (value == nullptr ||
        !config.damage.outgoing.enabled ||
        !config.damage.outgoing.stat_write_fallback ||
        config.damage.outgoing.multiplier == 1.0 ||
        entry_kind != TrackedStatEntryKind::None ||
        actual_type != kHealthId ||
        requested_value >= old_value ||
        max_value <= 0 ||
        old_value < 0 ||
        old_value > max_value ||
        requested_value < 0) {
        return false;
    }

    const uintptr_t player_health = GetTrackedPlayerHealthEntry();
    if (player_health < kMinimumPointerAddress || entry == player_health) {
        return false;
    }

    int64_t player_current = 0;
    int64_t player_max = 0;
    if (!TryReadStatEntryValues(player_health, kHealthId, &player_current, &player_max)) {
        return false;
    }

    const int64_t original_damage = old_value - requested_value;
    if (original_damage <= 0) {
        return false;
    }

    int64_t scaled_damage = ScaleDelta(original_damage, config.damage.outgoing.multiplier);
    if (scaled_damage == 0 && config.damage.outgoing.multiplier > 0.0) {
        scaled_damage = 1;
    }

    const int64_t adjusted_value = ClampToRange(old_value - scaled_damage, 0, max_value);
    if (adjusted_value == requested_value) {
        return false;
    }

    *value = adjusted_value;

    const auto current = g_outgoing_stat_write_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 48) {
        Log("runtime: adjusted outgoing stat write entry=0x%p old=%lld requested=%lld damage=%lld final=%lld max=%lld player_health=0x%p player_max=%lld multiplier=%.3f",
            reinterpret_cast<void*>(entry),
            static_cast<long long>(old_value),
            static_cast<long long>(requested_value),
            static_cast<long long>(original_damage),
            static_cast<long long>(adjusted_value),
            static_cast<long long>(max_value),
            reinterpret_cast<void*>(player_health),
            static_cast<long long>(player_max),
            config.damage.outgoing.multiplier);
    }

    return true;
}

const StatConfig& PlayerStaminaConfigForEntry(const ModConfig& config, const TrackedStatEntryKind entry_kind) {
    static_cast<void>(entry_kind);
    return config.stamina;
}

const StatConfig& PlayerSpiritConfigForEntry(const ModConfig& config, const TrackedStatEntryKind entry_kind) {
    static_cast<void>(entry_kind);
    return config.spirit;
}

const char* PlayerStaminaNameForEntry(const ModConfig& config, const TrackedStatEntryKind entry_kind) {
    static_cast<void>(config);
    static_cast<void>(entry_kind);
    return "stamina";
}

const char* PlayerSpiritNameForEntry(const ModConfig& config, const TrackedStatEntryKind entry_kind) {
    static_cast<void>(config);
    static_cast<void>(entry_kind);
    return "spirit";
}

bool TryAdjustPlayerStaminaDelta(const ModConfig& config,
                                 const uintptr_t entry,
                                 const TrackedStatEntryKind entry_kind,
                                 const int32_t actual_type,
                                 int64_t* const delta) {
    if (delta == nullptr ||
        entry_kind != TrackedStatEntryKind::PlayerStamina ||
        !IsStaminaStatId(actual_type) ||
        *delta == 0) {
        return false;
    }

    int64_t current_value = 0;
    int64_t max_value = 0;
    if (!TryReadStatEntryValues(entry, kStaminaId, &current_value, &max_value)) {
        return false;
    }

    const StatConfig& stat_config = PlayerStaminaConfigForEntry(config, entry_kind);
    const int64_t original_delta = *delta;
    if (original_delta > 0 && !ShouldBoostStaminaRecoveryDelta(original_delta, max_value)) {
        return false;
    }

    if (original_delta > 0 &&
        WasRecentPlayerStaminaConsumptionInternal(entry, kRecoveryBoostAfterCostDelayMs)) {
        return false;
    }

    if (original_delta < 0 &&
        config.advanced.redirect_large_stamina_costs_to_spirit &&
        config.advanced.suppress_redirected_stamina_cost &&
        -original_delta >= config.advanced.spirit_stamina_redirect_min_cost) {
        const auto current = g_process_apply_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 24) {
            Log("runtime: ignored legacy stamina-to-spirit suppression for tracked player stamina entry=0x%p old=%lld current=%lld max=%lld min_cost=%lld",
                reinterpret_cast<void*>(entry),
                static_cast<long long>(original_delta),
                static_cast<long long>(current_value),
                static_cast<long long>(max_value),
                static_cast<long long>(config.advanced.spirit_stamina_redirect_min_cost));
        }
    }

    int64_t adjusted_delta = original_delta;
    if (original_delta < 0) {
        RecordPlayerStaminaConsumption(entry);
        adjusted_delta = -ScaleDelta(-original_delta, stat_config.consumption_multiplier);
    } else if (ShouldBoostStaminaRecoveryDelta(original_delta, max_value)) {
        adjusted_delta = ScaleRecoveryDelta(original_delta, stat_config.heal_multiplier);
    } else {
        return false;
    }

    adjusted_delta = ClampToRange(current_value + adjusted_delta, 0, max_value) - current_value;

    if (adjusted_delta == original_delta) {
        const auto current = g_stamina_spirit_diag_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 32) {
            Log("runtime: stamina delta observed no-op entry=0x%p old=%lld current=%lld max=%lld consumption=%.3f heal=%.3f note=scaled-same",
                reinterpret_cast<void*>(entry),
                static_cast<long long>(original_delta),
                static_cast<long long>(current_value),
                static_cast<long long>(max_value),
                stat_config.consumption_multiplier,
                stat_config.heal_multiplier);
        }
        return false;
    }

    *delta = adjusted_delta;

    const auto current = g_process_apply_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 24) {
        Log("runtime: adjusted stamina delta entry=0x%p old=%lld final=%lld current=%lld max=%lld",
            reinterpret_cast<void*>(entry),
            static_cast<long long>(original_delta),
            static_cast<long long>(adjusted_delta),
            static_cast<long long>(current_value),
            static_cast<long long>(max_value));
    }

    return true;
}

bool TryRedirectStaminaCostToSpiritEntry(const ModConfig& config,
                                         const uintptr_t stamina_entry,
                                         const uintptr_t spirit_entry,
                                         const int64_t original_delta,
                                         const bool allow_suppress,
                                         int64_t* const delta,
                                         const char* const reason) {
    if (delta == nullptr ||
        !config.advanced.redirect_large_stamina_costs_to_spirit ||
        !config.spirit.enabled ||
        original_delta >= 0 ||
        -original_delta < config.advanced.spirit_stamina_redirect_min_cost ||
        spirit_entry < kMinimumPointerAddress) {
        return false;
    }

    int64_t spirit_current = 0;
    int64_t spirit_max = 0;
    if (!TryReadStatEntryValues(spirit_entry, kSpiritId, &spirit_current, &spirit_max)) {
        return false;
    }

    int64_t redirected_cost = ScaleDelta(-original_delta, config.advanced.spirit_stamina_redirect_scale);
    redirected_cost = ScaleDelta(redirected_cost, config.spirit.consumption_multiplier);
    const int64_t reserve_floor = kPlayerSpiritMinimumFloor;
    const int64_t final_spirit = ClampToRange(spirit_current - redirected_cost, reserve_floor, spirit_max);

    __try {
        *reinterpret_cast<int64_t*>(spirit_entry + 0x08) = final_spirit;
        RecordSpiritMirrorWrite(spirit_entry, final_spirit);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }

    const auto current = g_spirit_mirror_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 64) {
        Log("runtime: redirected combat stamina cost to spirit reason=%s stamina_entry=0x%p spirit_entry=0x%p stamina_delta=%lld redirected_cost=%lld spirit_old=%lld spirit_final=%lld spirit_max=%lld reserve_floor=%lld suppress_stamina=%d scale=%.3f consumption=%.3f",
            reason != nullptr ? reason : "unknown",
            reinterpret_cast<void*>(stamina_entry),
            reinterpret_cast<void*>(spirit_entry),
            static_cast<long long>(original_delta),
            static_cast<long long>(redirected_cost),
            static_cast<long long>(spirit_current),
            static_cast<long long>(final_spirit),
            static_cast<long long>(spirit_max),
            static_cast<long long>(reserve_floor),
            allow_suppress && config.advanced.suppress_redirected_stamina_cost ? 1 : 0,
            config.advanced.spirit_stamina_redirect_scale,
            config.spirit.consumption_multiplier);
    }

    if (allow_suppress && config.advanced.suppress_redirected_stamina_cost) {
        *delta = 0;
    }
    return true;
}

bool TryRedirectCombatStaminaCostToAdjacentSpiritImpl(const ModConfig& config,
                                                      const uintptr_t entry,
                                                      const int32_t actual_type,
                                                      int64_t* const delta) {
    if (delta == nullptr || !IsStaminaStatId(actual_type) || *delta >= 0) {
        return false;
    }

    // Do not let untracked stamina/spirit lookalikes rewrite player state.
    // Boss rematches and rideable proxies can expose many 440000/170000 stat
    // pairs that match the old heuristic but are not the visible player bars.
    if (entry != GetTrackedPlayerStaminaEntry()) {
        return false;
    }

    int64_t current_value = 0;
    int64_t max_value = 0;
    if (!TryReadStatEntryValues(entry, kStaminaId, &current_value, &max_value) ||
        max_value < 400000 ||
        max_value > 500000) {
        return false;
    }

    const uintptr_t adjacent_spirit = entry + (kSpiritEntryOffsetFromHealth - kStaminaEntryOffsetFromHealth);
    int64_t adjacent_current = 0;
    int64_t adjacent_max = 0;
    if (!TryReadStatEntryValues(adjacent_spirit, kSpiritId, &adjacent_current, &adjacent_max) ||
        adjacent_max < 100000 ||
        adjacent_max > 220000) {
        return false;
    }

    const int64_t original_delta = *delta;
    if (!TryRedirectStaminaCostToSpiritEntry(config,
                                             entry,
                                             adjacent_spirit,
                                             original_delta,
                                             true,
                                             delta,
                                             "adjacent-combat-resource")) {
        return false;
    }

    std::lock_guard lock(g_state_mutex);
    g_player_resolve.stamina_entry = entry;
    g_player_resolve.spirit_entry = adjacent_spirit;
    return true;
}

bool TryAdoptAdjacentCombatSpiritAsPlayer(const uintptr_t spirit_entry) {
    if (spirit_entry < kMinimumPointerAddress ||
        spirit_entry < (kSpiritEntryOffsetFromHealth - kStaminaEntryOffsetFromHealth)) {
        return false;
    }

    int64_t spirit_current = 0;
    int64_t spirit_max = 0;
    if (!TryReadStatEntryValues(spirit_entry, kSpiritId, &spirit_current, &spirit_max) ||
        spirit_max < 100000 ||
        spirit_max > 220000) {
        return false;
    }

    const uintptr_t stamina_entry = spirit_entry - (kSpiritEntryOffsetFromHealth - kStaminaEntryOffsetFromHealth);
    int64_t stamina_current = 0;
    int64_t stamina_max = 0;
    if (!TryReadStatEntryValues(stamina_entry, kStaminaId, &stamina_current, &stamina_max) ||
        stamina_max < 400000 ||
        stamina_max > 500000) {
        return false;
    }

    {
        std::lock_guard lock(g_state_mutex);
        if (g_player_resolve.health_entry < kMinimumPointerAddress ||
            spirit_entry < g_player_resolve.health_entry ||
            spirit_entry > g_player_resolve.health_entry + 0x900 ||
            stamina_entry < g_player_resolve.health_entry ||
            stamina_entry > g_player_resolve.health_entry + 0x900) {
            return false;
        }

        g_player_resolve.stamina_entry = stamina_entry;
        g_player_resolve.spirit_entry = spirit_entry;
    }

    const auto current = g_discovery_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 24) {
        Log("runtime: adopted adjacent combat spirit as player spirit=0x%p stamina=0x%p spirit_current=%lld spirit_max=%lld stamina_current=%lld stamina_max=%lld",
            reinterpret_cast<void*>(spirit_entry),
            reinterpret_cast<void*>(stamina_entry),
            static_cast<long long>(spirit_current),
            static_cast<long long>(spirit_max),
            static_cast<long long>(stamina_current),
            static_cast<long long>(stamina_max));
    }

    return true;
}

bool TryAdjustMountStaminaDelta(const ModConfig& config,
                                const uintptr_t entry,
                                const TrackedStatEntryKind entry_kind,
                                const int32_t actual_type,
                                int64_t* const delta) {
    if (delta == nullptr ||
        entry_kind != TrackedStatEntryKind::MountStamina ||
        !IsStaminaStatId(actual_type) ||
        *delta >= 0) {
        return false;
    }

    const int64_t original_delta = *delta;
    int64_t current_value = 0;
    int64_t max_value = 0;
    bool used_lock_value = false;
    int64_t locked_value = 0;
    const char* profile_name = "unknown";

    if (TryReadStatEntryValues(entry, kStaminaId, &current_value, &max_value)) {
        int64_t requested_lock_value = 0;
        if (!ShouldLockMountStaminaEntry(config, entry, max_value, &requested_lock_value, &profile_name)) {
            return false;
        }

        locked_value = ClampMountLockValue(requested_lock_value, max_value);
        *delta = locked_value - current_value;
        used_lock_value = true;
    } else {
        if (!config.mount.enabled || !config.mount.lock_stamina) {
            return false;
        }
        *delta = -*delta;
    }

    const auto current = g_mount_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 32) {
        const ActorResolveSnapshot mount_snapshot = g_mount_resolve;
        if (used_lock_value) {
            Log("runtime: locked mount stamina profile=%s root=0x%p entry=0x%p old_delta=%lld current=%lld lock=%lld max=%lld final_delta=%lld",
                profile_name,
                reinterpret_cast<void*>(mount_snapshot.root),
                reinterpret_cast<void*>(mount_snapshot.stamina_entry),
                static_cast<long long>(original_delta),
                static_cast<long long>(current_value),
                static_cast<long long>(locked_value),
                static_cast<long long>(max_value),
                static_cast<long long>(*delta));
        } else {
            Log("runtime: locked mount stamina root=0x%p entry=0x%p old_delta=%lld final_delta=%lld fallback=invert",
                reinterpret_cast<void*>(mount_snapshot.root),
                reinterpret_cast<void*>(mount_snapshot.stamina_entry),
                static_cast<long long>(original_delta),
                static_cast<long long>(*delta));
        }
    }

    return true;
}

bool TryAdjustPlayerSpiritDelta(const ModConfig& config,
                                const uintptr_t entry,
                                const TrackedStatEntryKind entry_kind,
                                const int32_t actual_type,
                                int64_t* const delta) {
    if (delta == nullptr ||
        entry_kind != TrackedStatEntryKind::PlayerSpirit ||
        !IsSpiritStatId(actual_type) ||
        *delta == 0) {
        return false;
    }

    const int64_t current_value = *reinterpret_cast<const int64_t*>(entry + 0x08);
    const int64_t max_value = *reinterpret_cast<const int64_t*>(entry + 0x18);
    if (max_value <= 0 || current_value < 0 || current_value > max_value) {
        return false;
    }

    const StatConfig& stat_config = PlayerSpiritConfigForEntry(config, entry_kind);

    const int64_t original_delta = *delta;
    if (original_delta < 0) {
        if (IsFocusModeActive()) {
            ClearFocusRecoveryWindow("direct-spirit-cost");
            const auto current = g_stamina_spirit_diag_logs.fetch_add(1, std::memory_order_acq_rel);
            if (current < 64) {
                Log("runtime: focus-mode cancelled by spirit delta consumption entry=0x%p old=%lld current=%lld max=%lld",
                    reinterpret_cast<void*>(entry),
                    static_cast<long long>(original_delta),
                    static_cast<long long>(current_value),
                    static_cast<long long>(max_value));
            }
        }
        RecordPlayerSpiritConsumption(entry);
        LogPlayerDenseResourceProfile(g_player_resolve.health_entry, "direct-spirit-cost");
    }

    if (original_delta < 0) {
        const int64_t consumed = -original_delta;
        const int64_t target_cost = ScaleDelta(consumed, stat_config.consumption_multiplier);
        const int64_t reserve_floor = kPlayerSpiritMinimumFloor;
        const int64_t direct_delta = ClampToRange(current_value - target_cost, reserve_floor, max_value) - current_value;
        if (direct_delta != original_delta) {
            *delta = direct_delta;
            SyncPlayerSpiritVisualMirror(entry,
                                         ClampToRange(current_value + direct_delta, 0, max_value),
                                         max_value,
                                         "spirit-delta-cost");
            const auto current = g_process_apply_logs.fetch_add(1, std::memory_order_acq_rel);
            if (current < 32) {
                Log("runtime: scaled direct spirit cost entry=0x%p old=%lld final=%lld current=%lld max=%lld reserve_floor=%lld consumption=%.3f",
                    reinterpret_cast<void*>(entry),
                    static_cast<long long>(original_delta),
                    static_cast<long long>(direct_delta),
                    static_cast<long long>(current_value),
                    static_cast<long long>(max_value),
                    static_cast<long long>(reserve_floor),
                    stat_config.consumption_multiplier);
            }
            return true;
        }
    }

    if (original_delta > 0 &&
        current_value == 0 &&
        original_delta >= max_value / 2) {
        const auto current = g_stamina_spirit_diag_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 32) {
            Log("runtime: accepted spirit delta bootstrap fill entry=0x%p delta=%lld current=%lld max=%lld heal=%.3f",
                reinterpret_cast<void*>(entry),
                static_cast<long long>(original_delta),
                static_cast<long long>(current_value),
                static_cast<long long>(max_value),
                stat_config.heal_multiplier);
        }
        return false;
    }

    int64_t adjusted_delta = original_delta;
    if (original_delta < 0) {
        adjusted_delta = -ScaleDelta(-original_delta, stat_config.consumption_multiplier);
    } else if (ShouldBoostSpiritRecoveryDelta(config, entry, original_delta, max_value)) {
        adjusted_delta = ScaleRecoveryDelta(original_delta, stat_config.heal_multiplier);
    } else {
        return false;
    }

    adjusted_delta = ClampToRange(current_value + adjusted_delta, 0, max_value) - current_value;

    if (adjusted_delta == original_delta) {
        if (original_delta < 0) {
            SyncPlayerSpiritVisualMirror(entry,
                                         ClampToRange(current_value + original_delta, 0, max_value),
                                         max_value,
                                         "spirit-delta-cost-same");
        }
        const auto current = g_stamina_spirit_diag_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 32) {
            Log("runtime: spirit delta observed no-op entry=0x%p old=%lld current=%lld max=%lld consumption=%.3f heal=%.3f note=scaled-same",
                reinterpret_cast<void*>(entry),
                static_cast<long long>(original_delta),
                static_cast<long long>(current_value),
                static_cast<long long>(max_value),
                stat_config.consumption_multiplier,
                stat_config.heal_multiplier);
        }
        return false;
    }

    *delta = adjusted_delta;
    SyncPlayerSpiritVisualMirror(entry,
                                 ClampToRange(current_value + adjusted_delta, 0, max_value),
                                 max_value,
                                 adjusted_delta < 0 ? "spirit-delta-cost" : "spirit-delta-recovery");

    const auto current = g_process_apply_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 24) {
        Log("runtime: adjusted spirit delta entry=0x%p old=%lld final=%lld current=%lld max=%lld",
            reinterpret_cast<void*>(entry),
            static_cast<long long>(original_delta),
            static_cast<long long>(adjusted_delta),
            static_cast<long long>(current_value),
            static_cast<long long>(max_value));
    }

    return true;
}

bool TryAdjustMountSpiritDelta(const ModConfig& config,
                               const uintptr_t entry,
                               const TrackedStatEntryKind entry_kind,
                               const int32_t actual_type,
                               int64_t* const delta) {
    if (delta == nullptr ||
        !config.mount.enabled ||
        entry_kind != TrackedStatEntryKind::MountSpirit ||
        !IsSpiritStatId(actual_type) ||
        *delta >= 0) {
        return false;
    }

    const bool special_profile = IsCurrentMountSpecialProfile();
    if (special_profile) {
        if (!config.mount.special_enabled || !config.mount.lock_special_spirit_stamina) {
            return false;
        }
    } else if (!config.mount.lock_spirit_stamina) {
        return false;
    }

    int64_t current_value = 0;
    int64_t max_value = 0;
    const int64_t original_delta = *delta;
    if (TryReadStatEntryValues(entry, kSpiritId, &current_value, &max_value)) {
        const int64_t lock_value = special_profile ? config.mount.special_lock_value : config.mount.lock_value;
        const int64_t locked_value = ClampMountLockValue(lock_value, max_value);
        *delta = locked_value - current_value;
    } else {
        *delta = 0;
    }

    const auto current = g_mount_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 48) {
        Log("runtime: locked mount spirit-stamina delta profile=%s entry=0x%p old_delta=%lld current=%lld max=%lld final_delta=%lld",
            special_profile ? "special" : "normal",
            reinterpret_cast<void*>(entry),
            static_cast<long long>(original_delta),
            static_cast<long long>(current_value),
            static_cast<long long>(max_value),
            static_cast<long long>(*delta));
    }

    return true;
}

}  // namespace

bool IsPostRematchDetachedPlayerStaminaMirror(const uintptr_t entry) {
    return IsPostRematchDetachedPlayerResourceEntry(entry, kStaminaId);
}

bool WasRecentPlayerSpiritConsumption(const uintptr_t entry, const uint32_t window_ms) {
    return WasRecentPlayerSpiritConsumptionInternal(entry, window_ms);
}

uintptr_t GetRecentPlayerSpiritConsumptionEntry(const uint32_t window_ms) {
    return GetRecentPlayerSpiritConsumptionEntryInternal(window_ms);
}

bool WasRecentPlayerStaminaConsumption(const uintptr_t entry, const uint32_t window_ms) {
    return WasRecentPlayerStaminaConsumptionInternal(entry, window_ms);
}

void RecordPlayerStaminaPreNotifyWrite(const uintptr_t entry, const int64_t final_value) {
    RecordPlayerStaminaPreNotifyWriteInternal(entry, final_value);
}

bool TryConsumePlayerStaminaPreNotifyWrite(const uintptr_t entry,
                                           const int64_t observed_value,
                                           int64_t* const final_value) {
    return TryConsumePlayerStaminaPreNotifyWriteInternal(entry, observed_value, final_value);
}

bool WasRecentPlayerStaminaPreNotifyWrite(const uintptr_t entry, const uint32_t window_ms) {
    return WasRecentConsumption(entry, window_ms, g_last_player_stamina_prenotify_entry, g_last_player_stamina_prenotify_tick);
}

bool TryGetPlayerStaminaAb00VirtualBase(const uintptr_t entry,
                                        const int64_t current_value,
                                        const int64_t max_value,
                                        const ModConfig& config,
                                        int64_t* const base_value) {
    if (base_value == nullptr) {
        return false;
    }

    int64_t advanced_value = 0;
    if (!AdvancePlayerStaminaAb00VirtualState(entry, max_value, config, GetTickCount(), &advanced_value)) {
        return false;
    }

    *base_value = std::clamp(std::min(current_value, advanced_value), int64_t{0}, max_value);
    return true;
}

void RecordPlayerStaminaAb00VirtualCost(const uintptr_t entry,
                                        const int64_t final_value,
                                        const int64_t max_value) {
    if (entry < kMinimumPointerAddress || final_value < 0 || max_value <= 0 || final_value > max_value) {
        return;
    }

    RecordPlayerStaminaConsumption(entry);
    g_stamina_ab00_virtual_entry.store(entry, std::memory_order_release);
    g_stamina_ab00_virtual_value.store(final_value, std::memory_order_release);
    g_stamina_ab00_virtual_max.store(max_value, std::memory_order_release);
    g_stamina_ab00_virtual_tick.store(GetTickCount(), std::memory_order_release);

    const auto log_index = g_stamina_ab00_virtual_logs.fetch_add(1, std::memory_order_acq_rel);
    if (log_index < 96) {
        Log("runtime: stamina-ab00 virtual recorded entry=0x%p final=%lld max=%lld note=d40-virtual-recover",
            reinterpret_cast<void*>(entry),
            static_cast<long long>(final_value),
            static_cast<long long>(max_value));
    }
}

bool TryApplyPlayerStaminaAb00VirtualRecovery(const uintptr_t entry,
                                              const int64_t observed_value,
                                              const int64_t max_value,
                                              const ModConfig& config,
                                              int64_t* const final_value) {
    if (final_value == nullptr || observed_value < 0 || observed_value > max_value) {
        return false;
    }

    int64_t virtual_value = 0;
    if (!AdvancePlayerStaminaAb00VirtualState(entry, max_value, config, GetTickCount(), &virtual_value)) {
        return false;
    }

    if (observed_value <= virtual_value) {
        *final_value = observed_value;
        return false;
    }

    const int64_t reconciled_value = std::clamp(virtual_value, int64_t{0}, max_value);
    *final_value = reconciled_value;
    if (reconciled_value != observed_value) {
        *reinterpret_cast<int64_t*>(entry + 0x08) = reconciled_value;
        RecordPlayerStaminaPreNotifyWriteInternal(entry, reconciled_value);
    }

    const auto log_index = g_stamina_ab00_virtual_logs.fetch_add(1, std::memory_order_acq_rel);
    if (log_index < 128) {
        Log("runtime: stamina-ab00 virtual reconciled entry=0x%p observed=%lld final=%lld max=%lld heal=%.3f note=d40-virtual-recover",
            reinterpret_cast<void*>(entry),
            static_cast<long long>(observed_value),
            static_cast<long long>(reconciled_value),
            static_cast<long long>(max_value),
            config.stamina.heal_multiplier);
    }

    return reconciled_value != observed_value;
}

bool TryConsumeSpiritMirrorWrite(const uintptr_t entry, const int64_t observed_value) {
    constexpr DWORD kMirrorAcceptWindowMs = 750;
    if (entry < kMinimumPointerAddress) {
        return false;
    }

    const uintptr_t mirrored_entry = g_last_spirit_mirror_entry.load(std::memory_order_acquire);
    if (mirrored_entry != entry) {
        return false;
    }

    const int64_t mirrored_value = g_last_spirit_mirror_value.load(std::memory_order_acquire);
    if (mirrored_value != observed_value) {
        return false;
    }

    const DWORD mirrored_tick = g_last_spirit_mirror_tick.load(std::memory_order_acquire);
    if (mirrored_tick == 0 || GetTickCount() - mirrored_tick > kMirrorAcceptWindowMs) {
        return false;
    }

    uintptr_t expected = entry;
    if (!g_last_spirit_mirror_entry.compare_exchange_strong(expected, 0, std::memory_order_acq_rel)) {
        return false;
    }

    return true;
}

void RecordReroutedSpiritDelta(const uintptr_t entry, const int64_t requested_value) {
    if (entry < kMinimumPointerAddress) {
        return;
    }

    RecordPlayerSpiritConsumption(entry);
    g_last_rerouted_spirit_value.store(requested_value, std::memory_order_release);
    g_last_rerouted_spirit_tick.store(GetTickCount(), std::memory_order_release);
    g_last_rerouted_spirit_entry.store(entry, std::memory_order_release);
}

bool SyncPlayerSpiritVisualMirror(const uintptr_t primary_entry,
                                  const int64_t primary_value,
                                  const int64_t primary_max,
                                  const char* const reason) {
    return TrySyncLegacySpiritMirrorFromPrimary(primary_entry, primary_value, primary_max, reason);
}

bool TryConsumeReroutedSpiritStatWrite(const uintptr_t entry, const int64_t requested_value) {
    constexpr DWORD kRerouteAcceptWindowMs = 750;
    if (entry < kMinimumPointerAddress) {
        return false;
    }

    const uintptr_t rerouted_entry = g_last_rerouted_spirit_entry.load(std::memory_order_acquire);
    if (rerouted_entry != entry) {
        return false;
    }

    const int64_t rerouted_value = g_last_rerouted_spirit_value.load(std::memory_order_acquire);
    if (rerouted_value != requested_value) {
        return false;
    }

    const DWORD rerouted_tick = g_last_rerouted_spirit_tick.load(std::memory_order_acquire);
    if (rerouted_tick == 0 || GetTickCount() - rerouted_tick > kRerouteAcceptWindowMs) {
        return false;
    }

    uintptr_t expected = entry;
    if (!g_last_rerouted_spirit_entry.compare_exchange_strong(expected, 0, std::memory_order_acq_rel)) {
        return false;
    }

    return true;
}

void RecordFocusRecoveryWindow() {
    g_last_focus_recovery_tick.store(GetTickCount(), std::memory_order_release);
}

void ClearFocusRecoveryWindow(const char* const reason) {
    g_last_focus_recovery_tick.store(0, std::memory_order_release);
    g_focus_input_was_down.store(false, std::memory_order_release);
    const bool was_active = g_focus_mode_active.exchange(false, std::memory_order_acq_rel);
    if (was_active) {
        const auto log_index = g_focus_mode_state_logs.fetch_add(1, std::memory_order_acq_rel);
        if (log_index < 128) {
            Log("runtime: focus mode cleared reason=%s", reason != nullptr ? reason : "unknown");
        }
    }
}

bool IsFocusModeActive() {
    bool keyboard_focus = false;
    bool left_stick = false;
    bool right_stick = false;
    bool xinput_focus = false;
    const bool focus_input_down = SamplePhysicalFocusInput(&keyboard_focus, &left_stick, &right_stick, &xinput_focus);
    const bool was_down = g_focus_input_was_down.exchange(focus_input_down, std::memory_order_acq_rel);

    if (focus_input_down && !was_down) {
        const bool next_active = !g_focus_mode_active.load(std::memory_order_acquire);
        g_focus_mode_active.store(next_active, std::memory_order_release);
        if (next_active) {
            RecordFocusRecoveryWindow();
        } else {
            g_last_focus_recovery_tick.store(0, std::memory_order_release);
        }

        const auto log_index = g_focus_mode_state_logs.fetch_add(1, std::memory_order_acq_rel);
        if (log_index < 128) {
            Log("runtime: focus mode toggled %s keyboard=%d left=%d right=%d xinput=%d",
                next_active ? "active" : "inactive",
                keyboard_focus ? 1 : 0,
                left_stick ? 1 : 0,
                right_stick ? 1 : 0,
                xinput_focus ? 1 : 0);
        }
    }

    return g_focus_mode_active.load(std::memory_order_acquire);
}

bool IsFocusRecoveryWindowActive() {
    const DWORD now = GetTickCount();
    DWORD focus_window_ms = GetConfig().advanced.focus_recovery_window_ms;
    bool keyboard_focus = false;
    bool left_stick = false;
    bool right_stick = false;
    bool xinput_focus = false;
    const bool mode_active = IsFocusModeActive();
    if (mode_active) {
        SamplePhysicalFocusInput(&keyboard_focus, &left_stick, &right_stick, &xinput_focus);
    }
    bool active = mode_active;
    if (active) {
        RecordFocusRecoveryWindow();
    } else {
        const DWORD last_tick = g_last_focus_recovery_tick.load(std::memory_order_acquire);
        active = last_tick != 0 && now - last_tick <= focus_window_ms;
    }

    const bool previous = g_last_focus_recovery_state.exchange(active, std::memory_order_acq_rel);
    if (previous != active) {
        const DWORD last_tick = g_last_focus_recovery_tick.load(std::memory_order_acquire);
        Log("runtime: focus recovery window %s elapsed_ms=%lu window_ms=%lu keyboard=%d left=%d right=%d xinput=%d",
            active ? "active" : "inactive",
            last_tick != 0 ? static_cast<unsigned long>(now - last_tick) : 0UL,
            static_cast<unsigned long>(focus_window_ms),
            keyboard_focus ? 1 : 0,
            left_stick ? 1 : 0,
            right_stick ? 1 : 0,
            xinput_focus ? 1 : 0);
    }
    return active;
}

bool TryRedirectCombatStaminaCostToAdjacentSpirit(const uintptr_t stamina_entry, int64_t* const delta) {
    if (!g_runtime_enabled.load(std::memory_order_acquire) ||
        delta == nullptr ||
        stamina_entry < kMinimumPointerAddress) {
        return false;
    }

    const auto& config = GetConfig();
    if (!config.general.enabled) {
        return false;
    }

    int32_t actual_type = -1;
    __try {
        actual_type = *reinterpret_cast<const int32_t*>(stamina_entry);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }

    return TryRedirectCombatStaminaCostToAdjacentSpiritImpl(config, stamina_entry, actual_type, delta);
}

void ObserveStatEntry(const uintptr_t entry, const uintptr_t component) {
    if (!g_runtime_enabled.load(std::memory_order_acquire)) {
        return;
    }

    const auto& config = GetConfig();
    if (!config.general.enabled || entry < kMinimumPointerAddress || component < kMinimumPointerAddress) {
        return;
    }

    const auto stat_type = *reinterpret_cast<const int32_t*>(entry);
    if (!IsTrackedStat(stat_type)) {
        return;
    }

    const auto* const current_value_ptr = reinterpret_cast<const int64_t*>(entry + 0x08);
    const auto* const max_value_ptr = reinterpret_cast<const int64_t*>(entry + 0x18);
    const int64_t current_value = *current_value_ptr;
    const int64_t max_value = *max_value_ptr;
    if (max_value <= 0 || current_value < 0 || current_value > max_value) {
        return;
    }

    const auto component_marker = *reinterpret_cast<const uintptr_t*>(component);
    const auto tracked_marker = g_player_resolve.marker;
    if (tracked_marker < kMinimumPointerAddress) {
        if (!TryBootstrapPlayerResolveFromStatComponent(entry, component)) {
            return;
        }

        const auto current = g_discovery_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 16) {
            const ActorResolveSnapshot resolved = g_player_resolve;
                Log("runtime: tracked player via stats marker=0x%p root=0x%p health=0x%p stamina=0x%p spirit=0x%p",
                reinterpret_cast<void*>(resolved.marker),
                reinterpret_cast<void*>(resolved.root),
                reinterpret_cast<void*>(resolved.health_entry),
                reinterpret_cast<void*>(resolved.stamina_entry),
                    reinterpret_cast<void*>(resolved.spirit_entry));
            LogPlayerNearbyStatProfile(resolved.health_entry, "tracked-player-via-stats");
        }
        return;
    }

    if (component_marker != tracked_marker) {
        if (TryRebindTrackedPlayerStatusComponentFromStats(entry, component, "stats-marker-rebind")) {
            return;
        }
        UpdateTrackedMountFromStatComponent(entry, component);
        return;
    }

    const uintptr_t tracked_health = g_player_resolve.health_entry;
    if (tracked_health >= kMinimumPointerAddress &&
        (entry < tracked_health || entry > tracked_health + 0x900)) {
        if (TryRebindTrackedPlayerStatusComponentFromStats(entry, component, "stats-out-of-range-rebind")) {
            return;
        }
        const auto current = g_discovery_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 32) {
            Log("runtime: ignored stale/out-of-range stats component type=%d entry=0x%p tracked_health=0x%p tracked_stamina=0x%p tracked_spirit=0x%p",
                stat_type,
                reinterpret_cast<void*>(entry),
                reinterpret_cast<void*>(tracked_health),
                reinterpret_cast<void*>(g_player_resolve.stamina_entry),
                reinterpret_cast<void*>(g_player_resolve.spirit_entry));
        }
        return;
    }

    std::lock_guard lock(g_state_mutex);
    if (*reinterpret_cast<const uintptr_t*>(component) != g_player_resolve.marker) {
        return;
    }

    if (!TryAssignPlayerResolvedEntry(entry, stat_type)) {
        return;
    }

    const auto current = g_discovery_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 16) {
        Log("runtime: discovered stat entry type=%d entry=0x%p current=%lld max=%lld",
            stat_type,
            reinterpret_cast<void*>(entry),
            static_cast<long long>(current_value),
            static_cast<long long>(max_value));
        if (IsSpiritStatId(stat_type)) {
            LogPlayerNearbyStatProfile(g_player_resolve.health_entry, "spirit-discovered");
        }
    }
}

bool TryAdjustStaminaDelta(const uintptr_t entry, int64_t* const delta) {
    if (!g_runtime_enabled.load(std::memory_order_acquire) ||
        delta == nullptr ||
        entry < kMinimumPointerAddress) {
        return false;
    }

    const auto& config = GetConfig();
    if (!config.general.enabled) {
        return false;
    }

    const int32_t actual_type = *reinterpret_cast<const int32_t*>(entry);
    if (!IsStaminaStatId(actual_type) || *delta == 0) {
        if (config.stamina.enabled) {
            const auto current = g_stamina_spirit_diag_logs.fetch_add(1, std::memory_order_acq_rel);
            if (current < 32) {
                Log("runtime: stamina delta skipped entry=0x%p type=%d delta=%lld reason=%s",
                    reinterpret_cast<void*>(entry),
                    actual_type,
                    delta == nullptr ? 0LL : static_cast<long long>(*delta),
                    !IsStaminaStatId(actual_type) ? "non-stamina-type" : "zero-delta");
            }
        }
        return false;
    }

    TrackedStatEntryKind entry_kind = ClassifyTrackedStatEntry(entry);
    if (entry_kind == TrackedStatEntryKind::None &&
        TryBootstrapPlayerStaminaFromEntry(entry)) {
        entry_kind = ClassifyTrackedStatEntry(entry);

        const auto current = g_discovery_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 16) {
            Log("runtime: tracked player stamina via stamina-delta fallback stamina=0x%p",
                reinterpret_cast<void*>(entry));
        }
    }

    if (entry_kind == TrackedStatEntryKind::None &&
        *delta < 0 &&
        TryPromotePlayerCombatResourcesFromStaminaEntry(entry, "stamina-delta active consumption")) {
        entry_kind = ClassifyTrackedStatEntry(entry);

        const auto current = g_discovery_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 32) {
            const ActorResolveSnapshot resolved = g_player_resolve;
            Log("runtime: tracked player via stamina-delta active consumption health=0x%p stamina=0x%p spirit=0x%p delta=%lld",
                reinterpret_cast<void*>(resolved.health_entry),
                reinterpret_cast<void*>(resolved.stamina_entry),
                reinterpret_cast<void*>(resolved.spirit_entry),
                static_cast<long long>(*delta));
        }
    }

    entry_kind = ClassifyForResourceScaling(entry, entry_kind, actual_type);

    if (TryAdjustMountStaminaDelta(config, entry, entry_kind, actual_type, delta)) {
        return true;
    }

    if (TryAdjustMountStaminaDeltaFallback(config, entry, entry_kind, actual_type, delta)) {
        return true;
    }

    if (entry_kind == TrackedStatEntryKind::PlayerStamina) {
        return TryAdjustPlayerStaminaDelta(config, entry, entry_kind, actual_type, delta);
    }

    if (TryRedirectCombatStaminaCostToAdjacentSpiritImpl(config, entry, actual_type, delta)) {
        return true;
    }

    const bool adjusted = TryAdjustPlayerStaminaDelta(config, entry, entry_kind, actual_type, delta);
    if (!adjusted && config.stamina.enabled) {
        const auto current = g_stamina_spirit_diag_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 32) {
            Log("runtime: stamina delta not adjusted entry=0x%p type=%d kind=%d delta=%lld tracked_stamina=0x%p",
                reinterpret_cast<void*>(entry),
                actual_type,
                static_cast<int>(entry_kind),
                static_cast<long long>(*delta),
                reinterpret_cast<void*>(GetTrackedPlayerStaminaEntry()));
        }
    }
    return adjusted;
}

bool TryAdjustSpiritDelta(const uintptr_t entry, int64_t* const delta) {
    if (!g_runtime_enabled.load(std::memory_order_acquire) ||
        delta == nullptr ||
        entry < kMinimumPointerAddress) {
        return false;
    }

    const auto& config = GetConfig();
    if (!ShouldInstallSpiritHook(config)) {
        return false;
    }

    const int32_t actual_type = *reinterpret_cast<const int32_t*>(entry);
    if (!IsSpiritStatId(actual_type) || *delta == 0) {
        if (config.spirit.enabled) {
            const auto current = g_stamina_spirit_diag_logs.fetch_add(1, std::memory_order_acq_rel);
            if (current < 32) {
                Log("runtime: spirit delta skipped entry=0x%p type=%d delta=%lld reason=%s",
                    reinterpret_cast<void*>(entry),
                    actual_type,
                    static_cast<long long>(*delta),
                    !IsSpiritStatId(actual_type) ? "non-spirit-type" : "zero-delta");
            }
        }
        return false;
    }

    TrackedStatEntryKind entry_kind = ClassifyTrackedStatEntry(entry);
    if (*delta < 0 &&
        entry_kind == TrackedStatEntryKind::MountSpirit &&
        TryAdoptAdjacentCombatSpiritAsPlayer(entry)) {
        entry_kind = TrackedStatEntryKind::PlayerSpirit;
    }

    if (entry_kind == TrackedStatEntryKind::None &&
        IsSpiritStatId(actual_type) &&
        TryBootstrapPlayerSpiritFromEntry(entry)) {
        entry_kind = ClassifyTrackedStatEntry(entry);

        const auto current = g_discovery_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 16) {
            Log("runtime: tracked player spirit via spirit-delta fallback spirit=0x%p",
                reinterpret_cast<void*>(entry));
        }
    }

    if (entry_kind == TrackedStatEntryKind::None &&
        *delta < 0 &&
        TryPromotePlayerCombatResourcesFromSpiritEntry(entry, "spirit-delta active consumption")) {
        entry_kind = ClassifyTrackedStatEntry(entry);

        const auto current = g_discovery_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 32) {
            const ActorResolveSnapshot resolved = g_player_resolve;
            Log("runtime: tracked player via spirit-delta active consumption health=0x%p stamina=0x%p spirit=0x%p delta=%lld",
                reinterpret_cast<void*>(resolved.health_entry),
                reinterpret_cast<void*>(resolved.stamina_entry),
                reinterpret_cast<void*>(resolved.spirit_entry),
                static_cast<long long>(*delta));
        }
    }

    entry_kind = ClassifyForResourceScaling(entry, entry_kind, actual_type);

    if (TryAdjustMountSpiritDelta(config, entry, entry_kind, actual_type, delta)) {
        return true;
    }

    const bool adjusted = TryAdjustPlayerSpiritDelta(config, entry, entry_kind, actual_type, delta);
    if (!adjusted && config.spirit.enabled) {
        const auto current = g_stamina_spirit_diag_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 32) {
            Log("runtime: spirit delta not adjusted entry=0x%p type=%d kind=%d delta=%lld tracked_spirit=0x%p",
                reinterpret_cast<void*>(entry),
                actual_type,
                static_cast<int>(entry_kind),
                static_cast<long long>(*delta),
                reinterpret_cast<void*>(GetTrackedPlayerSpiritEntry()));
        }
    }
    return adjusted;
}

bool TryAdjustStatWrite(const uintptr_t entry, int64_t* const value, const uintptr_t owner_root) {
    if (!g_runtime_enabled.load(std::memory_order_acquire) || value == nullptr) {
        return false;
    }

    const auto& config = GetConfig();
    if (!config.general.enabled || entry < kMinimumPointerAddress) {
        return false;
    }

    const int64_t old_value = *reinterpret_cast<const int64_t*>(entry + 0x08);
    const int64_t max_value = *reinterpret_cast<const int64_t*>(entry + 0x18);
    const int64_t requested_value = *value;
    if (max_value <= 0 || old_value < 0 || old_value > max_value || requested_value < 0) {
        const auto current = g_process_skip_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 24) {
            Log("runtime: write skipped, invalid range entry=0x%p old=%lld new=%lld max=%lld",
                reinterpret_cast<void*>(entry),
                static_cast<long long>(old_value),
                static_cast<long long>(requested_value),
                static_cast<long long>(max_value));
        }
        return false;
    }

    const int32_t actual_type = *reinterpret_cast<const int32_t*>(entry);
    TrackedStatEntryKind entry_kind = ClassifyForResourceScaling(entry, ClassifyTrackedStatEntry(entry), actual_type);
    const int64_t delta = requested_value - old_value;

    if (delta < 0 &&
        entry_kind == TrackedStatEntryKind::None &&
        IsStaminaStatId(actual_type) &&
        TryPromotePlayerCombatResourcesFromStaminaEntry(entry, "stat-write active stamina")) {
        UpdateTrackedPlayerResourceOwner(owner_root, "stat-write active stamina");
        entry_kind = ClassifyForResourceScaling(entry, ClassifyTrackedStatEntry(entry), actual_type);

        const auto current = g_discovery_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 32) {
            const ActorResolveSnapshot resolved = g_player_resolve;
            Log("runtime: tracked player via stat-write stamina health=0x%p stamina=0x%p spirit=0x%p old=%lld requested=%lld max=%lld",
                reinterpret_cast<void*>(resolved.health_entry),
                reinterpret_cast<void*>(resolved.stamina_entry),
                reinterpret_cast<void*>(resolved.spirit_entry),
                static_cast<long long>(old_value),
                static_cast<long long>(requested_value),
                static_cast<long long>(max_value));
        }
    }

    if (delta < 0 &&
        entry_kind == TrackedStatEntryKind::None &&
        IsPrimarySpiritStatId(actual_type) &&
        TryPromotePlayerCombatResourcesFromSpiritEntry(entry, "stat-write active spirit")) {
        UpdateTrackedPlayerResourceOwner(owner_root, "stat-write active spirit");
        entry_kind = ClassifyForResourceScaling(entry, ClassifyTrackedStatEntry(entry), actual_type);

        const auto current = g_discovery_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 32) {
            const ActorResolveSnapshot resolved = g_player_resolve;
            Log("runtime: tracked player via stat-write spirit health=0x%p stamina=0x%p spirit=0x%p old=%lld requested=%lld max=%lld",
                reinterpret_cast<void*>(resolved.health_entry),
                reinterpret_cast<void*>(resolved.stamina_entry),
                reinterpret_cast<void*>(resolved.spirit_entry),
                static_cast<long long>(old_value),
                static_cast<long long>(requested_value),
                static_cast<long long>(max_value));
        }
    }

    if (actual_type == kHealthId && delta < 0) {
        if (TryAdjustMountHealthWrite(config,
                                      entry,
                                      entry_kind,
                                      actual_type,
                                      old_value,
                                      requested_value,
                                      max_value,
                                      value)) {
            return true;
        }
    }

    if (entry_kind == TrackedStatEntryKind::None &&
        actual_type == kHealthId &&
        delta < 0) {
        if (TryBootstrapPlayerResolveFromHealthWrite(entry)) {
            entry_kind = ClassifyTrackedStatEntry(entry);

            const auto current = g_discovery_logs.fetch_add(1, std::memory_order_acq_rel);
            if (current < 16) {
                const ActorResolveSnapshot resolved = g_player_resolve;
                Log("runtime: tracked player via health-write fallback health=0x%p stamina=0x%p spirit=0x%p old=%lld requested=%lld max=%lld",
                    reinterpret_cast<void*>(resolved.health_entry),
                    reinterpret_cast<void*>(resolved.stamina_entry),
                    reinterpret_cast<void*>(resolved.spirit_entry),
                    static_cast<long long>(old_value),
                    static_cast<long long>(requested_value),
                    static_cast<long long>(max_value));
            }
        } else if (TryAdjustOutgoingHealthWrite(config,
                                                entry,
                                                entry_kind,
                                                actual_type,
                                                old_value,
                                                requested_value,
                                                max_value,
                                                value)) {
            return true;
        } else {
            const auto current = g_process_skip_logs.fetch_add(1, std::memory_order_acq_rel);
            if (current < 24) {
                Log("runtime: untracked health write skipped entry=0x%p old=%lld requested=%lld max=%lld",
                    reinterpret_cast<void*>(entry),
                    static_cast<long long>(old_value),
                    static_cast<long long>(requested_value),
                    static_cast<long long>(max_value));
            }
        }
    }

    StatConfig stat_config{};
    const char* stat_name = nullptr;

    if (entry_kind == TrackedStatEntryKind::PlayerHealth && actual_type == kHealthId) {
        stat_config = config.health;
        if (config.damage.incoming.enabled && config.damage.incoming.stat_write_fallback) {
            stat_config.consumption_multiplier *= config.damage.incoming.multiplier;
        }
        ApplyIncomingResistanceToStatConfig(config, old_value, requested_value, &stat_config);
        stat_name = "health";
    } else if (entry_kind == TrackedStatEntryKind::PlayerStamina && IsStaminaStatId(actual_type)) {
        stat_config = PlayerStaminaConfigForEntry(config, entry_kind);
        stat_name = PlayerStaminaNameForEntry(config, entry_kind);
    } else if (entry_kind == TrackedStatEntryKind::PlayerSpirit && IsSpiritStatId(actual_type)) {
        stat_config = PlayerSpiritConfigForEntry(config, entry_kind);
        stat_name = PlayerSpiritNameForEntry(config, entry_kind);
    } else {
        return false;
    }

    if (entry_kind == TrackedStatEntryKind::PlayerSpirit &&
        IsSpiritStatId(actual_type) &&
        TryConsumeReroutedSpiritStatWrite(entry, requested_value)) {
        const auto current = g_stamina_spirit_diag_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 32) {
            Log("runtime: accepted rerouted spirit stat write without rescale entry=0x%p old=%lld requested=%lld max=%lld",
                reinterpret_cast<void*>(entry),
                static_cast<long long>(old_value),
                static_cast<long long>(requested_value),
                static_cast<long long>(max_value));
        }
        return false;
    }


    int64_t adjusted_value = requested_value;
    if (delta == 0) {
        if ((entry_kind == TrackedStatEntryKind::PlayerStamina && config.stamina.enabled) ||
            (entry_kind == TrackedStatEntryKind::PlayerSpirit && config.spirit.enabled)) {
            const auto current = g_stamina_spirit_diag_logs.fetch_add(1, std::memory_order_acq_rel);
            if (current < 32) {
                Log("runtime: stat write observed no-op stat=%s type=%d entry=0x%p value=%lld max=%lld note=zero-delta",
                    stat_name,
                    actual_type,
                    reinterpret_cast<void*>(entry),
                    static_cast<long long>(requested_value),
                    static_cast<long long>(max_value));
            }
        }
        return false;
    }

    if (delta < 0) {
        if (entry_kind == TrackedStatEntryKind::PlayerSpirit &&
            IsSpiritStatId(actual_type)) {
            if (IsFocusModeActive()) {
                ClearFocusRecoveryWindow("spirit-stat-write-cost");
                const auto focus_current = g_stamina_spirit_diag_logs.fetch_add(1, std::memory_order_acq_rel);
                if (focus_current < 64) {
                    Log("runtime: focus-mode cancelled by spirit stat-write consumption entry=0x%p old=%lld requested=%lld max=%lld",
                        reinterpret_cast<void*>(entry),
                        static_cast<long long>(old_value),
                        static_cast<long long>(requested_value),
                        static_cast<long long>(max_value));
                }
            }

            const int64_t reserve_floor = kPlayerSpiritMinimumFloor;
            if (requested_value < reserve_floor && old_value > reserve_floor) {
                *value = reserve_floor;
                RecordPlayerSpiritConsumption(entry);
                const auto floor_current = g_stamina_spirit_diag_logs.fetch_add(1, std::memory_order_acq_rel);
                if (floor_current < 64) {
                    Log("runtime: clamped player spirit stat-write to reserve floor entry=0x%p old=%lld requested=%lld final=%lld max=%lld",
                        reinterpret_cast<void*>(entry),
                        static_cast<long long>(old_value),
                        static_cast<long long>(requested_value),
                        static_cast<long long>(reserve_floor),
                        static_cast<long long>(max_value));
                }
                return true;
            }
        }

        if (entry_kind == TrackedStatEntryKind::PlayerStamina &&
            IsStaminaStatId(actual_type) &&
            config.advanced.redirect_large_stamina_costs_to_spirit &&
            config.advanced.suppress_redirected_stamina_cost &&
            -delta >= config.advanced.spirit_stamina_redirect_min_cost) {
            const auto suppress_current = g_process_apply_logs.fetch_add(1, std::memory_order_acq_rel);
            if (suppress_current < 24) {
                Log("runtime: ignored legacy stamina-to-spirit stat-write suppression for tracked player stamina entry=0x%p old=%lld requested=%lld max=%lld min_cost=%lld",
                    reinterpret_cast<void*>(entry),
                    static_cast<long long>(old_value),
                    static_cast<long long>(requested_value),
                    static_cast<long long>(max_value),
                    static_cast<long long>(config.advanced.spirit_stamina_redirect_min_cost));
            }
        }

        const int64_t consumed = -delta;
        const int64_t target_consumption = ScaleDelta(consumed, stat_config.consumption_multiplier);
        const int64_t adjustment = consumed - target_consumption;
        adjusted_value = ClampToRange(requested_value + adjustment, 0, max_value);
    } else {
        if (entry_kind == TrackedStatEntryKind::PlayerSpirit &&
            IsSpiritStatId(actual_type) &&
            old_value == 0 &&
            requested_value >= max_value / 2) {
            const auto bootstrap_current = g_stamina_spirit_diag_logs.fetch_add(1, std::memory_order_acq_rel);
            if (bootstrap_current < 32) {
                Log("runtime: accepted spirit stat-write bootstrap fill entry=0x%p old=%lld requested=%lld max=%lld heal=%.3f",
                    reinterpret_cast<void*>(entry),
                    static_cast<long long>(old_value),
                    static_cast<long long>(requested_value),
                    static_cast<long long>(max_value),
                    stat_config.heal_multiplier);
            }
            return false;
        }

        DWORD spirit_recovery_elapsed_ms = 0;
        if (entry_kind == TrackedStatEntryKind::PlayerSpirit &&
            IsSpiritStatId(actual_type) &&
            IsSpiritRecoveryLockoutActive(config, entry, &spirit_recovery_elapsed_ms)) {
            *value = old_value;
            const auto block_current = g_spirit_recovery_block_logs.fetch_add(1, std::memory_order_acq_rel);
            if (block_current < 64) {
                Log("runtime: blocked immediate spirit recovery stat-write entry=0x%p old=%lld requested=%lld max=%lld elapsed_ms=%lu visible_window_ms=%lu configured_lockout_ms=%lu",
                    reinterpret_cast<void*>(entry),
                    static_cast<long long>(old_value),
                    static_cast<long long>(requested_value),
                    static_cast<long long>(max_value),
                    static_cast<unsigned long>(spirit_recovery_elapsed_ms),
                    static_cast<unsigned long>(kVisibleSpiritCostWindowMs),
                    static_cast<unsigned long>(config.spirit.recovery_lockout_ms));
            }
            return true;
        }

        bool should_boost_recovery = true;
        if (entry_kind == TrackedStatEntryKind::PlayerSpirit && IsSpiritStatId(actual_type)) {
            should_boost_recovery = ShouldBoostSpiritRecoveryDelta(config, entry, delta, max_value);
        } else if (entry_kind == TrackedStatEntryKind::PlayerStamina && IsStaminaStatId(actual_type)) {
            should_boost_recovery = ShouldBoostStaminaRecoveryDelta(delta, max_value);
        }

        if (!should_boost_recovery) {
            return false;
        }

        const int64_t healed = delta;
        const int64_t target_heal = ScaleRecoveryDelta(healed, stat_config.heal_multiplier);
        const int64_t adjustment = target_heal - healed;
        adjusted_value = ClampToRange(requested_value + adjustment, 0, max_value);
    }

    *value = adjusted_value;
    if (entry_kind == TrackedStatEntryKind::PlayerSpirit &&
        IsSpiritStatId(actual_type)) {
        SyncPlayerSpiritVisualMirror(entry,
                                     adjusted_value,
                                     max_value,
                                     adjusted_value < old_value ? "spirit-stat-write-cost" : "spirit-stat-write-recovery");
    }

    if (adjusted_value == requested_value &&
        ((entry_kind == TrackedStatEntryKind::PlayerStamina && config.stamina.enabled) ||
         (entry_kind == TrackedStatEntryKind::PlayerSpirit && config.spirit.enabled))) {
        const auto diag_current = g_stamina_spirit_diag_logs.fetch_add(1, std::memory_order_acq_rel);
        if (diag_current < 32) {
            Log("runtime: stat write observed no-op stat=%s type=%d old=%lld requested=%lld max=%lld consumption=%.3f heal=%.3f note=scaled-same",
                stat_name,
                actual_type,
                static_cast<long long>(old_value),
                static_cast<long long>(requested_value),
                static_cast<long long>(max_value),
                stat_config.consumption_multiplier,
                stat_config.heal_multiplier);
        }
    }

    const auto current = g_process_apply_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 24) {
        Log("runtime: adjusted stat write stat=%s type=%d old=%lld requested=%lld final=%lld max=%lld",
            stat_name,
            actual_type,
            static_cast<long long>(old_value),
            static_cast<long long>(requested_value),
            static_cast<long long>(adjusted_value),
            static_cast<long long>(max_value));
    }

    return adjusted_value != requested_value;
}





