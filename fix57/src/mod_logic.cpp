#include "mod_logic.h"

#include "config.h"
#include "logger.h"
#include "runtime/actor_resolve.h"
#include "runtime/mount_resolver.h"
#include "runtime/runtime_state.h"

#include <cmath>
#include <atomic>
#include <cstdint>
#include <limits>

namespace {

constexpr int64_t kDragonHealthMaxThreshold = 2500000;
constexpr int64_t kDragonStaminaMaxThreshold = 300000;
constexpr int64_t kDragonHealthMaxUpper = 100000000;
constexpr int64_t kDragonStaminaMaxUpper = 10000000;
constexpr int64_t kGroundMountHealthMaxThreshold = 10000;
constexpr int64_t kGroundMountStaminaMaxThreshold = 500;
constexpr int64_t kGroundMountHealthMaxUpper = 750000;
constexpr int64_t kGroundMountStaminaMaxUpper = 500000;
constexpr int64_t kRideableProxyHealthMaxThreshold = 500000;
constexpr int64_t kRideableProxyStaminaMaxThreshold = 50000;
constexpr int64_t kRideableProxyHealthMaxUpper = 2500000;
constexpr int64_t kRideableProxyStaminaMaxUpper = 500000;
std::atomic<std::uint32_t> g_mount_relock_logs{0};
std::atomic<std::uint32_t> g_mount_refresh_logs{0};
std::atomic<std::uint32_t> g_mount_relock_diag_logs{0};
std::atomic<std::uint32_t> g_mount_stat_component_diag_logs{0};
std::atomic<std::uint32_t> g_mount_proxy_profile_logs{0};

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

bool TryWriteStatCurrentValue(const uintptr_t entry, const int64_t value) {
    if (entry < kMinimumPointerAddress || value < 0) {
        return false;
    }

    __try {
        *reinterpret_cast<int64_t*>(entry + 0x08) = value;
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

bool IsInClosedRange(const int64_t value, const int64_t minimum, const int64_t maximum) {
    return value >= minimum && value <= maximum;
}

bool IsDragonMountMaxima(const int64_t health_max, const int64_t stamina_max) {
    return IsInClosedRange(health_max, kDragonHealthMaxThreshold, kDragonHealthMaxUpper) &&
           IsInClosedRange(stamina_max, kDragonStaminaMaxThreshold, kDragonStaminaMaxUpper);
}

bool IsGroundMountMaxima(const int64_t health_max, const int64_t stamina_max) {
    return IsInClosedRange(health_max, kGroundMountHealthMaxThreshold, kGroundMountHealthMaxUpper) &&
           IsInClosedRange(stamina_max, kGroundMountStaminaMaxThreshold, kGroundMountStaminaMaxUpper);
}

bool IsRideableProxyMaxima(const int64_t health_max, const int64_t stamina_max) {
    return IsInClosedRange(health_max, kRideableProxyHealthMaxThreshold, kRideableProxyHealthMaxUpper) &&
           IsInClosedRange(stamina_max, kRideableProxyStaminaMaxThreshold, kRideableProxyStaminaMaxUpper);
}

bool TryRelockMountStatEntry(const uintptr_t entry,
                             const int32_t expected_type,
                             const char* const stat_name,
                             const int64_t requested_lock_value) {
    int64_t current_value = 0;
    int64_t max_value = 0;
    if (!TryReadStatEntryValues(entry, expected_type, &current_value, &max_value)) {
        const auto current = g_mount_relock_diag_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 64) {
            Log("runtime: mount relock skipped stat=%s entry=0x%p reason=read-failed expected_type=%d",
                stat_name,
                reinterpret_cast<void*>(entry),
                expected_type);
        }
        return false;
    }

    const int64_t locked_value = ClampMountLockValue(requested_lock_value, max_value);
    if (current_value >= locked_value) {
        const auto current = g_mount_relock_diag_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 64) {
            Log("runtime: mount relock observed stat=%s entry=0x%p current=%lld lock=%lld max=%lld reason=already-at-or-above-lock",
                stat_name,
                reinterpret_cast<void*>(entry),
                static_cast<long long>(current_value),
                static_cast<long long>(locked_value),
                static_cast<long long>(max_value));
        }
        return false;
    }

    if (!TryWriteStatCurrentValue(entry, locked_value)) {
        const auto current = g_mount_relock_diag_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 64) {
            Log("runtime: mount relock skipped stat=%s entry=0x%p current=%lld lock=%lld max=%lld reason=write-failed",
                stat_name,
                reinterpret_cast<void*>(entry),
                static_cast<long long>(current_value),
                static_cast<long long>(locked_value),
                static_cast<long long>(max_value));
        }
        return false;
    }

