#include "runtime/mount_resolver.h"

#include "config.h"
#include "logger.h"
#include "mod_logic.h"
#include "ptrchain.h"
#include "ptrchain_resources.h"
#include "runtime/actor_resolve.h"

#include <Windows.h>

#include <atomic>
#include <cstdint>

namespace {

constexpr int64_t kDragonHealthMaxThreshold = 2500000;
constexpr int64_t kDragonStaminaMaxThreshold = 300000;
constexpr int64_t kDragonHealthMaxUpper = 100000000;
constexpr int64_t kDragonStaminaMaxUpper = 10000000;
constexpr int64_t kGroundMountHealthMaxThreshold = 10000;
constexpr int64_t kGroundMountStaminaMaxThreshold = 500;
constexpr int64_t kGroundMountHealthMaxUpper = 750000;
constexpr int64_t kGroundMountStaminaMaxUpper = 500000;
std::atomic<std::uint32_t> g_mount_profile_logs{0};

bool TryReadStatMaxValue(const uintptr_t entry, int64_t* const max_value) {
    if (max_value == nullptr || entry < kMinimumPointerAddress) {
        return false;
    }

    __try {
        *max_value = *reinterpret_cast<const int64_t*>(entry + 0x18);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
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

bool IsSupportedMountProfile(const ActorResolveSnapshot& snapshot) {
    if (!snapshot.valid()) {
        return false;
    }

    if (!HasExpectedHealthStaminaLayout(snapshot.health_entry, snapshot.stamina_entry)) {
        return false;
    }

    int64_t health_max = 0;
    int64_t stamina_max = 0;
    if (!TryReadStatMaxValue(snapshot.health_entry, &health_max) ||
        !TryReadStatMaxValue(snapshot.stamina_entry, &stamina_max)) {
        return false;
    }

    return IsDragonMountMaxima(health_max, stamina_max) ||
           IsGroundMountMaxima(health_max, stamina_max);
}

const char* MountProfileName(const ActorResolveSnapshot& snapshot) {
    int64_t health_max = 0;
    int64_t stamina_max = 0;
    if (!TryReadStatMaxValue(snapshot.health_entry, &health_max) ||
        !TryReadStatMaxValue(snapshot.stamina_entry, &stamina_max)) {
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

    return "rejected";
}

void LogRejectedMountProfile(const char* const reason, const ActorResolveSnapshot& snapshot) {
    const auto current = g_mount_profile_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current >= 24) {
        return;
    }

    int64_t health_max = 0;
    int64_t stamina_max = 0;
    const bool has_health_max = TryReadStatMaxValue(snapshot.health_entry, &health_max);
    const bool has_stamina_max = TryReadStatMaxValue(snapshot.stamina_entry, &stamina_max);
    Log("runtime: rejected mount profile reason=%s actor=0x%p root=0x%p marker=0x%p health=0x%p health_max=%lld stamina=0x%p stamina_max=%lld",
        reason,
        reinterpret_cast<void*>(snapshot.actor),
        reinterpret_cast<void*>(snapshot.root),
        reinterpret_cast<void*>(snapshot.marker),
        reinterpret_cast<void*>(snapshot.health_entry),
        static_cast<long long>(has_health_max ? health_max : -1),
        reinterpret_cast<void*>(snapshot.stamina_entry),
        static_cast<long long>(has_stamina_max ? stamina_max : -1));
}

bool TryResolveCurrentMountCandidateMarker(const ActorResolveSnapshot& player_snapshot, uintptr_t* const marker) {
    if (marker == nullptr || !player_snapshot.valid()) {
        return false;
    }

    uintptr_t resolved_marker = 0;
    if (!TryResolveMountedDragonMarker(&resolved_marker) ||
        resolved_marker < kMinimumPointerAddress ||
        resolved_marker == player_snapshot.marker) {
        return false;
    }

    const auto current = g_mount_candidate_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 24) {
        Log("runtime: mount chain matched name=%s marker=0x%p",
            kDragonMarkerChain.name,
            reinterpret_cast<void*>(resolved_marker));
    }

    *marker = resolved_marker;
    return true;
}

bool TryResolveCurrentMountFromPlayer(const ActorResolveSnapshot& player_snapshot,
                                      ActorResolveSnapshot* const mount_snapshot) {
    if (mount_snapshot == nullptr) {
        return false;
    }

    uintptr_t marker = 0;
    if (!TryResolveCurrentMountCandidateMarker(player_snapshot, &marker)) {
        return false;
    }

    if (!TryResolveActorResolveFromMarker(marker, mount_snapshot)) {
        return false;
    }

    if (mount_snapshot->marker == player_snapshot.marker || mount_snapshot->root == player_snapshot.root) {
        return false;
    }

    if (!IsSupportedMountProfile(*mount_snapshot)) {
        LogRejectedMountProfile("ptrchain-candidate", *mount_snapshot);
        return false;
    }

    return true;
}

void MountResolverLoop() {
    while (g_mount_resolver_running.load(std::memory_order_acquire)) {
        RefreshTrackedMountFromPlayerActor();
        RelockTrackedMountStats();
        Sleep(kMountResolvePollMs);
    }
}

DWORD WINAPI MountResolverThreadProc(LPVOID) {
    MountResolverLoop();
    return 0;
}

}  // namespace

bool TryResolveMountContext(const uintptr_t context_root_a,
                            const uintptr_t context_root_b,
                            ActorResolveSnapshot* const mount_snapshot) {
    if (mount_snapshot == nullptr) {
        return false;
    }

    const ActorResolveSnapshot player_snapshot = g_player_resolve;
    if (!player_snapshot.valid()) {
        return false;
    }

    const ActorResolveSnapshot current_mount = g_mount_resolve;
    if (current_mount.valid() &&
        current_mount.marker != player_snapshot.marker &&
        current_mount.root != player_snapshot.root &&
        IsSupportedMountProfile(current_mount)) {
        *mount_snapshot = current_mount;
        return true;
    }

    if (TryResolveActorResolveFromContextRoot(context_root_a, mount_snapshot, player_snapshot) &&
        IsSupportedMountProfile(*mount_snapshot)) {
        return true;
    }

    if (TryResolveActorResolveFromContextRoot(context_root_b, mount_snapshot, player_snapshot) &&
        IsSupportedMountProfile(*mount_snapshot)) {
        return true;
    }

    return false;
}

void RefreshTrackedMountFromPlayerActor() {
    const ActorResolveSnapshot player_snapshot = g_player_resolve;
    if (!player_snapshot.valid()) {
        return;
    }

    ActorResolveSnapshot mount_snapshot{};
    if (!TryResolveCurrentMountFromPlayer(player_snapshot, &mount_snapshot)) {
        const ActorResolveSnapshot current_mount = g_mount_resolve;
        if (!current_mount.valid()) {
            return;
        }

        const ULONGLONG last_seen = g_mount_last_seen_tick.load(std::memory_order_acquire);
        const DWORD stale_ms = GetConfig().general.stale_component_ms;
        if (last_seen != 0 && GetTickCount64() - last_seen < stale_ms) {
            return;
        }

        RelockTrackedMountStats();
        const ULONGLONG refreshed_seen = g_mount_last_seen_tick.load(std::memory_order_acquire);
        if (refreshed_seen != 0 && GetTickCount64() - refreshed_seen < stale_ms) {
            return;
        }

        std::lock_guard lock(g_state_mutex);
        if (g_mount_resolve.valid()) {
            ResetTrackedMountLocked();
        }
        return;
    }

    UpdateTrackedMountStatusComponent(mount_snapshot.actor, mount_snapshot.marker);
}

bool StartMountResolverLoop() {
    if (g_mount_resolver_running.exchange(true, std::memory_order_acq_rel)) {
        return true;
    }

    g_mount_resolver_thread = CreateThread(nullptr, 0, MountResolverThreadProc, nullptr, 0, nullptr);
    if (g_mount_resolver_thread == nullptr) {
        g_mount_resolver_running.store(false, std::memory_order_release);
        return false;
    }

    return true;
}

void StopMountResolverLoop() {
    if (!g_mount_resolver_running.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    if (g_mount_resolver_thread != nullptr) {
        WaitForSingleObject(g_mount_resolver_thread, INFINITE);
        CloseHandle(g_mount_resolver_thread);
        g_mount_resolver_thread = nullptr;
    }
}
