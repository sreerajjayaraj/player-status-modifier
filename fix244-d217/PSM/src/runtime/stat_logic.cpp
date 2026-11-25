#include "runtime/stat_logic.h"
#include "damage_menu514_contract.h"
#include "runtime/keyboard_state.h"

#include "config.h"
#include "logger.h"
#include "mod_logic.h"
#include "runtime/actor_resolve.h"
#include "runtime/resource_routing_policy.h"
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
constexpr int64_t kPostRematchDetachedStaminaMinMax =
    psm::routing::kSupportedPlayerStaminaMinimum;
constexpr int64_t kPostRematchDetachedStaminaMaxMax =
    psm::routing::kSupportedPlayerStaminaMaximum;
constexpr int64_t kPostRematchDetachedSpiritMinMax =
    psm::routing::kSupportedPlayerSpiritMinimum;
constexpr int64_t kPostRematchDetachedSpiritMaxMax =
    psm::routing::kSupportedPlayerSpiritMaximum;

#ifndef VK_GAMEPAD_LEFT_THUMBSTICK_BUTTON
#define VK_GAMEPAD_LEFT_THUMBSTICK_BUTTON 0xC8
#endif

#ifndef VK_GAMEPAD_RIGHT_THUMBSTICK_BUTTON
#define VK_GAMEPAD_RIGHT_THUMBSTICK_BUTTON 0xC9
#endif


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
std::atomic<DWORD> g_last_focus_recovery_tick{0};
std::atomic<bool> g_last_focus_recovery_state{false};
std::atomic<std::uint32_t> g_spirit_recovery_block_logs{0};
std::atomic<std::uint32_t> g_legacy_spirit_mirror_apply_logs{0};
std::atomic<uintptr_t> g_post_rematch_detached_stamina_entry{0};
std::atomic<uintptr_t> g_post_rematch_detached_spirit_entry{0};
std::atomic<bool> g_authoritative_player_resource_delta_active{false};
std::atomic<std::uint32_t> g_authoritative_player_resource_delta_logs{0};

struct RecentAuthoritativeResourceAdjustment {
    std::atomic<uintptr_t> entry{0};
    std::atomic<int64_t> final_value{0};
    std::atomic<DWORD> tick{0};
};

RecentAuthoritativeResourceAdjustment g_recent_stamina_adjustment{};
RecentAuthoritativeResourceAdjustment g_recent_spirit_adjustment{};

bool IsPlayerStaminaMaxValue(const int64_t max_value) {
    return max_value >= kPostRematchDetachedStaminaMinMax &&
           max_value <= kPostRematchDetachedStaminaMaxMax;
}

bool IsType20StaminaShapedEntry(const int32_t actual_type, const int64_t max_value) {
    return psm::routing::IsStaminaResourceShape(actual_type,
                                                max_value,
                                                GetResolvedStaminaStatId(),
                                                GetResolvedSpiritStatId(),
                                                kPostRematchDetachedStaminaMinMax,
                                                kPostRematchDetachedStaminaMaxMax);
}

bool IsStaminaTypeOrShape(const int32_t actual_type, const int64_t max_value) {
    return IsStaminaStatId(actual_type) || IsType20StaminaShapedEntry(actual_type, max_value);
}

bool IsSpiritTypeOrShape(const int32_t actual_type, const int64_t max_value) {
    return psm::routing::IsCombatSpiritResourceShape(actual_type,
                                                     max_value,
                                                     GetResolvedSpiritStatId(),
                                                     kPostRematchDetachedSpiritMinMax,
                                                     kPostRematchDetachedSpiritMaxMax,
                                                     GetResolvedAlternateSpiritStatId()) ||
           actual_type == kLegacySpiritId ||
           actual_type == kLegacySpiritId2;
}

bool IsLargePrimarySpiritMirrorShape(const int32_t actual_type, const int64_t max_value) {
    return IsPrimarySpiritStatId(actual_type) && IsPlayerStaminaMaxValue(max_value);
}

bool IsTrackedLargePlayerSpiritMirror(const uintptr_t entry,
                                      const int32_t actual_type,
                                      const int64_t max_value) {
    return IsLargePrimarySpiritMirrorShape(actual_type, max_value) &&
           entry == GetTrackedPlayerSpiritEntry() &&
           entry != GetTrackedPlayerStaminaEntry();
}

