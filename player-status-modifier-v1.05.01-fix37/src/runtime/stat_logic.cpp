#include "runtime/stat_logic.h"

#include "config.h"
#include "logger.h"
#include "mod_logic.h"
#include "runtime/actor_resolve.h"
#include "runtime/runtime_state.h"

#include <algorithm>
#include <atomic>
#include <cstdint>

namespace {

constexpr int64_t kMountHealthStatWriteMinMax = 2500000;
constexpr int64_t kMountStaminaStatWriteMinMax = 300000;
constexpr int64_t kGroundMountHealthCandidateMinMax = 10000;
constexpr int64_t kGroundMountHealthCandidateMaxMax = 600000;
std::atomic<std::uint32_t> g_mount_stat_write_logs{0};
std::atomic<std::uint32_t> g_mount_candidate_health_logs{0};
std::atomic<std::uint32_t> g_resistance_apply_logs{0};

bool TryReadStatEntryValues(const uintptr_t entry,
                            const int32_t expected_type,
                            int64_t* const current_value,
                            int64_t* const max_value) {
    if (current_value == nullptr || max_value == nullptr || entry < kMinimumPointerAddress) {
        return false;
    }

    __try {
        const int32_t actual_type = *reinterpret_cast<const int32_t*>(entry);
        const bool type_matches =
            expected_type == kStaminaId ? IsStaminaStatId(actual_type) : actual_type == expected_type;
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

int64_t ClampMountLockValue(const int64_t requested_value, const int64_t max_value) {
    if (max_value <= 0) {
        return 0;
    }

    if (requested_value <= 0) {
        return max_value;
    }

    return requested_value > max_value ? max_value : requested_value;
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
        !config.mount.lock_health ||
        actual_type != kHealthId ||
        entry_kind == TrackedStatEntryKind::PlayerHealth ||
        entry == GetTrackedPlayerHealthEntry() ||
        requested_value >= old_value ||
        old_value < 0 ||
        old_value > max_value ||
        requested_value < 0) {
        return false;
    }

    // A resolved ground mount is not a "large dragon" profile.  Once the
    // resolver has explicitly identified an entry as MountHealth, lock it even
    // when its max value is in the normal ground-mount range.  Keep the old large
    // untracked fallback only for dragon-like entries so enemy/object health is
    // not accidentally locked.
    const bool tracked_mount_health = entry_kind == TrackedStatEntryKind::MountHealth;
    const bool large_untracked_mount_like =
        entry_kind == TrackedStatEntryKind::None && max_value >= kMountHealthStatWriteMinMax;
    if (!tracked_mount_health && !large_untracked_mount_like) {
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

    const int64_t locked_value = ClampMountLockValue(config.mount.lock_value, max_value);
    const int64_t adjusted_value = ClampToRange(locked_value, requested_value, max_value);
    if (adjusted_value == requested_value) {
        return false;
    }

    *value = adjusted_value;

    const auto current = g_mount_stat_write_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 32) {
        Log("runtime: locked mount health write entry=0x%p old=%lld requested=%lld final=%lld max=%lld tracked=%d",
            reinterpret_cast<void*>(entry),
            static_cast<long long>(old_value),
            static_cast<long long>(requested_value),
            static_cast<long long>(adjusted_value),
            static_cast<long long>(max_value),
            tracked_mount_health ? 1 : 0);
    }

    return true;
}

bool TryAdjustMountStaminaDeltaFallback(const ModConfig& config,
                                        const uintptr_t entry,
                                        const TrackedStatEntryKind entry_kind,
                                        const int32_t actual_type,
                                        int64_t* const delta) {
    if (delta == nullptr ||
        !config.mount.enabled ||
        !config.mount.lock_stamina ||
        entry_kind == TrackedStatEntryKind::PlayerStamina ||
        !IsStaminaStatId(actual_type) ||
        *delta >= 0) {
        return false;
    }

    int64_t current_value = 0;
    int64_t max_value = 0;
    if (!TryReadStatEntryValues(entry, kStaminaId, &current_value, &max_value) ||
        max_value < kMountStaminaStatWriteMinMax) {
        return false;
    }

    const int64_t original_delta = *delta;
    const int64_t locked_value = ClampMountLockValue(config.mount.lock_value, max_value);
    *delta = locked_value - current_value;

    const auto current = g_mount_stat_write_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 32) {
        Log("runtime: locked mount-like stamina delta entry=0x%p old_delta=%lld current=%lld lock=%lld max=%lld final_delta=%lld",
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

    const int64_t scaled_damage = ScaleDelta(original_damage, config.damage.outgoing.multiplier);
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

    const int64_t original_delta = *delta;
    int64_t adjusted_delta = original_delta;
    if (original_delta < 0) {
        adjusted_delta = -ScaleDelta(-original_delta, config.stamina.consumption_multiplier);
    } else {
        adjusted_delta = ScaleDelta(original_delta, config.stamina.heal_multiplier);
    }

    if (adjusted_delta == original_delta) {
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

bool TryAdjustMountStaminaDelta(const ModConfig& config,
                                const uintptr_t entry,
                                const TrackedStatEntryKind entry_kind,
                                const int32_t actual_type,
                                int64_t* const delta) {
    if (delta == nullptr ||
        !config.mount.enabled ||
        !config.mount.lock_stamina ||
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

    if (TryReadStatEntryValues(entry, kStaminaId, &current_value, &max_value)) {
        locked_value = ClampMountLockValue(config.mount.lock_value, max_value);
        *delta = locked_value - current_value;
        used_lock_value = true;
    } else {
        *delta = -*delta;
    }

    const auto current = g_mount_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 32) {
        const ActorResolveSnapshot mount_snapshot = g_mount_resolve;
        if (used_lock_value) {
            Log("runtime: locked mount stamina root=0x%p entry=0x%p old_delta=%lld current=%lld lock=%lld max=%lld final_delta=%lld",
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
        actual_type != kSpiritId ||
        *delta == 0) {
        return false;
    }

    const int64_t current_value = *reinterpret_cast<const int64_t*>(entry + 0x08);
    const int64_t max_value = *reinterpret_cast<const int64_t*>(entry + 0x18);
    if (max_value <= 0 || current_value < 0 || current_value > max_value) {
        return false;
    }

    StatConfig stat_config{};
    if (!SelectConfig(config, kSpiritId, &stat_config)) {
        return false;
    }

    const int64_t original_delta = *delta;
    int64_t adjusted_delta = original_delta;
    if (original_delta < 0) {
        adjusted_delta = -ScaleDelta(-original_delta, stat_config.consumption_multiplier);
    } else {
        adjusted_delta = ScaleDelta(original_delta, stat_config.heal_multiplier);
    }

    if (adjusted_delta == original_delta) {
        return false;
    }

    *delta = adjusted_delta;

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

}  // namespace

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
        }
        return;
    }

    if (component_marker != tracked_marker) {
        UpdateTrackedMountFromStatComponent(entry, component);
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

    if (TryAdjustMountStaminaDelta(config, entry, entry_kind, actual_type, delta)) {
        return true;
    }

    if (TryAdjustMountStaminaDeltaFallback(config, entry, entry_kind, actual_type, delta)) {
        return true;
    }

    return TryAdjustPlayerStaminaDelta(config, entry, entry_kind, actual_type, delta);
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
    TrackedStatEntryKind entry_kind = ClassifyTrackedStatEntry(entry);
    if (entry_kind == TrackedStatEntryKind::None &&
        actual_type == kSpiritId &&
        TryBootstrapPlayerSpiritFromEntry(entry)) {
        entry_kind = ClassifyTrackedStatEntry(entry);

        const auto current = g_discovery_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 16) {
            Log("runtime: tracked player spirit via spirit-delta fallback spirit=0x%p",
                reinterpret_cast<void*>(entry));
        }
    }

    return TryAdjustPlayerSpiritDelta(config, entry, entry_kind, actual_type, delta);
}

bool TryAdjustStatWrite(const uintptr_t entry, int64_t* const value) {
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

    TrackedStatEntryKind entry_kind = ClassifyTrackedStatEntry(entry);
    const int32_t actual_type = *reinterpret_cast<const int32_t*>(entry);
    const int64_t delta = requested_value - old_value;

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
        if (config.damage.incoming.enabled) {
            stat_config.consumption_multiplier *= config.damage.incoming.multiplier;
        }
        ApplyIncomingResistanceToStatConfig(config, old_value, requested_value, &stat_config);
        stat_name = "health";
    } else if (entry_kind == TrackedStatEntryKind::PlayerStamina && IsStaminaStatId(actual_type)) {
        stat_config = config.stamina;
        stat_name = "stamina";
    } else {
        return false;
    }

    int64_t adjusted_value = requested_value;
    if (delta == 0) {
        return false;
    }

    if (delta < 0) {
        const int64_t consumed = -delta;
        const int64_t target_consumption = ScaleDelta(consumed, stat_config.consumption_multiplier);
        const int64_t adjustment = consumed - target_consumption;
        adjusted_value = ClampToRange(requested_value + adjustment, 0, max_value);
    } else {
        const int64_t healed = delta;
        const int64_t target_heal = ScaleDelta(healed, stat_config.heal_multiplier);
        const int64_t adjustment = target_heal - healed;
        adjusted_value = ClampToRange(requested_value + adjustment, 0, max_value);
    }

    *value = adjusted_value;

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
