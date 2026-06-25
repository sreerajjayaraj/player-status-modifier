#include "runtime/damage_logic.h"

#include "config.h"
#include "logger.h"
#include "mod_logic.h"
#include "runtime/actor_resolve.h"
#include "runtime/runtime_state.h"

#include <Windows.h>

#include <atomic>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace {

std::atomic<DWORD64> g_environment_damage_burst_tick{0};
std::atomic<int64_t> g_environment_damage_burst_remaining{0};

uintptr_t TryGetTrackedPlayerTargetOwner() {
    return g_player_resolve.damage_target;
}

uintptr_t TryGetTrackedMountTargetOwner() {
    return g_mount_resolve.damage_target;
}

bool TryReadStatEntryValues(const uintptr_t entry,
                            const int32_t expected_type,
                            int64_t* const current_value,
                            int64_t* const max_value) {
    if (current_value == nullptr || max_value == nullptr || entry < kMinimumPointerAddress) {
        return false;
    }

    __try {
        if (*reinterpret_cast<const int32_t*>(entry) != expected_type) {
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

void ClampScaledSignedDelta(const double scaled, int64_t* const value) {
    if (value == nullptr) {
        return;
    }

    if (scaled <= static_cast<double>(std::numeric_limits<int64_t>::min())) {
        *value = std::numeric_limits<int64_t>::min();
    } else if (scaled >= static_cast<double>(std::numeric_limits<int64_t>::max())) {
        *value = std::numeric_limits<int64_t>::max();
    } else {
        *value = static_cast<int64_t>(scaled);
    }
}

bool IsPlayerResourceEntriesReady() {
    const ActorResolveSnapshot player = g_player_resolve;
    int64_t current_value = 0;
    int64_t max_value = 0;
    return TryReadStatEntryValues(player.health_entry, kHealthId, &current_value, &max_value) &&
           TryReadStatEntryValues(player.stamina_entry, kStaminaId, &current_value, &max_value) &&
           TryReadStatEntryValues(player.spirit_entry, kSpiritId, &current_value, &max_value);
}

bool TryReadTrackedPlayerHealthValues(int64_t* const current_value, int64_t* const max_value) {
    if (current_value == nullptr || max_value == nullptr) {
        return false;
    }

    const ActorResolveSnapshot player = g_player_resolve;
    return TryReadStatEntryValues(player.health_entry, kHealthId, current_value, max_value);
}

bool TryClampIncomingDamageToSurvivablePlayerHealth(int64_t* const value,
                                                    int64_t* const current_value,
                                                    int64_t* const max_value) {
    if (value == nullptr || *value <= 0) {
        return false;
    }

    int64_t current = 0;
    int64_t maximum = 0;
    if (!TryReadTrackedPlayerHealthValues(&current, &maximum) || current <= 1) {
        return false;
    }

    if (current_value != nullptr) {
        *current_value = current;
    }
    if (max_value != nullptr) {
        *max_value = maximum;
    }

    if (*value >= current) {
        *value = current - 1;
        return true;
    }

    return false;
}

bool TryClampEnvironmentDamageBurstToSurvivablePlayerHealth(int64_t* const value,
                                                            int64_t* const current_value,
                                                            int64_t* const max_value) {
    if (value == nullptr || *value <= 0) {
        return false;
    }

    int64_t current = 0;
    int64_t maximum = 0;
    if (!TryReadTrackedPlayerHealthValues(&current, &maximum) || current <= 1) {
        return false;
    }

    if (current_value != nullptr) {
        *current_value = current;
    }
    if (max_value != nullptr) {
        *max_value = maximum;
    }

    constexpr DWORD64 kEnvironmentDamageBurstWindowMs = 750;
    const DWORD64 now = GetTickCount64();
    const DWORD64 last_tick = g_environment_damage_burst_tick.load(std::memory_order_acquire);
    int64_t remaining = g_environment_damage_burst_remaining.load(std::memory_order_acquire);
    if (last_tick == 0 ||
        now - last_tick > kEnvironmentDamageBurstWindowMs ||
        remaining <= 0 ||
        remaining > current) {
        remaining = current - 1;
    }

    bool clamped = false;
    if (*value >= remaining) {
        *value = remaining > 0 ? remaining : 0;
        remaining = 0;
        clamped = true;
    } else {
        remaining -= *value;
    }

    g_environment_damage_burst_tick.store(now, std::memory_order_release);
    g_environment_damage_burst_remaining.store(remaining, std::memory_order_release);
    return clamped;
}

bool IsTrackedDamageParticipant(const uintptr_t candidate) {
    if (candidate < kMinimumPointerAddress) {
        return false;
    }

    const uintptr_t actor = g_player_resolve.actor;
    if (actor >= kMinimumPointerAddress && candidate == actor) {
        return true;
    }

    const uintptr_t marker = g_player_resolve.marker;
    if (marker >= kMinimumPointerAddress && candidate == marker) {
        return true;
    }

    for (const auto& participant : g_tracked_damage_participants) {
        if (participant.load(std::memory_order_acquire) == candidate) {
            return true;
        }
    }

    return false;
}

void TrackDamageParticipant(const uintptr_t candidate) {
    if (candidate < kMinimumPointerAddress || IsTrackedDamageParticipant(candidate)) {
        return;
    }

    const uint32_t index =
        g_tracked_damage_participant_cursor.fetch_add(1, std::memory_order_acq_rel) % kTrackedDamageParticipantCount;
    g_tracked_damage_participants[index].store(candidate, std::memory_order_release);
}

bool ShouldLogDamageRuntime() {
    return g_damage_logs.fetch_add(1, std::memory_order_acq_rel) < 64;
}

bool LooksLikePlayableDamageSourceActor(const uintptr_t source_actor) {
    if (source_actor < kMinimumPointerAddress) {
        return false;
    }

    ActorResolveSnapshot resolved{};
    if (!TryResolveActorResolveFromActor(source_actor, &resolved) || !resolved.valid()) {
        return false;
    }

    const ActorResolveSnapshot mount_snapshot = g_mount_resolve;
    if ((mount_snapshot.actor >= kMinimumPointerAddress && source_actor == mount_snapshot.actor) ||
        (mount_snapshot.marker >= kMinimumPointerAddress && resolved.marker == mount_snapshot.marker) ||
        (mount_snapshot.root >= kMinimumPointerAddress && resolved.root == mount_snapshot.root)) {
        return false;
    }

    int64_t stamina_current = 0;
    int64_t stamina_max = 0;
    int64_t spirit_current = 0;
    int64_t spirit_max = 0;
    if (!TryReadStatEntryValues(resolved.stamina_entry, kStaminaId, &stamina_current, &stamina_max) ||
        !TryReadStatEntryValues(resolved.spirit_entry, kSpiritId, &spirit_current, &spirit_max) ||
        stamina_max < 400000 || stamina_max > 500000 ||
        spirit_max < 100000 || spirit_max > 220000) {
        return false;
    }

    TrackDamageParticipant(source_actor);
    TrackDamageParticipant(resolved.marker);
    TrackDamageParticipant(resolved.root);

    if (ShouldLogDamageRuntime()) {
        Log("runtime: accepted outgoing playable source actor=0x%p marker=0x%p root=0x%p health=0x%p stamina=0x%p stamina_max=%lld spirit=0x%p spirit_max=%lld note=source-profile-match",
            reinterpret_cast<void*>(source_actor),
            reinterpret_cast<void*>(resolved.marker),
            reinterpret_cast<void*>(resolved.root),
            reinterpret_cast<void*>(resolved.health_entry),
            reinterpret_cast<void*>(resolved.stamina_entry),
            static_cast<long long>(stamina_max),
            reinterpret_cast<void*>(resolved.spirit_entry),
            static_cast<long long>(spirit_max));
    }

    return true;
}

bool LooksLikePlayableDamageTargetRoot(const uintptr_t target_root) {
    if (target_root < kMinimumPointerAddress) {
        return false;
    }

    ActorResolveSnapshot resolved{};
    if (!TryResolveActorResolveFromRoot(target_root, &resolved) || !resolved.valid()) {
        return false;
    }

    int64_t stamina_current = 0;
    int64_t stamina_max = 0;
    int64_t spirit_current = 0;
    int64_t spirit_max = 0;
    return TryReadStatEntryValues(resolved.stamina_entry, kStaminaId, &stamina_current, &stamina_max) &&
           TryReadStatEntryValues(resolved.spirit_entry, kSpiritId, &spirit_current, &spirit_max) &&
           stamina_max >= 400000 && stamina_max <= 500000 &&
           spirit_max >= 100000 && spirit_max <= 220000;
}

bool IsRelatedDamageParticipant(const uintptr_t candidate, const int depth) {
    if (candidate < kMinimumPointerAddress || depth < 0) {
        return false;
    }

    if (IsTrackedDamageParticipant(candidate)) {
        return true;
    }

    std::array<uintptr_t, 8> related{};
    __try {
        for (size_t index = 0; index < related.size(); ++index) {
            const uintptr_t nested = *reinterpret_cast<const uintptr_t*>(candidate + index * sizeof(uintptr_t));
            related[index] = nested;
            if (IsTrackedDamageParticipant(nested)) {
                return true;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }

    if (depth == 0) {
        return false;
    }

    for (size_t index = 0; index < related.size(); ++index) {
        const uintptr_t nested = related[index];
        if (nested < kMinimumPointerAddress || nested == candidate) {
            continue;
        }

        bool already_seen = false;
        for (size_t previous = 0; previous < index; ++previous) {
            if (related[previous] == nested) {
                already_seen = true;
                break;
            }
        }

        if (!already_seen && IsRelatedDamageParticipant(nested, depth - 1)) {
            return true;
        }
    }

    return false;
}

bool IsOutgoingPlayerDamageSource(const uintptr_t source_context) {
    if (source_context < kMinimumPointerAddress) {
        return false;
    }

    uintptr_t source_actor = 0;
    __try {
        source_actor = *reinterpret_cast<const uintptr_t*>(source_context + 0x68);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }

    if (source_actor < kMinimumPointerAddress) {
        return false;
    }

    TrackDamageParticipant(source_actor);

    const uintptr_t player_actor = g_player_resolve.damage_source;
    if (player_actor >= kMinimumPointerAddress && source_actor == player_actor) {
        return true;
    }

    if (LooksLikePlayableDamageSourceActor(source_actor)) {
        return true;
    }

    const uintptr_t mount_marker = g_mount_resolve.marker;
    if (mount_marker < kMinimumPointerAddress) {
        return false;
    }

    const uintptr_t mount_actor = g_mount_resolve.damage_source;
    if (mount_actor >= kMinimumPointerAddress && source_actor == mount_actor) {
        TrackDamageParticipant(mount_actor);
        return true;
    }

    uintptr_t source_marker = 0;
    __try {
        source_marker = *reinterpret_cast<const uintptr_t*>(source_actor + 0x20);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }

    if (source_marker >= kMinimumPointerAddress) {
        TrackDamageParticipant(source_marker);
    }

    if (source_marker == mount_marker) {
        return true;
    }

    return IsRelatedDamageParticipant(source_context, kDamageRelationDepth);
}

}  // namespace

bool IsRelevantDamageEvent(const uintptr_t target,
                           const int32_t status_id,
                           const uintptr_t source_context,
                           const int64_t delta) {
    const bool environment_health_delta =
        status_id == 0 &&
        delta > 0 &&
        source_context < kMinimumPointerAddress &&
        target >= kMinimumPointerAddress;

    if (!g_runtime_enabled.load(std::memory_order_acquire) ||
        !(environment_health_delta ? IsPlayerResourceEntriesReady() : IsPlayerRuntimeReady()) ||
        (status_id != kHealthId && !environment_health_delta) ||
        delta == 0) {
        return false;
    }

    if (environment_health_delta) {
        return true;
    }

    const uintptr_t player_target_owner = TryGetTrackedPlayerTargetOwner();
    if (target >= kMinimumPointerAddress &&
        player_target_owner >= kMinimumPointerAddress &&
        target == player_target_owner) {
        return true;
    }

    const uintptr_t mount_target_owner = TryGetTrackedMountTargetOwner();
    if (target >= kMinimumPointerAddress &&
        mount_target_owner >= kMinimumPointerAddress &&
        target == mount_target_owner) {
        return true;
    }

    return IsOutgoingPlayerDamageSource(source_context);
}

bool TryScalePlayerDamage(const uintptr_t target,
                          const int32_t status_id,
                          const uintptr_t return_address,
                          const uintptr_t source_context,
                          int64_t* const value) {
    static_cast<void>(return_address);

    if (!g_runtime_enabled.load(std::memory_order_acquire) || value == nullptr) {
        return false;
    }

    const auto& config = GetConfig();
    const bool environment_health_delta =
        status_id == 0 &&
        *value > 0 &&
        source_context < kMinimumPointerAddress &&
        target >= kMinimumPointerAddress;
    if (!config.general.enabled ||
        *value == 0 ||
        (status_id != kHealthId && !environment_health_delta) ||
        !(environment_health_delta ? IsPlayerResourceEntriesReady() : IsPlayerRuntimeReady())) {
        return false;
    }

    if (environment_health_delta) {
        const uintptr_t player_target_owner = TryGetTrackedPlayerTargetOwner();
        const bool targets_tracked_player =
            target >= kMinimumPointerAddress &&
            player_target_owner >= kMinimumPointerAddress &&
            target == player_target_owner;

        int64_t current_health = 0;
        int64_t max_health = 0;
        const bool have_health = targets_tracked_player && TryReadTrackedPlayerHealthValues(&current_health, &max_health);

        if (targets_tracked_player &&
            have_health &&
            current_health < max_health &&
            max_health > 0 &&
            *value <= (max_health / 4)) {
            if (config.health.heal_multiplier == 1.0) {
                return false;
            }

            const int64_t original_delta = *value;
            const double scaled = std::floor(static_cast<double>(*value) * config.health.heal_multiplier);
            ClampScaledSignedDelta(scaled, value);
            const int64_t missing = max_health - current_health;
            if (*value > missing) {
                *value = missing;
            }

            if (ShouldLogDamageRuntime()) {
                Log("runtime: scaled damage direction=%s target=0x%p sourceCtx=0x%p old_delta=%lld final=%lld current=%lld max=%lld multiplier=%.3f note=d59-null-source-player-heal",
                    "incoming-heal-env-null",
                    reinterpret_cast<void*>(target),
                    reinterpret_cast<void*>(source_context),
                    static_cast<long long>(original_delta),
                    static_cast<long long>(*value),
                    static_cast<long long>(current_health),
                    static_cast<long long>(max_health),
                    config.health.heal_multiplier);
            }
            return true;
        }

        double effective_multiplier = config.health.consumption_multiplier;
        if (config.damage.incoming.enabled) {
            effective_multiplier *= config.damage.incoming.multiplier;
        }

        if (effective_multiplier == 1.0) {
            return false;
        }

        const int64_t original_delta = *value;
        const double scaled = std::floor(static_cast<double>(*value) * effective_multiplier);
        ClampScaledSignedDelta(scaled, value);
        int64_t clamp_current = current_health;
        int64_t clamp_max = max_health;
        bool nonlethal_clamped = false;
        if (targets_tracked_player) {
            nonlethal_clamped =
                TryClampIncomingDamageToSurvivablePlayerHealth(value, &clamp_current, &clamp_max);
        }
        if (!nonlethal_clamped) {
            nonlethal_clamped =
                TryClampEnvironmentDamageBurstToSurvivablePlayerHealth(value, &clamp_current, &clamp_max);
        }

        if (ShouldLogDamageRuntime()) {
            Log("runtime: scaled damage direction=%s target=0x%p sourceCtx=0x%p old_delta=%lld final=%lld status=%d current=%lld max=%lld nonlethal=%d health_multiplier=%.3f incoming_enabled=%d incoming_multiplier=%.3f effective_multiplier=%.3f note=d59-resource-ready-status0-env",
                "incoming-damage-positive-env",
                reinterpret_cast<void*>(target),
                reinterpret_cast<void*>(source_context),
                static_cast<long long>(original_delta),
                static_cast<long long>(*value),
                status_id,
                static_cast<long long>(clamp_current),
                static_cast<long long>(clamp_max),
                nonlethal_clamped ? 1 : 0,
                config.health.consumption_multiplier,
                config.damage.incoming.enabled ? 1 : 0,
                config.damage.incoming.multiplier,
                effective_multiplier);
        }
        return true;
    }

    UpdateTrackedMountFromHealthRoot(target);
    if (config.mount.enabled && config.mount.lock_health && *value < 0) {
        RelockTrackedMountStats();
    }

    const uintptr_t mount_target_owner = TryGetTrackedMountTargetOwner();
    if (config.mount.enabled &&
        config.mount.lock_health &&
        *value < 0 &&
        target >= kMinimumPointerAddress &&
        mount_target_owner >= kMinimumPointerAddress &&
        target == mount_target_owner) {
        const int64_t original_delta = *value;
        const ActorResolveSnapshot mount_snapshot = g_mount_resolve;
        int64_t current_value = 0;
        int64_t max_value = 0;
        if (TryReadStatEntryValues(mount_snapshot.health_entry, kHealthId, &current_value, &max_value)) {
            const int64_t locked_value = ClampMountLockValue(config.mount.lock_value, max_value);
            *value = locked_value - current_value;

            if (ShouldLogDamageRuntime()) {
                Log("runtime: scaled damage direction=%s target=0x%p sourceCtx=0x%p old_delta=%lld current=%lld lock=%lld max=%lld final_delta=%lld",
                    "mount-lock-health",
                    reinterpret_cast<void*>(target),
                    reinterpret_cast<void*>(source_context),
                    static_cast<long long>(original_delta),
                    static_cast<long long>(current_value),
                    static_cast<long long>(locked_value),
                    static_cast<long long>(max_value),
                    static_cast<long long>(*value));
            }
            return true;
        }

        return false;
    }

    const uintptr_t player_target_owner = TryGetTrackedPlayerTargetOwner();
    if (*value < 0 &&
        target >= kMinimumPointerAddress &&
        player_target_owner >= kMinimumPointerAddress &&
        target != player_target_owner) {
        ActorResolveSnapshot resolved_target{};
        if (TryResolveActorResolveFromRoot(target, &resolved_target) &&
            resolved_target.health_entry >= kMinimumPointerAddress &&
            TryPromotePlayerCombatResourcesFromHealthEntry(resolved_target.health_entry, "damage target active playable")) {
            UpdateTrackedPlayerResourceOwner(target, "damage target active playable");
        }
    } else if (*value > 0 &&
               target >= kMinimumPointerAddress &&
               player_target_owner >= kMinimumPointerAddress &&
               target != player_target_owner &&
               LooksLikePlayableDamageTargetRoot(target) &&
               ShouldLogDamageRuntime()) {
        Log("runtime: ignored heal target as active playable candidate target=0x%p current_owner=0x%p sourceCtx=0x%p value=%lld note=heal-does-not-prove-active-character",
            reinterpret_cast<void*>(target),
            reinterpret_cast<void*>(player_target_owner),
            reinterpret_cast<void*>(source_context),
            static_cast<long long>(*value));
    }

    const uintptr_t refreshed_player_target_owner = TryGetTrackedPlayerTargetOwner();
    if (target >= kMinimumPointerAddress &&
        refreshed_player_target_owner >= kMinimumPointerAddress &&
        target == refreshed_player_target_owner &&
        *value > 0) {
        if (config.health.heal_multiplier == 1.0) {
            return false;
        }

        const double scaled = std::floor(static_cast<double>(*value) * config.health.heal_multiplier);
        if (scaled <= 0.0) {
            *value = 0;
        } else if (scaled >= static_cast<double>(std::numeric_limits<int64_t>::max())) {
            *value = std::numeric_limits<int64_t>::max();
        } else {
            *value = static_cast<int64_t>(scaled);
        }

        if (ShouldLogDamageRuntime()) {
            Log("runtime: scaled damage direction=%s target=0x%p sourceCtx=0x%p final=%lld multiplier=%.3f",
                "incoming-heal",
                reinterpret_cast<void*>(target),
                reinterpret_cast<void*>(source_context),
                static_cast<long long>(*value),
                config.health.heal_multiplier);
        }

        return true;
    }

    DamageChannelConfig channel{};
    const char* direction = nullptr;
    if (*value > 0) {
        if (target >= kMinimumPointerAddress &&
            refreshed_player_target_owner >= kMinimumPointerAddress &&
            target != refreshed_player_target_owner &&
            !LooksLikePlayableDamageTargetRoot(target) &&
            IsOutgoingPlayerDamageSource(source_context)) {
            channel = config.damage.outgoing;
            direction = "outgoing-damage-positive";
        } else {
            return false;
        }
    } else if (target >= kMinimumPointerAddress &&
        refreshed_player_target_owner >= kMinimumPointerAddress &&
        target == refreshed_player_target_owner) {
        double effective_multiplier = config.health.consumption_multiplier;
        if (config.damage.incoming.enabled) {
            effective_multiplier *= config.damage.incoming.multiplier;
        }

        if (effective_multiplier == 1.0) {
            return false;
        }

        const int64_t original_delta = *value;
        const double scaled = std::floor(static_cast<double>(*value) * effective_multiplier);
        ClampScaledSignedDelta(scaled, value);

        if (ShouldLogDamageRuntime()) {
            Log("runtime: scaled damage direction=%s target=0x%p sourceCtx=0x%p old_delta=%lld final=%lld health_multiplier=%.3f incoming_enabled=%d incoming_multiplier=%.3f effective_multiplier=%.3f",
                "incoming-damage",
                reinterpret_cast<void*>(target),
                reinterpret_cast<void*>(source_context),
                static_cast<long long>(original_delta),
                static_cast<long long>(*value),
                config.health.consumption_multiplier,
                config.damage.incoming.enabled ? 1 : 0,
                config.damage.incoming.multiplier,
                effective_multiplier);
        }
        return true;
    } else if (IsOutgoingPlayerDamageSource(source_context)) {
        channel = config.damage.outgoing;
        direction = "outgoing-damage";
    } else {
        return false;
    }

    if (!channel.enabled || channel.multiplier == 1.0) {
        return false;
    }

    const double scaled = std::floor(static_cast<double>(*value) * channel.multiplier);
    ClampScaledSignedDelta(scaled, value);

    if (ShouldLogDamageRuntime()) {
        Log("runtime: scaled damage direction=%s target=0x%p sourceCtx=0x%p final=%lld multiplier=%.3f",
            direction,
            reinterpret_cast<void*>(target),
            reinterpret_cast<void*>(source_context),
            static_cast<long long>(*value),
            channel.multiplier);
    }

    return true;
}




