#include "mod_logic.h"

#include "config.h"
#include "logger.h"
#include "runtime/actor_resolve.h"
#include "runtime/mount_resolver.h"
#include "runtime/runtime_state.h"
#include "runtime/stat_logic.h"

#include <cmath>
#include <atomic>
#include <cstring>
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
constexpr int64_t kGroundMountStaminaMaxUpper = 10000000;
constexpr int64_t kRideableProxyHealthMaxThreshold = 500000;
constexpr int64_t kRideableProxyStaminaMaxThreshold = 50000;
constexpr int64_t kRideableProxyHealthMaxUpper = 2500000;
constexpr int64_t kRideableProxyStaminaMaxUpper = 500000;
constexpr int64_t kSpecialMountStaminaMinMax = 180000;
constexpr int64_t kSpecialMountStaminaMaxMax = 299999;
std::atomic<std::uint32_t> g_mount_relock_logs{0};
std::atomic<std::uint32_t> g_mount_refresh_logs{0};
std::atomic<std::uint32_t> g_mount_relock_diag_logs{0};
std::atomic<std::uint32_t> g_mount_stat_component_diag_logs{0};
std::atomic<std::uint32_t> g_mount_proxy_profile_logs{0};
std::atomic<std::uint32_t> g_loose_mount_stamina_logs{0};

struct PendingMountCandidate {
    uintptr_t actor = 0;
    uintptr_t marker = 0;
    uintptr_t root = 0;
    uintptr_t health_entry = 0;
    uintptr_t stamina_entry = 0;
    std::uint32_t hits = 0;
    ULONGLONG first_seen_tick = 0;
    ULONGLONG last_seen_tick = 0;
};