bool TryReadRawStatEntryValues(const uintptr_t entry,
                               int32_t* const actual_type,
                               int64_t* const current_value,
                               int64_t* const max_value) {
    if (entry < kMinimumPointerAddress ||
        actual_type == nullptr ||
        current_value == nullptr ||
        max_value == nullptr) {
        return false;
    }

    __try {
        *actual_type = *reinterpret_cast<const int32_t*>(entry);
        *current_value = *reinterpret_cast<const int64_t*>(entry + 0x08);
        *max_value = *reinterpret_cast<const int64_t*>(entry + 0x18);
        return *max_value > 0 && *current_value >= 0 && *current_value <= *max_value;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool TryReadStatEntryValues(const uintptr_t entry,
                            const int32_t expected_type,
                            int64_t* const current_value,
                            int64_t* const max_value) {
    if (current_value == nullptr || max_value == nullptr || entry < kMinimumPointerAddress) {
        return false;
    }

    int32_t actual_type = -1;
    int64_t current = 0;
    int64_t maximum = 0;
    if (!TryReadRawStatEntryValues(entry, &actual_type, &current, &maximum)) {
        return false;
    }

    const bool type_matches = expected_type == kStaminaId
        ? IsStaminaTypeOrShape(actual_type, maximum)
        : (expected_type == kSpiritId ? IsSpiritTypeOrShape(actual_type, maximum) : actual_type == expected_type);
    if (!type_matches) {
        return false;
    }

    *current_value = current;
    *max_value = maximum;
    return true;
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

bool LooksLikePostRematchDetachedPlayerStaminaEntry(const uintptr_t entry,
                                                    const int64_t max_value) {
    if (entry < kMinimumPointerAddress ||
        max_value < kPostRematchDetachedStaminaMinMax ||
        max_value > kPostRematchDetachedStaminaMaxMax) {
        return false;
    }

    const uintptr_t adjacent_spirit =
        entry + (kSpiritEntryOffsetFromHealth - kStaminaEntryOffsetFromHealth);
    int64_t spirit_current = 0;
    int64_t spirit_max = 0;
    return TryReadStatEntryValues(adjacent_spirit, kSpiritId, &spirit_current, &spirit_max) &&
           spirit_max >= kPostRematchDetachedSpiritMinMax &&
           spirit_max <= kPostRematchDetachedSpiritMaxMax;
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
        if (!TryReadStatEntryValues(entry, kStaminaId, &current_value, &max_value)) {
            return false;
        }

        // The post-rematch gameplay path can continue writing a visible 440k
        // player stamina mirror while other rematch resource blocks are active.
        // Do not cache a single entry here: character switching can expose
        // multiple valid mirrors in the same session.
        if (LooksLikePostRematchDetachedPlayerStaminaEntry(entry, max_value)) {
            g_post_rematch_detached_stamina_entry.store(entry, std::memory_order_release);
            return true;
        }

        return false;
    }

    if (!IsCurrentSpiritStatId(actual_type) ||
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
    if (IsCurrentSpiritStatId(actual_type) && IsPostRematchDetachedPlayerResourceEntry(entry, actual_type)) {
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

    if (IsFocusRecoveryWindowActive()) {
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
    if (IsFocusRecoveryWindowActive() || IsPassiveRecoveryTick(delta, max_value)) {
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
                !IsStaminaStatId(stat_type) &&
                !IsCurrentSpiritStatId(stat_type)) {
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

bool TrySyncLegacySpiritMirrorFromPrimary(const uintptr_t primary_entry,
                                          const int64_t primary_value,
                                          const int64_t primary_max,
                                          const char* const reason) {
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
        max_value < psm::routing::kSupportedPlayerStaminaMinimum ||
        max_value > psm::routing::kSupportedPlayerStaminaMaximum) {
        return false;
    }

    const uintptr_t adjacent_spirit =
        entry + (kSpiritEntryOffsetFromHealth - kStaminaEntryOffsetFromHealth);
    int64_t spirit_current = 0;
    int64_t spirit_max = 0;
    return TryReadStatEntryValues(adjacent_spirit, kSpiritId, &spirit_current, &spirit_max) &&
           spirit_max >= psm::routing::kSupportedPlayerSpiritMinimum &&
           spirit_max <= psm::routing::kSupportedPlayerSpiritMaximum;
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
                                  const uintptr_t native_player_health,
                                  const bool preserve_native_health_floor,
                                  int64_t* const value) {
    if (value == nullptr ||
        !config.damage.outgoing.enabled ||
        !config.damage.outgoing.stat_write_fallback ||
        config.damage.outgoing.multiplier == 1.0 ||
        psm::damage_menu514::PreferMenuOutgoing(
            psm::damage_menu514::g_verified_menu.load(std::memory_order_acquire)) ||
        entry_kind != TrackedStatEntryKind::None ||
        actual_type != kHealthId ||
        requested_value >= old_value ||
        max_value <= 0 ||
        old_value < 0 ||
        old_value > max_value ||
        requested_value < 0 ||
        (preserve_native_health_floor && requested_value == 0)) {
        return false;
    }

    const uintptr_t player_health = native_player_health ? native_player_health : GetTrackedPlayerHealthEntry();
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

    // The pre-notify hook runs after the native writer clamps to this lower bound.
    // Preserve phase floors and let native lethal writes deliver defeat notifications.
    int64_t floor = 0;
    if (preserve_native_health_floor) {
        const int64_t native_floor = *reinterpret_cast<const int64_t*>(entry + 0x28);
        if (native_floor > requested_value) return false;
        floor = (std::max)(int64_t{1}, native_floor);
    }
    const int64_t adjusted_value = ClampToRange(old_value - scaled_damage, floor, max_value);
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
        *delta == 0) {
        return false;
    }

    int64_t current_value = 0;
    int64_t max_value = 0;
    if (!TryReadStatEntryValues(entry, kStaminaId, &current_value, &max_value)) {
        return false;
    }

    if (!IsStaminaTypeOrShape(actual_type, max_value)) {
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
    RecordAuthoritativeResourceAdjustment(
        AuthoritativeResourceKind::Stamina,
        entry,
        ClampToRange(current_value + adjusted_delta, 0, max_value));

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
        max_value < psm::routing::kSupportedPlayerStaminaMinimum ||
        max_value > psm::routing::kSupportedPlayerStaminaMaximum) {
        return false;
    }

    const uintptr_t adjacent_spirit = entry + (kSpiritEntryOffsetFromHealth - kStaminaEntryOffsetFromHealth);
    int64_t adjacent_current = 0;
    int64_t adjacent_max = 0;
    if (!TryReadStatEntryValues(adjacent_spirit, kSpiritId, &adjacent_current, &adjacent_max) ||
        adjacent_max < psm::routing::kSupportedPlayerSpiritMinimum ||
        adjacent_max > psm::routing::kSupportedPlayerSpiritMaximum) {
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
    NotifyResourceHudBindingLocked();
    g_player_resolve.spirit_entry = adjacent_spirit;
    NotifyResourceHudBindingLocked();
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
        spirit_max < psm::routing::kSupportedPlayerSpiritMinimum ||
        spirit_max > psm::routing::kSupportedPlayerSpiritMaximum) {
        return false;
    }

    const uintptr_t stamina_entry = spirit_entry - (kSpiritEntryOffsetFromHealth - kStaminaEntryOffsetFromHealth);
    int64_t stamina_current = 0;
    int64_t stamina_max = 0;
    if (!TryReadStatEntryValues(stamina_entry, kStaminaId, &stamina_current, &stamina_max) ||
        stamina_max < psm::routing::kSupportedPlayerStaminaMinimum ||
        stamina_max > psm::routing::kSupportedPlayerStaminaMaximum) {
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
        NotifyResourceHudBindingLocked();
        g_player_resolve.spirit_entry = spirit_entry;
        NotifyResourceHudBindingLocked();
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
        (!IsStaminaStatId(actual_type) && !IsPrimarySpiritStatId(actual_type)) ||
        *delta >= 0) {
        return false;
    }

    const int64_t original_delta = *delta;
    int64_t current_value = 0;
    int64_t max_value = 0;
    bool used_lock_value = false;
    int64_t locked_value = 0;
    const char* profile_name = "unknown";

    int32_t read_type = -1;
    if (TryReadRawStatEntryValues(entry, &read_type, &current_value, &max_value)) {
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
        if (IsFocusRecoveryWindowActive()) {
            *delta = 0;
            const auto current = g_stamina_spirit_diag_logs.fetch_add(1, std::memory_order_acq_rel);
            if (current < 64) {
                Log("runtime: blocked focus-mode spirit delta consumption entry=0x%p old=%lld current=%lld max=%lld",
                    reinterpret_cast<void*>(entry),
                    static_cast<long long>(original_delta),
                    static_cast<long long>(current_value),
                    static_cast<long long>(max_value));
            }
            return true;
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
            RecordAuthoritativeResourceAdjustment(
                AuthoritativeResourceKind::Spirit,
                entry,
                ClampToRange(current_value + direct_delta, 0, max_value));
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
    RecordAuthoritativeResourceAdjustment(
        AuthoritativeResourceKind::Spirit,
        entry,
        ClampToRange(current_value + adjusted_delta, 0, max_value));

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

void RecordAuthoritativeResourceAdjustment(const AuthoritativeResourceKind kind,
                                           const uintptr_t entry,
                                           const int64_t final_value) {
    if (entry < kMinimumPointerAddress || final_value < 0) {
        return;
    }

    RecentAuthoritativeResourceAdjustment& slot =
        kind == AuthoritativeResourceKind::Stamina
            ? g_recent_stamina_adjustment
            : g_recent_spirit_adjustment;
    slot.entry.store(entry, std::memory_order_relaxed);
    slot.final_value.store(final_value, std::memory_order_relaxed);
    slot.tick.store(GetTickCount(), std::memory_order_release);
}

void SetAuthoritativePlayerResourceDeltaActive(const bool active) {
    g_authoritative_player_resource_delta_active.store(active, std::memory_order_release);
}

bool IsAuthoritativePlayerResourceDeltaActive() {
    return g_authoritative_player_resource_delta_active.load(std::memory_order_acquire);
}

bool TryAdjustAuthoritativePlayerResourceDelta(
    const AuthoritativeResourceKind kind,
    const uintptr_t entry,
    int64_t* const delta) {
    if (!g_runtime_enabled.load(std::memory_order_acquire) ||
        delta == nullptr ||
        entry < kMinimumPointerAddress ||
        *delta == 0) {
        return false;
    }

    const uintptr_t tracked_stamina = GetTrackedPlayerStaminaEntry();
    const uintptr_t tracked_spirit = GetTrackedPlayerSpiritEntry();
    const auto route = psm::routing::SelectAuthoritativeSharedDeltaRoute(
        entry,
        tracked_stamina,
        tracked_spirit);
    const auto expected_route = kind == AuthoritativeResourceKind::Stamina
        ? psm::routing::DiscoveredPlayerResourceRole::Stamina
        : psm::routing::DiscoveredPlayerResourceRole::Spirit;
    if (route != expected_route) {
        return false;
    }

    int32_t actual_type = -1;
    int64_t current_value = 0;
    int64_t max_value = 0;
    if (!TryReadRawStatEntryValues(entry, &actual_type, &current_value, &max_value)) {
        return false;
    }

    const ModConfig config = GetConfig();
    const StatConfig& stat_config = kind == AuthoritativeResourceKind::Stamina
        ? config.stamina
        : config.spirit;
    if (!config.general.enabled || !stat_config.enabled) {
        return false;
    }

    const int64_t original_delta = *delta;
    int64_t adjusted_delta = original_delta;
    double multiplier = psm::routing::SelectAuthoritativeSharedDeltaMultiplier(
        route,
        original_delta,
        config.stamina.consumption_multiplier,
        config.stamina.heal_multiplier,
        config.spirit.consumption_multiplier,
        config.spirit.heal_multiplier);

    const bool bootstrap_fill =
        original_delta > 0 &&
        current_value == 0 &&
        original_delta >= max_value / 2;
    if (!bootstrap_fill && original_delta < 0) {
        const int64_t original_cost = original_delta == (std::numeric_limits<int64_t>::min)()
            ? (std::numeric_limits<int64_t>::max)()
            : -original_delta;
        const int64_t scaled_cost = ScaleDelta(original_cost, multiplier);
        const int64_t reserve_floor = kind == AuthoritativeResourceKind::Spirit
            ? kPlayerSpiritMinimumFloor
            : 0;
        const int64_t available = std::max<int64_t>(0, current_value - reserve_floor);
        adjusted_delta = -std::min(scaled_cost, available);

        if (kind == AuthoritativeResourceKind::Stamina) {
            RecordPlayerStaminaConsumption(entry);
        } else {
            RecordPlayerSpiritConsumption(entry);
        }
    } else if (!bootstrap_fill && original_delta > 0) {
        const int64_t scaled_recovery = ScaleRecoveryDelta(original_delta, multiplier);
        const int64_t missing = std::max<int64_t>(0, max_value - current_value);
        adjusted_delta = std::min(scaled_recovery, missing);
    } else {
        adjusted_delta = std::min(original_delta, max_value - current_value);
        multiplier = 1.0;
    }

    const int64_t final_value = ClampToRange(current_value + adjusted_delta, 0, max_value);
    RecordAuthoritativeResourceAdjustment(kind, entry, final_value);
    *delta = adjusted_delta;

    const auto log_index =
        g_authoritative_player_resource_delta_logs.fetch_add(1, std::memory_order_acq_rel);
    if (log_index < 160) {
        Log("runtime: authoritative shared %s delta entry=0x%p type=%d old=%lld final=%lld current=%lld result=%lld max=%lld multiplier=%.3f bootstrap=%d",
            kind == AuthoritativeResourceKind::Stamina ? "stamina" : "spirit",
            reinterpret_cast<void*>(entry),
            actual_type,
            static_cast<long long>(original_delta),
            static_cast<long long>(adjusted_delta),
            static_cast<long long>(current_value),
            static_cast<long long>(final_value),
            static_cast<long long>(max_value),
            multiplier,
            bootstrap_fill ? 1 : 0);
    }

    return adjusted_delta != original_delta;
}

void RecordAuthoritativePlayerSpiritCommit(const uintptr_t entry,
                                           const int64_t previous_value,
                                           const int64_t final_value) {
    if (entry < kMinimumPointerAddress ||
        previous_value < 0 ||
        final_value < 0) {
        return;
    }

    if (psm::routing::IsAuthoritativePlayerResourceConsumption(
            previous_value,
            final_value)) {
        RecordPlayerSpiritConsumption(entry);
    }
    RecordAuthoritativeResourceAdjustment(
        AuthoritativeResourceKind::Spirit,
        entry,
        final_value);
}

bool WasRecentlyAdjustedByAuthoritativeResourceWrite(const uintptr_t entry,
                                                      const int64_t observed_value,
                                                      const uint32_t window_ms) {
    if (entry < kMinimumPointerAddress || observed_value < 0) {
        return false;
    }

    const DWORD now = GetTickCount();
    const auto matches = [entry, observed_value, window_ms, now](
                             const RecentAuthoritativeResourceAdjustment& slot) {
        const DWORD tick = slot.tick.load(std::memory_order_acquire);
        if (tick == 0) {
            return false;
        }

        return psm::routing::ShouldSkipWatcherAdjustmentAfterAuthoritativeWrite(
            slot.entry.load(std::memory_order_relaxed) == entry,
            slot.final_value.load(std::memory_order_relaxed) == observed_value,
            now - tick,
            window_ms);
    };

    return matches(g_recent_stamina_adjustment) ||
           matches(g_recent_spirit_adjustment);
}

bool IsPostRematchDetachedPlayerStaminaMirror(const uintptr_t entry) {
    return IsPostRematchDetachedPlayerResourceEntry(
        entry,
        GetResolvedStaminaStatId());
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

bool IsFocusRecoveryWindowActive() {
    const psm::input::ForegroundInput focus;
    bool xinput_focus = false;
    for (DWORD index = 0; index < XUSER_MAX_COUNT && focus.Active(); ++index) {
        XINPUT_STATE state{};
        if (!focus.ReadButton([&] { return XInputGetState(index, &state) == ERROR_SUCCESS; })) {
            continue;
        }

        constexpr WORD kFocusButtons = XINPUT_GAMEPAD_LEFT_THUMB | XINPUT_GAMEPAD_RIGHT_THUMB;
        if ((state.Gamepad.wButtons & kFocusButtons) == kFocusButtons) {
            xinput_focus = true;
            break;
        }
    }
    const DWORD now = GetTickCount();
    DWORD focus_window_ms = GetConfig().advanced.focus_recovery_window_ms;
    bool active = false;
    const bool keyboard_focus = psm::input::IsKeyDown(focus, 'X');
    const bool left_stick = psm::input::IsKeyDown(focus, VK_GAMEPAD_LEFT_THUMBSTICK_BUTTON);
    const bool right_stick = psm::input::IsKeyDown(focus, VK_GAMEPAD_RIGHT_THUMBSTICK_BUTTON);
    if ((keyboard_focus || (left_stick && right_stick) || xinput_focus) && focus.Active()) {
        RecordFocusRecoveryWindow();
        active = true;
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
        UpdateTrackedMountFromStatComponent(entry, component);
        return;
    }

    const uintptr_t tracked_health = g_player_resolve.health_entry;
    if (tracked_health >= kMinimumPointerAddress &&
        (entry < tracked_health || entry > tracked_health + 0x900)) {
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

    if (stat_type == kHealthId && component == g_player_resolve.root) {
        auto next = g_player_resolve;
        if (psm::combat_health::BindPrimary(next, entry)) {
            AssignTrackedPlayerResolveLocked(next);
            NotifyResourceHudBindingLocked();
            return;
        }
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

    int32_t actual_type = -1;
    int64_t current_value_for_type = 0;
    int64_t max_value_for_type = 0;
    if (!TryReadRawStatEntryValues(entry, &actual_type, &current_value_for_type, &max_value_for_type) ||
        (!IsStaminaTypeOrShape(actual_type, max_value_for_type) &&
         !(entry == g_mount_resolve.stamina_entry &&
           IsPrimarySpiritStatId(actual_type) &&
           max_value_for_type > kPostRematchDetachedSpiritMaxMax)) ||
        *delta == 0) {
        if (config.stamina.enabled) {
            const auto current = g_stamina_spirit_diag_logs.fetch_add(1, std::memory_order_acq_rel);
            if (current < 32) {
                Log("runtime: stamina delta skipped entry=0x%p type=%d delta=%lld reason=%s",
                    reinterpret_cast<void*>(entry),
                    actual_type,
                    delta == nullptr ? 0LL : static_cast<long long>(*delta),
                    *delta == 0 ? "zero-delta" : "non-stamina-shape");
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

    int32_t actual_type = -1;
    int64_t current_value_for_type = 0;
    int64_t max_value_for_type = 0;
    const bool readable = TryReadRawStatEntryValues(entry, &actual_type, &current_value_for_type, &max_value_for_type);
    const bool large_tracked_spirit = readable &&
        IsTrackedLargePlayerSpiritMirror(entry, actual_type, max_value_for_type);
    if (!readable ||
        (!IsSpiritTypeOrShape(actual_type, max_value_for_type) && !large_tracked_spirit) ||
        *delta == 0) {
        if (config.spirit.enabled) {
            const auto current = g_stamina_spirit_diag_logs.fetch_add(1, std::memory_order_acq_rel);
            if (current < 32) {
                Log("runtime: spirit delta skipped entry=0x%p type=%d delta=%lld reason=%s",
                    reinterpret_cast<void*>(entry),
                    actual_type,
                    static_cast<long long>(*delta),
                    *delta == 0 ? "zero-delta" : "non-spirit-shape");
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

bool TryAdjustStatWrite(const uintptr_t entry, int64_t* const value, const uintptr_t owner_root,
                        const bool preserve_native_health_floor) {
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
    const bool stamina_shape = IsStaminaTypeOrShape(actual_type, max_value);
    const bool compact_spirit_shape = IsSpiritTypeOrShape(actual_type, max_value);
    const bool large_spirit_mirror_shape = IsLargePrimarySpiritMirrorShape(actual_type, max_value);
    const bool large_spirit_candidate = large_spirit_mirror_shape && entry != GetTrackedPlayerStaminaEntry();
    TrackedStatEntryKind entry_kind = ClassifyForResourceScaling(entry, ClassifyTrackedStatEntry(entry), actual_type);
    const uintptr_t native_player_health = actual_type == kHealthId ? psm::combat_health::HealthEntry() : 0;
    if (native_player_health) {
        if (entry == native_player_health || psm::combat_health::HealthEntry(0, entry)) {
            entry_kind = TrackedStatEntryKind::PlayerHealth;
        } else if (entry_kind == TrackedStatEntryKind::PlayerHealth) {
            entry_kind = TrackedStatEntryKind::None;
        }
    }
    const int64_t delta = requested_value - old_value;
    if (IsAuthoritativePlayerResourceDeltaActive() &&
        (entry_kind == TrackedStatEntryKind::PlayerStamina ||
         entry_kind == TrackedStatEntryKind::PlayerSpirit ||
         (entry_kind == TrackedStatEntryKind::None &&
          (stamina_shape || compact_spirit_shape || large_spirit_mirror_shape)))) {
        return false;
    }
    const bool has_authoritative_player_owner = HasTrackedPlayerDamageOwner();
    const bool candidate_matches_active_player =
        entry == GetTrackedPlayerStaminaEntry() ||
        entry == GetTrackedPlayerSpiritEntry();
    const bool allow_ownerless_player_promotion =
        psm::routing::ShouldAllowOwnerlessPlayerResourcePromotion(
            has_authoritative_player_owner,
            candidate_matches_active_player);

    // A real negative write to either member of a validated adjacent player
    // resource pair is stronger evidence than the process-wide selector cache.
    // Rebind the pair before type-based routing so character switches do not
    // inherit Kliff's old entry addresses or wait for an unrelated primer.
    if (entry_kind == TrackedStatEntryKind::None &&
        delta < 0 &&
        owner_root >= kMinimumPointerAddress &&
        TryPromotePlayerObservedResourceOwner(
            entry,
            owner_root,
            actual_type,
            "stat-write active adjacent player pair")) {
        entry_kind = ClassifyForResourceScaling(
            entry,
            ClassifyTrackedStatEntry(entry),
            actual_type);
    }

    const bool loose_mount_stamina_shape =
        psm::routing::ShouldTreatType20ResourceAsLooseMountStamina(
            actual_type,
            max_value,
            GetResolvedSpiritStatId(),
            kPostRematchDetachedSpiritMaxMax,
            false,
            false,
            false,
            kSpecialMountStaminaMinMax,
            10000000,
            kPostRematchDetachedStaminaMinMax);
    if (entry_kind == TrackedStatEntryKind::None &&
        loose_mount_stamina_shape) {
        UpdateTrackedMountFromStaminaContext(entry, owner_root);
        entry_kind = ClassifyTrackedStatEntry(entry);
    }

    if (entry_kind == TrackedStatEntryKind::MountStamina && delta < 0) {
        int64_t adjusted_delta = delta;
        if (TryAdjustMountStaminaDelta(config,
                                       entry,
                                       entry_kind,
                                       actual_type,
                                       &adjusted_delta)) {
            *value = ClampToRange(old_value + adjusted_delta, 0, max_value);
            return true;
        }
    }

    if (entry_kind == TrackedStatEntryKind::None &&
        large_spirit_candidate &&
        delta < 0 &&
        TryBindObservedSpiritToConfirmedPlayer(
            entry,
            owner_root,
            delta,
            "stat-write large spirit mirror")) {
        entry_kind = ClassifyForResourceScaling(
            entry,
            ClassifyTrackedStatEntry(entry),
            actual_type);
    }

    if (stamina_shape &&
        entry_kind != TrackedStatEntryKind::PlayerSpirit &&
        owner_root >= kMinimumPointerAddress) {
        bool activated = false;
        if (delta < 0) {
            activated = TryActivateCorrelatedPlayerOwnerCandidate(
                owner_root,
                entry,
                delta,
                "stat-write active stamina owner");
        } else {
            ObservePlayerStaminaOwnerCandidate(owner_root, entry);
        }
        if (activated) {
            entry_kind = ClassifyForResourceScaling(
                entry,
                ClassifyTrackedStatEntry(entry),
                actual_type);
        }
    }

    if (entry_kind == TrackedStatEntryKind::None &&
        compact_spirit_shape &&
        delta < 0 &&
        TryBindObservedSpiritToConfirmedPlayer(
            entry,
            owner_root,
            delta,
            "stat-write active spirit")) {
        entry_kind = ClassifyForResourceScaling(
            entry,
            ClassifyTrackedStatEntry(entry),
            actual_type);
    }

    ActorResolveSnapshot observed_mount{};
    {
        std::lock_guard state_lock(g_state_mutex);
        observed_mount = g_mount_resolve;
    }
    const bool trusted_mount_resource =
        observed_mount.valid() &&
        g_mount_track_confidence.load(std::memory_order_acquire) >= 3 &&
        (entry == observed_mount.health_entry ||
         entry == observed_mount.stamina_entry ||
         entry == observed_mount.spirit_entry ||
         owner_root == observed_mount.root ||
         owner_root == observed_mount.damage_target);
    const bool exact_active_player_owner =
        IsExactConfirmedTrackedPlayerOwner(owner_root);
    const bool observed_player_resource_shape =
        stamina_shape || compact_spirit_shape || large_spirit_candidate;
    if (entry_kind == TrackedStatEntryKind::None &&
        owner_root >= kMinimumPointerAddress &&
        delta < 0 &&
        psm::routing::ShouldPromoteObservedPlayerResourceOwner(
            !HasTrackedPlayerDamageOwner(),
            exact_active_player_owner,
            observed_player_resource_shape,
            trusted_mount_resource) &&
        TryPromotePlayerObservedResourceOwner(entry, owner_root, actual_type, "stat-write observed resource owner")) {
        entry_kind = ClassifyForResourceScaling(entry, ClassifyTrackedStatEntry(entry), actual_type);

        const auto current = g_discovery_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 48) {
            const ActorResolveSnapshot resolved = g_player_resolve;
            Log("runtime: tracked player via observed stat-write owner=0x%p health=0x%p stamina=0x%p spirit=0x%p type=%d old=%lld requested=%lld max=%lld delta=%lld",
                reinterpret_cast<void*>(owner_root),
                reinterpret_cast<void*>(resolved.health_entry),
                reinterpret_cast<void*>(resolved.stamina_entry),
                reinterpret_cast<void*>(resolved.spirit_entry),
                actual_type,
                static_cast<long long>(old_value),
                static_cast<long long>(requested_value),
                static_cast<long long>(max_value),
                static_cast<long long>(delta));
        }
    }
    if (delta < 0 &&
        entry_kind == TrackedStatEntryKind::None &&
        stamina_shape &&
        allow_ownerless_player_promotion &&
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
        compact_spirit_shape &&
        allow_ownerless_player_promotion &&
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
        if (!native_player_health && TryBootstrapPlayerResolveFromHealthWrite(entry)) {
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
                                                native_player_health,
                                                preserve_native_health_floor,
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
    const bool effective_spirit_shape =
        compact_spirit_shape || IsTrackedLargePlayerSpiritMirror(entry, actual_type, max_value);

    if (entry_kind == TrackedStatEntryKind::PlayerHealth && actual_type == kHealthId) {
        stat_config = config.health;
        if (config.damage.incoming.enabled && config.damage.incoming.stat_write_fallback) {
            stat_config.consumption_multiplier *= config.damage.incoming.multiplier;
        }
        ApplyIncomingResistanceToStatConfig(config, old_value, requested_value, &stat_config);
        stat_name = "health";
    } else if (entry_kind == TrackedStatEntryKind::PlayerStamina && stamina_shape) {
        stat_config = PlayerStaminaConfigForEntry(config, entry_kind);
        stat_name = PlayerStaminaNameForEntry(config, entry_kind);
    } else if (entry_kind == TrackedStatEntryKind::PlayerSpirit && effective_spirit_shape) {
        stat_config = PlayerSpiritConfigForEntry(config, entry_kind);
        stat_name = PlayerSpiritNameForEntry(config, entry_kind);
    } else {
        return false;
    }

    if (entry_kind == TrackedStatEntryKind::PlayerSpirit &&
        effective_spirit_shape &&
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
            effective_spirit_shape) {
            if (IsFocusRecoveryWindowActive()) {
                *value = old_value;
                const auto focus_current = g_stamina_spirit_diag_logs.fetch_add(1, std::memory_order_acq_rel);
                if (focus_current < 64) {
                    Log("runtime: blocked focus-mode spirit stat-write consumption entry=0x%p old=%lld requested=%lld max=%lld",
                        reinterpret_cast<void*>(entry),
                        static_cast<long long>(old_value),
                        static_cast<long long>(requested_value),
                        static_cast<long long>(max_value));
                }
                return true;
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
            stamina_shape &&
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
            effective_spirit_shape &&
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
            effective_spirit_shape &&
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
        if (entry_kind == TrackedStatEntryKind::PlayerSpirit && effective_spirit_shape) {
            should_boost_recovery = ShouldBoostSpiritRecoveryDelta(config, entry, delta, max_value);
        } else if (entry_kind == TrackedStatEntryKind::PlayerStamina && stamina_shape) {
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