    const auto current = g_mount_relock_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 64) {
        Log("runtime: relocked mount %s entry=0x%p old=%lld final=%lld max=%lld",
            stat_name,
            reinterpret_cast<void*>(entry),
            static_cast<long long>(current_value),
            static_cast<long long>(locked_value),
            static_cast<long long>(max_value));
    }

    return true;
}

bool TryReadMountProfileMaxima(const ActorResolveSnapshot& snapshot,
                               int64_t* const health_max,
                               int64_t* const stamina_max) {
    if (!snapshot.valid() || health_max == nullptr || stamina_max == nullptr) {
        return false;
    }

    int64_t health_current = 0;
    int64_t stamina_current = 0;
    return TryReadStatEntryValues(snapshot.health_entry, kHealthId, &health_current, health_max) &&
           TryReadStatEntryValues(snapshot.stamina_entry, kStaminaId, &stamina_current, stamina_max);
}

bool IsDragonMountProfile(const ActorResolveSnapshot& snapshot) {
    if (!HasExpectedHealthStaminaLayout(snapshot.health_entry, snapshot.stamina_entry)) {
        return false;
    }

    int64_t health_max = 0;
    int64_t stamina_max = 0;
    if (!TryReadMountProfileMaxima(snapshot, &health_max, &stamina_max)) {
        return false;
    }

    return IsDragonMountMaxima(health_max, stamina_max);
}

bool IsSupportedMountProfile(const ActorResolveSnapshot& snapshot) {
    if (!HasExpectedHealthStaminaLayout(snapshot.health_entry, snapshot.stamina_entry)) {
        return false;
    }

    int64_t health_max = 0;
    int64_t stamina_max = 0;
    if (!TryReadMountProfileMaxima(snapshot, &health_max, &stamina_max)) {
        return false;
    }

    return IsDragonMountMaxima(health_max, stamina_max) ||
           IsGroundMountMaxima(health_max, stamina_max);
}

bool IsCurrentTrackedRideableProxyProfile(const ActorResolveSnapshot& snapshot) {
    if (!snapshot.valid() ||
        !HasExpectedHealthStaminaLayout(snapshot.health_entry, snapshot.stamina_entry)) {
        return false;
    }

    const ActorResolveSnapshot current = g_mount_resolve;
    const ActorResolveSnapshot player = g_player_resolve;
    if (!current.valid() ||
        snapshot.marker != current.marker ||
        snapshot.root != current.root ||
        snapshot.health_entry != current.health_entry ||
        snapshot.stamina_entry != current.stamina_entry ||
        snapshot.marker == player.marker ||
        snapshot.root == player.root) {
        return false;
    }

    int64_t health_max = 0;
    int64_t stamina_max = 0;
    if (!TryReadMountProfileMaxima(snapshot, &health_max, &stamina_max) ||
        !IsRideableProxyMaxima(health_max, stamina_max)) {
        return false;
    }

    const auto current_log = g_mount_proxy_profile_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current_log < 24) {
        Log("runtime: accepted rideable proxy mount profile actor=0x%p root=0x%p marker=0x%p health=0x%p health_max=%lld stamina=0x%p stamina_max=%lld",
            reinterpret_cast<void*>(snapshot.actor),
            reinterpret_cast<void*>(snapshot.root),
            reinterpret_cast<void*>(snapshot.marker),
            reinterpret_cast<void*>(snapshot.health_entry),
            static_cast<long long>(health_max),
            reinterpret_cast<void*>(snapshot.stamina_entry),
            static_cast<long long>(stamina_max));
    }

    return true;
}

bool IsSupportedTrackedMountProfile(const ActorResolveSnapshot& snapshot) {
    return IsSupportedMountProfile(snapshot) || IsCurrentTrackedRideableProxyProfile(snapshot);
}