PendingMountCandidate g_pending_mount_candidate{};

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
bool HasReadableTrackedPlayerCombatPair() {
    const ActorResolveSnapshot existing = g_player_resolve;
    if (existing.stamina_entry < kMinimumPointerAddress ||
        existing.spirit_entry < kMinimumPointerAddress) {
        return false;
    }

    int64_t current_value = 0;
    int64_t stamina_max = 0;
    int64_t spirit_max = 0;
    return TryReadStatEntryValues(existing.stamina_entry, kStaminaId, &current_value, &stamina_max) &&
           TryReadStatEntryValues(existing.spirit_entry, kSpiritId, &current_value, &spirit_max) &&
           IsInClosedRange(stamina_max, 400000, 500000) &&
           IsInClosedRange(spirit_max, 100000, 220000);
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

bool LooksLikePlayerCombatResourcePair(const ActorResolveSnapshot& snapshot) {
    if (!snapshot.valid() || snapshot.stamina_entry < kMinimumPointerAddress) {
        return false;
    }

    int64_t stamina_current = 0;
    int64_t stamina_max = 0;
    if (!TryReadStatEntryValues(snapshot.stamina_entry, kStaminaId, &stamina_current, &stamina_max) ||
        !IsInClosedRange(stamina_max, 400000, 500000)) {
        return false;
    }

    const uintptr_t adjacent_spirit =
        snapshot.stamina_entry + (kSpiritEntryOffsetFromHealth - kStaminaEntryOffsetFromHealth);
    int64_t spirit_current = 0;
    int64_t spirit_max = 0;
    return TryReadStatEntryValues(adjacent_spirit, kSpiritId, &spirit_current, &spirit_max) &&
           IsInClosedRange(spirit_max, 100000, 220000);
}

bool IsSupportedMountStaminaMax(const int64_t stamina_max) {
    return IsInClosedRange(stamina_max, kGroundMountStaminaMaxThreshold, kGroundMountStaminaMaxUpper) ||
           IsInClosedRange(stamina_max, kRideableProxyStaminaMaxThreshold, kRideableProxyStaminaMaxUpper) ||
           IsInClosedRange(stamina_max, kDragonStaminaMaxThreshold, kDragonStaminaMaxUpper);
}

bool IsSameMountIdentity(const ActorResolveSnapshot& left, const ActorResolveSnapshot& right) {
    return left.actor == right.actor &&
           left.marker == right.marker &&
           left.root == right.root;
}

bool IsSameMountEntries(const ActorResolveSnapshot& left, const ActorResolveSnapshot& right) {
    return left.health_entry == right.health_entry &&
           left.stamina_entry == right.stamina_entry &&
           left.spirit_entry == right.spirit_entry;
}

bool OverlapsTrustedMountResolve(const ActorResolveSnapshot& snapshot) {
    const ActorResolveSnapshot mount = g_mount_resolve;
    if (!snapshot.valid() ||
        !mount.valid() ||
        g_mount_track_confidence.load(std::memory_order_acquire) < 3) {
        return false;
    }

    return snapshot.marker == mount.marker ||
           snapshot.root == mount.root ||
           snapshot.health_entry == mount.health_entry ||
           snapshot.stamina_entry == mount.stamina_entry ||
           snapshot.spirit_entry == mount.spirit_entry;
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

    if (IsPlayerTrackedStatEntry(ClassifyTrackedStatEntry(entry))) {
        const auto current = g_mount_relock_diag_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 64) {
            Log("runtime: mount relock skipped stat=%s entry=0x%p reason=tracked-player-resource",
                stat_name,
                reinterpret_cast<void*>(entry));
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
        !HasExpectedHealthStaminaLayout(snapshot.health_entry, snapshot.stamina_entry) ||
        LooksLikePlayerCombatResourcePair(snapshot)) {
        return false;
    }

    const ActorResolveSnapshot current = g_mount_resolve;
    const ActorResolveSnapshot player = g_player_resolve;
    if (g_mount_track_confidence.load(std::memory_order_acquire) < 3 ||
        !current.valid() ||
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

bool IsRideableProxyMountCandidateProfile(const ActorResolveSnapshot& snapshot) {
    if (!snapshot.valid() ||
        !HasExpectedHealthStaminaLayout(snapshot.health_entry, snapshot.stamina_entry) ||
        LooksLikePlayerCombatResourcePair(snapshot)) {
        return false;
    }

    const ActorResolveSnapshot player = g_player_resolve;
    if (snapshot.marker == player.marker || snapshot.root == player.root) {
        return false;
    }

    int64_t health_max = 0;
    int64_t stamina_max = 0;
    return TryReadMountProfileMaxima(snapshot, &health_max, &stamina_max) &&
           IsRideableProxyMaxima(health_max, stamina_max);
}

bool IsSpecialMountProfile(const ActorResolveSnapshot& snapshot) {
    int64_t health_max = 0;
    int64_t stamina_max = 0;
    return TryReadMountProfileMaxima(snapshot, &health_max, &stamina_max) &&
           IsRideableProxyMaxima(health_max, stamina_max) &&
           stamina_max >= kSpecialMountStaminaMinMax &&
           stamina_max <= kSpecialMountStaminaMaxMax;
}

bool IsRideableProxyTrackedProfile(const ActorResolveSnapshot& snapshot) {
    int64_t health_max = 0;
    int64_t stamina_max = 0;
    return TryReadMountProfileMaxima(snapshot, &health_max, &stamina_max) &&
           IsRideableProxyMaxima(health_max, stamina_max);
}

bool ShouldLockSnapshotStamina(const ModConfig& config,
                               const ActorResolveSnapshot& snapshot,
                               int64_t* const lock_value,
                               const char** const stat_name) {
    if (lock_value == nullptr || stat_name == nullptr || !config.mount.enabled) {
        return false;
    }

    if (LooksLikePlayerCombatResourcePair(snapshot)) {
        const auto current = g_mount_relock_diag_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 96) {
            Log("runtime: mount stamina relock skipped reason=player-shaped-resource-pair actor=0x%p root=0x%p stamina=0x%p spirit=0x%p",
                reinterpret_cast<void*>(snapshot.actor),
                reinterpret_cast<void*>(snapshot.root),
                reinterpret_cast<void*>(snapshot.stamina_entry),
                reinterpret_cast<void*>(snapshot.spirit_entry));
        }
        return false;
    }

    if (IsSpecialMountProfile(snapshot)) {
        *lock_value = config.mount.special_lock_value;
        *stat_name = "special-stamina";
        return config.mount.special_enabled && config.mount.lock_special_stamina;
    }

    *lock_value = config.mount.lock_value;
    *stat_name = "stamina";
    return config.mount.lock_stamina;
}

bool ShouldLockSnapshotHealth(const ModConfig& config,
                              const ActorResolveSnapshot& snapshot,
                              int64_t* const lock_value,
                              const char** const stat_name) {
    if (lock_value == nullptr || stat_name == nullptr || !config.mount.enabled) {
        return false;
    }

    if (IsRideableProxyTrackedProfile(snapshot)) {
        const auto current = g_mount_relock_diag_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 64) {
            int64_t health_max = 0;
            int64_t stamina_max = 0;
            TryReadMountProfileMaxima(snapshot, &health_max, &stamina_max);
            Log("runtime: mount health lock skipped reason=rideable-proxy-unsafe actor=0x%p root=0x%p health=0x%p health_max=%lld stamina=0x%p stamina_max=%lld",
                reinterpret_cast<void*>(snapshot.actor),
                reinterpret_cast<void*>(snapshot.root),
                reinterpret_cast<void*>(snapshot.health_entry),
                static_cast<long long>(health_max),
                reinterpret_cast<void*>(snapshot.stamina_entry),
                static_cast<long long>(stamina_max));
        }
        return false;
    }

    if (IsSpecialMountProfile(snapshot)) {
        *lock_value = config.mount.special_lock_value;
        *stat_name = "special-health";
        return config.mount.special_enabled && config.mount.lock_special_health;
    }

    *lock_value = config.mount.lock_value;
    *stat_name = "health";
    return config.mount.lock_health;
}

bool IsSupportedTrackedMountProfile(const ActorResolveSnapshot& snapshot) {
    return IsSupportedMountProfile(snapshot) ||
           IsCurrentTrackedRideableProxyProfile(snapshot) ||
           IsRideableProxyMountCandidateProfile(snapshot);
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

void LogDroppedTrackedMount(const char* const reason, const ActorResolveSnapshot& snapshot) {
    const auto current = g_mount_refresh_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current >= 32) {
        return;
    }

    int64_t health_max = 0;
    int64_t stamina_max = 0;
    TryReadMountProfileMaxima(snapshot, &health_max, &stamina_max);
    Log("runtime: dropped tracked mount reason=%s actor=0x%p root=0x%p status_marker=0x%p health=0x%p health_max=%lld stamina=0x%p stamina_max=%lld profile=%s",
        reason,
        reinterpret_cast<void*>(snapshot.actor),
        reinterpret_cast<void*>(snapshot.root),
        reinterpret_cast<void*>(snapshot.marker),
        reinterpret_cast<void*>(snapshot.health_entry),
        static_cast<long long>(health_max),
        reinterpret_cast<void*>(snapshot.stamina_entry),
        static_cast<long long>(stamina_max),
        MountProfileName(snapshot));
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
        LogDroppedTrackedMount("cached-marker-mismatch", latest.valid() ? latest : current);
        ResetTrackedMountIfStillCurrent(current);
        return false;
    }

    {
        std::lock_guard lock(g_state_mutex);
        if (g_mount_resolve.marker != current.marker) {
            return false;
        }

        g_mount_resolve = latest;
        g_mount_last_seen_tick.store(GetTickCount64(), std::memory_order_release);
        g_mount_track_confidence.store(3, std::memory_order_release);
    }

    *refreshed = latest;
    return true;
}