const char* MountProfileName(const ActorResolveSnapshot& snapshot) {
    int64_t health_max = 0;
    int64_t stamina_max = 0;
    if (!TryReadMountProfileMaxima(snapshot, &health_max, &stamina_max)) {
        return "unknown";
    }

    if (!HasExpectedHealthStaminaLayout(snapshot.health_entry, snapshot.stamina_entry)) {
        return "rejected-layout";
    }

    if (IsDragonMountMaxima(health_max, stamina_max)) {
        return "dragon";
    }

    if (IsGroundMountMaxima(health_max, stamina_max)) {
        return "ground";
    }

    if (IsRideableProxyMaxima(health_max, stamina_max)) {
        return "rideable-proxy";
    }

    return "rejected";
}

bool TryResolveMountSnapshotFromContextRoot(const uintptr_t context_root,
                                           ActorResolveSnapshot* const resolved,
                                           const bool allow_ground_mount_profile) {
    if (resolved == nullptr || context_root < kMinimumPointerAddress) {
        return false;
    }

    const ActorResolveSnapshot player_snapshot = g_player_resolve;
    if (!player_snapshot.valid()) {
        return false;
    }

    if (!TryResolveActorResolveFromContextRoot(context_root, resolved, player_snapshot)) {
        return false;
    }

    // Health-root damage callbacks can point at enemies/destructibles.  Keep
    // those broad callbacks dragon-only to avoid making ordinary enemies
    // invulnerable.  Ground-mount profiles are accepted only from mount-owned
    // stat/stamina context or from the explicit mount marker chain.
    return allow_ground_mount_profile ? IsSupportedMountProfile(*resolved) : IsDragonMountProfile(*resolved);
}

void ResetTrackedMountIfStillCurrent(const ActorResolveSnapshot& expected) {
    std::lock_guard lock(g_state_mutex);
    if (g_mount_resolve.actor == expected.actor &&
        g_mount_resolve.marker == expected.marker &&
        g_mount_resolve.root == expected.root) {
        ResetTrackedMountLocked();
    }
}

void LogMountRefreshFallback(const char* const reason, const ActorResolveSnapshot& snapshot) {
    const auto current = g_mount_refresh_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current >= 32) {
        return;
    }

    int64_t health_max = 0;
    int64_t stamina_max = 0;
    TryReadMountProfileMaxima(snapshot, &health_max, &stamina_max);
    Log("runtime: kept tracked mount alive reason=%s actor=0x%p root=0x%p status_marker=0x%p health=0x%p health_max=%lld stamina=0x%p stamina_max=%lld",
        reason,
        reinterpret_cast<void*>(snapshot.actor),
        reinterpret_cast<void*>(snapshot.root),
        reinterpret_cast<void*>(snapshot.marker),
        reinterpret_cast<void*>(snapshot.health_entry),
        static_cast<long long>(health_max),
        reinterpret_cast<void*>(snapshot.stamina_entry),
        static_cast<long long>(stamina_max));
}

bool TryKeepCurrentMountSnapshotAlive(const ActorResolveSnapshot& current,
                                      ActorResolveSnapshot* const refreshed,
                                      const char* const reason) {
    if (!IsSupportedTrackedMountProfile(current)) {
        ResetTrackedMountIfStillCurrent(current);
        return false;
    }

    {
        std::lock_guard lock(g_state_mutex);
        if (g_mount_resolve.actor != current.actor ||
            g_mount_resolve.marker != current.marker ||
            g_mount_resolve.root != current.root) {
            return false;
        }

        g_mount_last_seen_tick.store(GetTickCount64(), std::memory_order_release);
    }

    *refreshed = current;
    LogMountRefreshFallback(reason, current);
    return true;
}

bool TryRefreshTrackedMountSnapshot(ActorResolveSnapshot* const refreshed) {
    if (refreshed == nullptr) {
        return false;
    }

    const ActorResolveSnapshot current = g_mount_resolve;
    if (!current.valid()) {
        return false;
    }

    if (!IsSupportedTrackedMountProfile(current)) {
        ResetTrackedMountIfStillCurrent(current);
        return false;
    }

    ActorResolveSnapshot latest{};
    if (!TryResolveActorResolveFromMarker(current.marker, &latest, current.actor)) {
        return TryKeepCurrentMountSnapshotAlive(current, refreshed, "cached-marker-unavailable");
    }

    const ActorResolveSnapshot player_snapshot = g_player_resolve;
    if (latest.marker != current.marker ||
        latest.marker == player_snapshot.marker ||
        latest.root == player_snapshot.root ||
        !IsSupportedTrackedMountProfile(latest)) {
        return TryKeepCurrentMountSnapshotAlive(current, refreshed, "cached-marker-mismatch");
    }

    {
        std::lock_guard lock(g_state_mutex);
        if (g_mount_resolve.marker != current.marker) {
            return false;
        }

        g_mount_resolve = latest;
        g_mount_last_seen_tick.store(GetTickCount64(), std::memory_order_release);
    }

    *refreshed = latest;
    return true;
}

}  // namespace

void ResetRuntimeState() {
    std::lock_guard lock(g_state_mutex);
    g_player_resolve = {};
    g_mount_resolve = {};
    ResetTrackedEntriesLocked();
    ResetTrackedDamageParticipantsLocked();
    g_runtime_enabled.store(true, std::memory_order_release);
}

void DisableRuntimeProcessing() {
    g_runtime_enabled.store(false, std::memory_order_release);
}

bool StartMountResolver() {
    return StartMountResolverLoop();
}

void StopMountResolver() {
    StopMountResolverLoop();
}

bool TryScaleItemGain(const int64_t amount, int64_t* const value) {
    if (!g_runtime_enabled.load(std::memory_order_acquire) || value == nullptr) {
        return false;
    }

    const auto& config = GetConfig();
    if (!config.general.enabled || config.items.gain_multiplier == 1.0 || amount <= 0) {
        return false;
    }

    const double scaled = std::floor(static_cast<double>(amount) * config.items.gain_multiplier);
    if (scaled <= 0.0) {
        *value = 0;
        return true;
    }

    if (scaled >= static_cast<double>(std::numeric_limits<int64_t>::max())) {
        *value = std::numeric_limits<int64_t>::max();
        return true;
    }

    *value = static_cast<int64_t>(scaled);
    return *value != amount;
}

void UpdateTrackedMountFromHealthRoot(const uintptr_t root) {
    ActorResolveSnapshot resolved{};
    if (!TryResolveMountSnapshotFromContextRoot(root, &resolved, false)) {
        return;
    }

    UpdateTrackedMountStatusComponent(resolved.actor, resolved.marker);
}

void UpdateTrackedMountFromStaminaContext(const uintptr_t stamina_entry, const uintptr_t context_root) {
    if (stamina_entry < kMinimumPointerAddress) {
        return;
    }

    ActorResolveSnapshot resolved{};
    if (!TryResolveMountSnapshotFromContextRoot(context_root, &resolved, true)) {
        return;
    }

    if (resolved.stamina_entry != stamina_entry) {
        return;
    }

    UpdateTrackedMountStatusComponent(resolved.actor, resolved.marker);
}