bool ShouldAcceptOpportunisticMountCandidate(const ActorResolveSnapshot& resolved) {
    const ActorResolveSnapshot current = g_mount_resolve;
    if (current.valid() && IsSameMountIdentity(current, resolved)) {
        g_mount_track_confidence.store(3, std::memory_order_release);
        return true;
    }

    if (IsDragonMountProfile(resolved)) {
        g_mount_track_confidence.store(3, std::memory_order_release);
        return true;
    }

    const ULONGLONG now = GetTickCount64();
    std::lock_guard lock(g_state_mutex);
    const bool same_pending =
        g_pending_mount_candidate.actor == resolved.actor &&
        g_pending_mount_candidate.marker == resolved.marker &&
        g_pending_mount_candidate.root == resolved.root &&
        g_pending_mount_candidate.health_entry == resolved.health_entry &&
        g_pending_mount_candidate.stamina_entry == resolved.stamina_entry &&
        g_pending_mount_candidate.last_seen_tick != 0 &&
        now - g_pending_mount_candidate.last_seen_tick < 8000;

    if (!same_pending) {
        g_pending_mount_candidate = {
            resolved.actor,
            resolved.marker,
            resolved.root,
            resolved.health_entry,
            resolved.stamina_entry,
            1,
            now,
            now,
        };
    } else {
        ++g_pending_mount_candidate.hits;
        g_pending_mount_candidate.last_seen_tick = now;
    }

    const auto current_log = g_mount_candidate_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current_log < 32) {
        Log("runtime: pending mount candidate profile=%s hits=%u actor=0x%p root=0x%p status_marker=0x%p health=0x%p stamina=0x%p",
            MountProfileName(resolved),
            g_pending_mount_candidate.hits,
            reinterpret_cast<void*>(resolved.actor),
            reinterpret_cast<void*>(resolved.root),
            reinterpret_cast<void*>(resolved.marker),
            reinterpret_cast<void*>(resolved.health_entry),
            reinterpret_cast<void*>(resolved.stamina_entry));
    }

    if (g_pending_mount_candidate.hits < 3) {
        return false;
    }

    g_mount_track_confidence.store(g_pending_mount_candidate.hits, std::memory_order_release);
    return true;
}

}  // namespace
bool HasReadableTrackedPlayerCombatPairForHooks() {
    return HasReadableTrackedPlayerCombatPair();
}
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
    if (TryResolveMountSnapshotFromContextRoot(context_root, &resolved, true) &&
        resolved.stamina_entry == stamina_entry) {
        UpdateTrackedMountStatusComponent(resolved.actor, resolved.marker);
        return;
    }

    // Special mounts can emit stamina costs through a context that does not
    // resolve into a full health/stamina profile.  Keep the fallback narrow:
    // never claim the player stamina entry, and only accept stat type 19 values
    // whose maximum is in a known mount stamina range.
    if (IsPlayerTrackedStatEntry(ClassifyTrackedStatEntry(stamina_entry))) {
        return;
    }

    int64_t current_value = 0;
    int64_t max_value = 0;
    if (!TryReadStatEntryValues(stamina_entry, kStaminaId, &current_value, &max_value) ||
        !IsSupportedMountStaminaMax(max_value)) {
        return;
    }

    const uintptr_t adjacent_spirit =
        stamina_entry + (kSpiritEntryOffsetFromHealth - kStaminaEntryOffsetFromHealth);
    int64_t adjacent_spirit_current = 0;
    int64_t adjacent_spirit_max = 0;
    if (max_value >= 400000 &&
        max_value <= 500000 &&
        TryReadStatEntryValues(adjacent_spirit, kSpiritId, &adjacent_spirit_current, &adjacent_spirit_max) &&
        adjacent_spirit_max >= 100000 &&
        adjacent_spirit_max <= 220000) {
        if (IsPostRematchDetachedPlayerStaminaMirror(stamina_entry)) {
            const auto current = g_loose_mount_stamina_logs.fetch_add(1, std::memory_order_acq_rel);
            if (current < 32) {
                Log("runtime: accepted loose post-rematch player stamina mirror entry=0x%p spirit=0x%p stamina_max=%lld spirit_max=%lld context=0x%p",
                    reinterpret_cast<void*>(stamina_entry),
                    reinterpret_cast<void*>(adjacent_spirit),
                    static_cast<long long>(max_value),
                    static_cast<long long>(adjacent_spirit_max),
                    reinterpret_cast<void*>(context_root));
            }
            return;
        }

        if (HasReadableTrackedPlayerCombatPair() && stamina_entry != GetTrackedPlayerStaminaEntry()) {
            const auto current = g_loose_mount_stamina_logs.fetch_add(1, std::memory_order_acq_rel);
            if (current < 32) {
                Log("runtime: ignored loose player-shaped stamina because active player pair is already tracked entry=0x%p spirit=0x%p tracked_stamina=0x%p tracked_spirit=0x%p context=0x%p",
                    reinterpret_cast<void*>(stamina_entry),
                    reinterpret_cast<void*>(adjacent_spirit),
                    reinterpret_cast<void*>(GetTrackedPlayerStaminaEntry()),
                    reinterpret_cast<void*>(GetTrackedPlayerSpiritEntry()),
                    reinterpret_cast<void*>(context_root));
            }
            return;
        }

        const auto current = g_loose_mount_stamina_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 32) {
            Log("runtime: rejected loose player-shaped stamina during mount update entry=0x%p spirit=0x%p stamina_max=%lld spirit_max=%lld context=0x%p note=wait-for-player-status-rebind",
                reinterpret_cast<void*>(stamina_entry),
                reinterpret_cast<void*>(adjacent_spirit),
                static_cast<long long>(max_value),
                static_cast<long long>(adjacent_spirit_max),
                reinterpret_cast<void*>(context_root));
        }
        return;
    }

    const auto& config = GetConfig();
    const bool lock_enabled =
        config.mount.enabled &&
        (config.mount.lock_stamina ||
         (config.mount.special_enabled && config.mount.lock_special_stamina));
    if (lock_enabled && max_value > 0) {
        const bool looks_like_special_mount =
            IsInClosedRange(max_value, kSpecialMountStaminaMinMax, kSpecialMountStaminaMaxMax);
        const int64_t requested_lock_value =
            (looks_like_special_mount &&
             config.mount.special_enabled &&
             config.mount.lock_special_stamina)
                ? config.mount.special_lock_value
                : config.mount.lock_value;
        const int64_t lock_value = ClampMountLockValue(requested_lock_value, max_value);
        TryWriteStatCurrentValue(stamina_entry, lock_value);
        const auto current = g_loose_mount_stamina_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 32) {
            Log("runtime: relocked loose non-player mount stamina entry=0x%p context=0x%p current=%lld lock=%lld max=%lld note=d90-no-memory-ring",
                reinterpret_cast<void*>(stamina_entry),
                reinterpret_cast<void*>(context_root),
                static_cast<long long>(current_value),
                static_cast<long long>(lock_value),
                static_cast<long long>(max_value));
        }
        return;
    }

    const auto current = g_loose_mount_stamina_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 32) {
        Log("runtime: rejected loose special-mount stamina without full mount profile entry=0x%p context=0x%p current=%lld max=%lld",
            reinterpret_cast<void*>(stamina_entry),
            reinterpret_cast<void*>(context_root),
            static_cast<long long>(current_value),
            static_cast<long long>(max_value));
    }
}