void UpdateTrackedMountFromStatComponent(const uintptr_t entry, const uintptr_t component) {
    if (entry < kMinimumPointerAddress || component < kMinimumPointerAddress) {
        return;
    }

    const auto& config = GetConfig();
    if (!config.general.enabled ||
        !config.mount.enabled ||
        (!config.mount.lock_health && !config.mount.lock_stamina)) {
        return;
    }

    const ActorResolveSnapshot player_snapshot = g_player_resolve;
    if (!player_snapshot.valid() || component == player_snapshot.root) {
        return;
    }

    ActorResolveSnapshot resolved{};
    if (!TryResolveActorResolveFromContextRoot(component, &resolved, player_snapshot)) {
        return;
    }

    if (entry != resolved.health_entry &&
        entry != resolved.stamina_entry &&
        entry != resolved.spirit_entry) {
        const auto current = g_mount_stat_component_diag_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 48) {
            int64_t health_max = 0;
            int64_t stamina_max = 0;
            TryReadMountProfileMaxima(resolved, &health_max, &stamina_max);
            Log("runtime: mount stat-component rejected reason=entry-not-in-resolved-profile entry=0x%p component=0x%p root=0x%p health=0x%p health_max=%lld stamina=0x%p stamina_max=%lld spirit=0x%p profile=%s",
                reinterpret_cast<void*>(entry),
                reinterpret_cast<void*>(component),
                reinterpret_cast<void*>(resolved.root),
                reinterpret_cast<void*>(resolved.health_entry),
                static_cast<long long>(health_max),
                reinterpret_cast<void*>(resolved.stamina_entry),
                static_cast<long long>(stamina_max),
                reinterpret_cast<void*>(resolved.spirit_entry),
                MountProfileName(resolved));
        }
        return;
    }

    if (!IsSupportedTrackedMountProfile(resolved)) {
        const auto current = g_mount_stat_component_diag_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 48) {
            int64_t health_max = 0;
            int64_t stamina_max = 0;
            TryReadMountProfileMaxima(resolved, &health_max, &stamina_max);
            Log("runtime: mount stat-component rejected reason=unsupported-profile entry=0x%p component=0x%p root=0x%p health=0x%p health_max=%lld stamina=0x%p stamina_max=%lld spirit=0x%p profile=%s",
                reinterpret_cast<void*>(entry),
                reinterpret_cast<void*>(component),
                reinterpret_cast<void*>(resolved.root),
                reinterpret_cast<void*>(resolved.health_entry),
                static_cast<long long>(health_max),
                reinterpret_cast<void*>(resolved.stamina_entry),
                static_cast<long long>(stamina_max),
                reinterpret_cast<void*>(resolved.spirit_entry),
                MountProfileName(resolved));
        }
        return;
    }

    UpdateTrackedMountStatusComponent(resolved.actor, resolved.marker);
}

void RelockTrackedMountStats() {
    if (!g_runtime_enabled.load(std::memory_order_acquire)) {
        return;
    }

    const auto& config = GetConfig();
    if (!config.general.enabled || !config.mount.enabled) {
        return;
    }

    ActorResolveSnapshot mount_snapshot{};
    if (!TryRefreshTrackedMountSnapshot(&mount_snapshot)) {
        return;
    }

    if (config.mount.lock_health) {
        TryRelockMountStatEntry(mount_snapshot.health_entry,
                                kHealthId,
                                "health",
                                config.mount.lock_value);
    }

    if (config.mount.lock_stamina) {
        TryRelockMountStatEntry(mount_snapshot.stamina_entry,
                                kStaminaId,
                                "stamina",
                                config.mount.lock_value);
        TryRelockMountStatEntry(mount_snapshot.spirit_entry,
                                kSpiritId,
                                "spirit-stamina",
                                config.mount.lock_value);
    }
}

void UpdateTrackedMountStatusComponent(const uintptr_t actor, const uintptr_t marker) {
    if (marker < kMinimumPointerAddress) {
        return;
    }

    const uintptr_t player_marker = g_player_resolve.marker;
    if (marker == player_marker) {
        return;
    }

    ActorResolveSnapshot resolved{};
    if (!TryResolveActorResolveFromMarker(marker, &resolved, actor)) {
        return;
    }

    if (resolved.marker == g_player_resolve.marker || resolved.root == g_player_resolve.root) {
        return;
    }

    if (!IsSupportedTrackedMountProfile(resolved)) {
        return;
    }

    const ULONGLONG now = GetTickCount64();
    const ActorResolveSnapshot current_mount = g_mount_resolve;
    if (current_mount.actor == resolved.actor && current_mount.marker == resolved.marker) {
        g_mount_last_seen_tick.store(now, std::memory_order_release);
        RelockTrackedMountStats();
        return;
    }

    {
        std::lock_guard lock(g_state_mutex);
        if (g_mount_resolve.actor == resolved.actor && g_mount_resolve.marker == resolved.marker) {
            g_mount_last_seen_tick.store(now, std::memory_order_release);
        } else {
            g_mount_resolve = resolved;
            g_mount_last_seen_tick.store(now, std::memory_order_release);

            const auto current = g_actor_resolve_logs.fetch_add(1, std::memory_order_acq_rel);
            if (current < 24) {
                int64_t health_max = 0;
                int64_t stamina_max = 0;
                TryReadMountProfileMaxima(resolved, &health_max, &stamina_max);
                Log("runtime: tracked mount profile=%s actor=0x%p root=0x%p status_marker=0x%p health=0x%p health_max=%lld stamina=0x%p stamina_max=%lld spirit=0x%p",
                    MountProfileName(resolved),
                    reinterpret_cast<void*>(resolved.actor),
                    reinterpret_cast<void*>(resolved.root),
                    reinterpret_cast<void*>(resolved.marker),
                    reinterpret_cast<void*>(resolved.health_entry),
                    static_cast<long long>(health_max),
                    reinterpret_cast<void*>(resolved.stamina_entry),
                    static_cast<long long>(stamina_max),
                    reinterpret_cast<void*>(resolved.spirit_entry));
            }
        }
    }

    RelockTrackedMountStats();
}