void UpdateTrackedMountFromStatComponent(const uintptr_t entry, const uintptr_t component) {
    if (entry < kMinimumPointerAddress || component < kMinimumPointerAddress) {
        return;
    }

    const auto& config = GetConfig();
    if (!config.general.enabled ||
        !config.mount.enabled ||
        (!config.mount.lock_health &&
         !config.mount.lock_stamina &&
         !(config.mount.special_enabled && config.mount.lock_special_health) &&
         !(config.mount.special_enabled && config.mount.lock_special_stamina))) {
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

    const ActorResolveSnapshot current_mount = g_mount_resolve;
    if (IsRideableProxyMountCandidateProfile(resolved) &&
        (!current_mount.valid() ||
         !IsSameMountIdentity(current_mount, resolved) ||
         !IsSameMountEntries(current_mount, resolved))) {
        const auto current = g_mount_stat_component_diag_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 48) {
            int64_t health_max = 0;
            int64_t stamina_max = 0;
            TryReadMountProfileMaxima(resolved, &health_max, &stamina_max);
            Log("runtime: mount stat-component rejected reason=rideable-proxy-needs-mount-context entry=0x%p component=0x%p root=0x%p health=0x%p health_max=%lld stamina=0x%p stamina_max=%lld",
                reinterpret_cast<void*>(entry),
                reinterpret_cast<void*>(component),
                reinterpret_cast<void*>(resolved.root),
                reinterpret_cast<void*>(resolved.health_entry),
                static_cast<long long>(health_max),
                reinterpret_cast<void*>(resolved.stamina_entry),
                static_cast<long long>(stamina_max));
        }
        return;
    }

    if (!current_mount.valid() ||
        !IsSameMountIdentity(current_mount, resolved) ||
        !IsSameMountEntries(current_mount, resolved)) {
        const auto current = g_mount_stat_component_diag_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 48) {
            Log("runtime: mount stat-component rejected reason=new-candidate-needs-stamina-context entry=0x%p component=0x%p root=0x%p health=0x%p stamina=0x%p profile=%s",
                reinterpret_cast<void*>(entry),
                reinterpret_cast<void*>(component),
                reinterpret_cast<void*>(resolved.root),
                reinterpret_cast<void*>(resolved.health_entry),
                reinterpret_cast<void*>(resolved.stamina_entry),
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

    int64_t health_lock_value = 0;
    const char* health_name = "health";
    if (!IsSpecialMountProfile(mount_snapshot) &&
        ShouldLockSnapshotHealth(config, mount_snapshot, &health_lock_value, &health_name)) {
        TryRelockMountStatEntry(mount_snapshot.health_entry,
                                kHealthId,
                                health_name,
                                health_lock_value);
    }

    int64_t stamina_lock_value = 0;
    const char* stamina_name = "stamina";
    if (ShouldLockSnapshotStamina(config, mount_snapshot, &stamina_lock_value, &stamina_name)) {
        TryRelockMountStatEntry(mount_snapshot.stamina_entry,
                                kStaminaId,
                                stamina_name,
                                stamina_lock_value);
        TryRelockMountStatEntry(mount_snapshot.spirit_entry,
                                kSpiritId,
                                IsSpecialMountProfile(mount_snapshot) ? "special-spirit-stamina" : "spirit-stamina",
                                stamina_lock_value);
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

    if (!ShouldAcceptOpportunisticMountCandidate(resolved)) {
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
            if (g_mount_track_confidence.load(std::memory_order_acquire) < 3) {
                g_mount_track_confidence.store(3, std::memory_order_release);
            }

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


bool TryRebindTrackedPlayerStatusComponentFromStats(const uintptr_t entry,
                                                    const uintptr_t component,
                                                    const char* const reason) {
    if (entry < kMinimumPointerAddress || component < kMinimumPointerAddress) {
        return false;
    }

    uintptr_t component_marker = 0;
    if (!TryReadPointer(component, &component_marker) || component_marker < kMinimumPointerAddress) {
        return false;
    }

    ActorResolveSnapshot resolved{};
    if (!TryResolveActorResolveFromMarker(component_marker, &resolved) || !resolved.valid()) {
        return false;
    }

    if (entry != resolved.health_entry &&
        entry != resolved.stamina_entry &&
        entry != resolved.spirit_entry) {
        return false;
    }

    if (!LooksLikePlayerCombatResourcePair(resolved)) {
        return false;
    }

    if (OverlapsTrustedMountResolve(resolved)) {
        std::lock_guard lock(g_state_mutex);
        if (OverlapsTrustedMountResolve(resolved)) {
            const ActorResolveSnapshot stale_mount = g_mount_resolve;
            ResetTrackedMountLocked();
            const auto current = g_actor_resolve_logs.fetch_add(1, std::memory_order_acq_rel);
            if (current < 96) {
                Log("runtime: cleared stale trusted mount overlap during stats rebind reason=%s entry=0x%p stale_mount_health=0x%p stale_mount_stamina=0x%p new_player_health=0x%p new_player_stamina=0x%p",
                    reason != nullptr ? reason : "unknown",
                    reinterpret_cast<void*>(entry),
                    reinterpret_cast<void*>(stale_mount.health_entry),
                    reinterpret_cast<void*>(stale_mount.stamina_entry),
                    reinterpret_cast<void*>(resolved.health_entry),
                    reinterpret_cast<void*>(resolved.stamina_entry));
            }
        }
    }

    if (OverlapsTrustedMountResolve(resolved)) {
        const auto current = g_actor_resolve_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 96) {
            Log("runtime: rejected player stats rebind reason=%s entry=0x%p marker=0x%p health=0x%p stamina=0x%p spirit=0x%p note=trusted-mount-overlap",
                reason != nullptr ? reason : "unknown",
                reinterpret_cast<void*>(entry),
                reinterpret_cast<void*>(resolved.marker),
                reinterpret_cast<void*>(resolved.health_entry),
                reinterpret_cast<void*>(resolved.stamina_entry),
                reinterpret_cast<void*>(resolved.spirit_entry));
        }
        return false;
    }

    std::lock_guard lock(g_state_mutex);
    const ActorResolveSnapshot existing = g_player_resolve;
    if (existing.marker == resolved.marker &&
        existing.health_entry == resolved.health_entry &&
        existing.stamina_entry == resolved.stamina_entry &&
        existing.spirit_entry == resolved.spirit_entry) {
        return true;
    }

    if (existing.health_entry >= kMinimumPointerAddress &&
        existing.health_entry != resolved.health_entry) {
        const auto current = g_actor_resolve_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 96) {
            Log("runtime: rebound stale player status from stats reason=%s entry=0x%p old_marker=0x%p old_health=0x%p old_stamina=0x%p old_spirit=0x%p new_marker=0x%p new_health=0x%p new_stamina=0x%p new_spirit=0x%p",
                reason != nullptr ? reason : "unknown",
                reinterpret_cast<void*>(entry),
                reinterpret_cast<void*>(existing.marker),
                reinterpret_cast<void*>(existing.health_entry),
                reinterpret_cast<void*>(existing.stamina_entry),
                reinterpret_cast<void*>(existing.spirit_entry),
                reinterpret_cast<void*>(resolved.marker),
                reinterpret_cast<void*>(resolved.health_entry),
                reinterpret_cast<void*>(resolved.stamina_entry),
                reinterpret_cast<void*>(resolved.spirit_entry));
        }
    }

    ResetTrackedEntriesLocked();
    ResetTrackedDamageParticipantsLocked();
    g_player_resolve = resolved;
    return true;
}
void UpdateTrackedPlayerStatusComponent(const uintptr_t actor, const uintptr_t component) {
    if (component < kMinimumPointerAddress) {
        return;
    }

    ActorResolveSnapshot resolved{};
    if (!TryResolveActorResolveFromMarker(component, &resolved, actor)) {
        uintptr_t root = 0;
        uintptr_t root_marker = 0;
        if (!TryReadPointer(component + 0x18, &root) ||
            root < kMinimumPointerAddress ||
            !TryReadPointer(root, &root_marker) ||
            root_marker != component) {
            return;
        }

        bool should_refresh_mount = false;
        {
            std::lock_guard lock(g_state_mutex);
            const ActorResolveSnapshot existing = g_player_resolve;
            if (existing.actor == actor && existing.marker == component && existing.root == root) {
                return;
            }

            const bool identity_changed = existing.actor != actor ||
                                          existing.marker != component ||
                                          existing.root != root;
            if (identity_changed) {
                ResetTrackedEntriesLocked();
                ResetTrackedMountLocked();
                ResetTrackedDamageParticipantsLocked();
            }

            g_player_resolve.actor = actor;
            g_player_resolve.marker = component;
            g_player_resolve.root = root;
            g_player_resolve.damage_source = actor;
            g_player_resolve.damage_target = root;
            if (!identity_changed) {
                ResetTrackedDamageParticipantsLocked();
            }
            should_refresh_mount = true;

            const auto current = g_actor_resolve_logs.fetch_add(1, std::memory_order_acq_rel);
            if (current < 24) {
                Log("runtime: tracked player actor/root via pointer fallback actor=0x%p status_marker=0x%p stat_root=0x%p identity_changed=%d health=0x%p stamina=0x%p spirit=0x%p",
                    reinterpret_cast<void*>(g_player_resolve.actor),
                    reinterpret_cast<void*>(g_player_resolve.marker),
                    reinterpret_cast<void*>(g_player_resolve.root),
                    identity_changed ? 1 : 0,
                    reinterpret_cast<void*>(g_player_resolve.health_entry),
                    reinterpret_cast<void*>(g_player_resolve.stamina_entry),
                    reinterpret_cast<void*>(g_player_resolve.spirit_entry));
            }
        }

        if (should_refresh_mount) {
            RefreshTrackedMountFromPlayerActor();
        }
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
            if (!LooksLikePlayerCombatResourcePair(resolved)) {
                const bool identity_changed = existing.actor != resolved.actor ||
                                              existing.marker != resolved.marker ||
                                              existing.root != resolved.root;
                if (identity_changed &&
                    resolved.marker >= kMinimumPointerAddress &&
                    resolved.root >= kMinimumPointerAddress) {
                    ResetTrackedEntriesLocked();
                    ResetTrackedMountLocked();
                    ResetTrackedDamageParticipantsLocked();
                    g_player_resolve.actor = resolved.actor;
                    g_player_resolve.marker = resolved.marker;
                    g_player_resolve.root = resolved.root;
                    g_player_resolve.damage_source = resolved.actor;
                    g_player_resolve.damage_target = resolved.root;
                    const auto current = g_actor_resolve_logs.fetch_add(1, std::memory_order_acq_rel);
                    if (current < 48) {
                        Log("runtime: cleared stale player resources for pending identity current_health=0x%p resolved_health=0x%p status_marker=0x%p stat_root=0x%p",
                            reinterpret_cast<void*>(existing.health_entry),
                            reinterpret_cast<void*>(resolved.health_entry),
                            reinterpret_cast<void*>(resolved.marker),
                            reinterpret_cast<void*>(resolved.root));
                    }
                } else {
                    const auto current = g_actor_resolve_logs.fetch_add(1, std::memory_order_acq_rel);
                    if (current < 24) {
                        Log("runtime: ignored player status update with different non-player-shaped health current_health=0x%p resolved_health=0x%p status_marker=0x%p",
                            reinterpret_cast<void*>(existing.health_entry),
                            reinterpret_cast<void*>(resolved.health_entry),
                            reinterpret_cast<void*>(resolved.marker));
                    }
                }
                return;
            }

            const auto current = g_actor_resolve_logs.fetch_add(1, std::memory_order_acq_rel);
            if (current < 24) {
                Log("runtime: replacing stale player status update current_health=0x%p current_stamina=0x%p current_spirit=0x%p resolved_health=0x%p resolved_stamina=0x%p resolved_spirit=0x%p status_marker=0x%p",
                    reinterpret_cast<void*>(existing.health_entry),
                    reinterpret_cast<void*>(existing.stamina_entry),
                    reinterpret_cast<void*>(existing.spirit_entry),
                    reinterpret_cast<void*>(resolved.health_entry),
                    reinterpret_cast<void*>(resolved.stamina_entry),
                    reinterpret_cast<void*>(resolved.spirit_entry),
                    reinterpret_cast<void*>(resolved.marker));
            }
            ResetTrackedEntriesLocked();
            ResetTrackedMountLocked();
            ResetTrackedDamageParticipantsLocked();
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

void UpdateTrackedPlayerResourceOwner(const uintptr_t owner_root, const char* const reason) {
    if (owner_root < kMinimumPointerAddress) {
        return;
    }

    ActorResolveSnapshot owner_resolved{};
    const bool owner_resolves = TryResolveActorResolveFromRoot(owner_root, &owner_resolved) &&
                                owner_resolved.valid();

    std::lock_guard lock(g_state_mutex);
    if (g_player_resolve.health_entry < kMinimumPointerAddress ||
        g_player_resolve.stamina_entry < kMinimumPointerAddress ||
        g_player_resolve.spirit_entry < kMinimumPointerAddress) {
        return;
    }

    const uintptr_t old_root = g_player_resolve.root;
    const uintptr_t old_damage_target = g_player_resolve.damage_target;
    const bool same_known_owner = old_root == owner_root || old_damage_target == owner_root;
    const bool same_resource_profile = owner_resolves &&
        owner_resolved.health_entry == g_player_resolve.health_entry &&
        owner_resolved.stamina_entry == g_player_resolve.stamina_entry &&
        owner_resolved.spirit_entry == g_player_resolve.spirit_entry;

    const bool trusted_resource_write_owner =
        reason != nullptr &&
        (std::strstr(reason, "stat-write active stamina") != nullptr ||
         std::strstr(reason, "stat-write active spirit") != nullptr);

    if (!same_known_owner && !same_resource_profile && !trusted_resource_write_owner) {
        const auto current = g_actor_resolve_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 96) {
            Log("runtime: rejected player damage owner update reason=%s owner=0x%p old_root=0x%p old_damage_target=0x%p owner_resolves=%d owner_health=0x%p owner_stamina=0x%p owner_spirit=0x%p tracked_health=0x%p tracked_stamina=0x%p tracked_spirit=0x%p note=not-current-profile",
                reason != nullptr ? reason : "unknown",
                reinterpret_cast<void*>(owner_root),
                reinterpret_cast<void*>(old_root),
                reinterpret_cast<void*>(old_damage_target),
                owner_resolves ? 1 : 0,
                reinterpret_cast<void*>(owner_resolved.health_entry),
                reinterpret_cast<void*>(owner_resolved.stamina_entry),
                reinterpret_cast<void*>(owner_resolved.spirit_entry),
                reinterpret_cast<void*>(g_player_resolve.health_entry),
                reinterpret_cast<void*>(g_player_resolve.stamina_entry),
                reinterpret_cast<void*>(g_player_resolve.spirit_entry));
        }
        return;
    }

    const bool replace_with_resolved_owner = owner_resolves && same_resource_profile;
    const uintptr_t new_root = replace_with_resolved_owner && owner_resolved.root >= kMinimumPointerAddress
        ? owner_resolved.root
        : owner_root;
    if (old_root == new_root && old_damage_target == new_root && !replace_with_resolved_owner) {
        return;
    }

    if (replace_with_resolved_owner) {
        g_player_resolve = owner_resolved;
    } else {
        g_player_resolve.root = new_root;
        g_player_resolve.damage_target = new_root;
    }

    if (g_mount_resolve.root == new_root ||
        g_mount_resolve.health_entry == g_player_resolve.health_entry ||
        g_mount_resolve.stamina_entry == g_player_resolve.stamina_entry ||
        g_mount_resolve.spirit_entry == g_player_resolve.spirit_entry) {
        ResetTrackedMountLocked();
    }

    ResetTrackedDamageParticipantsLocked();
    const auto current = g_actor_resolve_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 96) {
        Log("runtime: updated player damage owner from resource write reason=%s old_root=0x%p old_damage_target=0x%p new_root=0x%p health=0x%p stamina=0x%p spirit=0x%p same_profile=%d",
            reason != nullptr ? reason : "unknown",
            reinterpret_cast<void*>(old_root),
            reinterpret_cast<void*>(old_damage_target),
            reinterpret_cast<void*>(new_root),
            reinterpret_cast<void*>(g_player_resolve.health_entry),
            reinterpret_cast<void*>(g_player_resolve.stamina_entry),
            reinterpret_cast<void*>(g_player_resolve.spirit_entry),
            same_resource_profile ? 1 : 0);
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