void UpdateTrackedPlayerStatusComponent(const uintptr_t actor, const uintptr_t component) {
    if (component < kMinimumPointerAddress) {
        return;
    }

    ActorResolveSnapshot resolved{};
    if (!TryResolveActorResolveFromMarker(component, &resolved, actor)) {
        return;
    }

    bool should_refresh_mount = false;

    {
        std::lock_guard lock(g_state_mutex);

        const ActorResolveSnapshot existing = g_player_resolve;
        if (existing.marker == component && existing.health_entry == resolved.health_entry) {
            return;
        }

        if (existing.health_entry >= kMinimumPointerAddress &&
            resolved.health_entry >= kMinimumPointerAddress &&
            existing.health_entry != resolved.health_entry) {
            const auto current = g_actor_resolve_logs.fetch_add(1, std::memory_order_acq_rel);
            if (current < 24) {
                Log("runtime: ignored player status update with different health current_health=0x%p resolved_health=0x%p status_marker=0x%p",
                    reinterpret_cast<void*>(existing.health_entry),
                    reinterpret_cast<void*>(resolved.health_entry),
                    reinterpret_cast<void*>(resolved.marker));
            }
            return;
        }

        ActorResolveSnapshot merged = resolved;
        if (existing.health_entry >= kMinimumPointerAddress &&
            resolved.health_entry == existing.health_entry) {
            merged = existing;
            if (merged.actor < kMinimumPointerAddress) {
                merged.actor = resolved.actor;
            }
            if (merged.marker < kMinimumPointerAddress) {
                merged.marker = resolved.marker;
            }
            if (merged.root < kMinimumPointerAddress) {
                merged.root = resolved.root;
            }
            if (merged.stamina_entry < kMinimumPointerAddress) {
                merged.stamina_entry = resolved.stamina_entry;
            }
            if (merged.spirit_entry < kMinimumPointerAddress) {
                merged.spirit_entry = resolved.spirit_entry;
            }
        } else {
            ResetTrackedEntriesLocked();
            ResetTrackedMountLocked();
            ResetTrackedDamageParticipantsLocked();
        }

        g_player_resolve = merged;
        should_refresh_mount = true;

        Log("runtime: tracked player actor=0x%p status_marker=0x%p stat_root=0x%p",
            reinterpret_cast<void*>(merged.actor),
            reinterpret_cast<void*>(merged.marker),
            reinterpret_cast<void*>(merged.root));
    }

    // Refreshing the mount can call UpdateTrackedMountStatusComponent(), which
    // also uses g_state_mutex.  Keep this outside the player-state critical
    // section to avoid self-deadlock when the pointer chain resolves a mount.
    if (should_refresh_mount) {
        RefreshTrackedMountFromPlayerActor();
    }
}


uintptr_t GetTrackedPlayerStatRoot() {
    return g_player_resolve.root;
}

uintptr_t GetTrackedMountActor() {
    return g_mount_resolve.actor;
}

uintptr_t GetTrackedMountStatRoot() {
    return g_mount_resolve.root;
}

uintptr_t GetTrackedMountStatusMarker() {
    return g_mount_resolve.marker;
}

uintptr_t GetTrackedPlayerActor() {
    return g_player_resolve.actor;
}

uintptr_t GetTrackedPlayerHealthEntry() {
    return g_player_resolve.health_entry;
}

uintptr_t GetTrackedPlayerSpiritEntry() {
    return g_player_resolve.spirit_entry;
}

uintptr_t GetTrackedPlayerStaminaEntry() {
    return g_player_resolve.stamina_entry;
}

uintptr_t GetTrackedPlayerStatusMarker() {
    return g_player_resolve.marker;
}
