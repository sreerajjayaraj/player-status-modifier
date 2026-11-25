#include "runtime/runtime_state.h"
#include "runtime/resource_routing_policy.h"

std::mutex g_state_mutex;
ActorResolveSnapshot g_player_resolve{};
ActorResolveSnapshot g_mount_resolve{};
std::atomic<ULONGLONG> g_mount_last_seen_tick{0};
std::atomic<std::uint32_t> g_mount_track_confidence{0};
std::atomic<bool> g_mount_resolver_running{false};
HANDLE g_mount_resolver_thread = nullptr;
std::array<std::atomic<uintptr_t>, kTrackedDamageParticipantCount> g_tracked_damage_participants{};
std::atomic<uint32_t> g_tracked_damage_participant_cursor{0};
std::atomic<bool> g_runtime_enabled{true};
std::atomic<std::uint32_t> g_process_skip_logs{0};
std::atomic<std::uint32_t> g_process_apply_logs{0};
std::atomic<std::uint32_t> g_discovery_logs{0};
std::atomic<std::uint32_t> g_durability_logs{0};
std::atomic<std::uint32_t> g_damage_logs{0};
std::atomic<std::uint32_t> g_affinity_logs{0};
std::atomic<std::uint32_t> g_mount_logs{0};
std::atomic<std::uint32_t> g_actor_resolve_logs{0};
std::atomic<std::uint32_t> g_mount_candidate_logs{0};

namespace {

std::atomic<int32_t> g_resolved_stamina_stat_id{kStaminaId};
std::atomic<int32_t> g_resolved_spirit_stat_id{kSpiritId};

}  // namespace

int32_t GetResolvedStaminaStatId() {
    return g_resolved_stamina_stat_id.load(std::memory_order_acquire);
}

int32_t GetResolvedSpiritStatId() {
    return g_resolved_spirit_stat_id.load(std::memory_order_acquire);
}

int32_t GetResolvedAlternateSpiritStatId() {
    return GetResolvedSpiritStatId() + 1;
}

bool PublishResolvedPlayerResourceStatIds(const int32_t stamina_type,
                                          const int32_t spirit_type) {
    if (stamina_type <= 0 || spirit_type <= 0 || stamina_type == spirit_type ||
        stamina_type > 0xFFFF || spirit_type > 0xFFFE) {
        return false;
    }

    const int32_t old_stamina =
        g_resolved_stamina_stat_id.exchange(stamina_type, std::memory_order_acq_rel);
    const int32_t old_spirit =
        g_resolved_spirit_stat_id.exchange(spirit_type, std::memory_order_acq_rel);
    return old_stamina != stamina_type || old_spirit != spirit_type;
}

bool IsStaminaStatId(const int32_t stat_type) {
    return psm::routing::MatchesResolvedStaminaType(
        stat_type,
        GetResolvedStaminaStatId());
}

bool IsSpiritStatId(const int32_t stat_type) {
    return psm::routing::MatchesResolvedSpiritType(
        stat_type,
        GetResolvedSpiritStatId(),
        kLegacySpiritId,
        kLegacySpiritId2);
}

bool IsCurrentSpiritStatId(const int32_t stat_type) {
    return stat_type == GetResolvedSpiritStatId() ||
           stat_type == GetResolvedAlternateSpiritStatId();
}

bool IsPrimarySpiritStatId(const int32_t stat_type) {
    return stat_type == GetResolvedSpiritStatId();
}

namespace {

bool IsVisiblePlayerSpiritEntry(const uintptr_t health_entry, const uintptr_t entry) {
    if (health_entry < kMinimumPointerAddress ||
        entry != health_entry + kVisibleSpiritEntryOffsetFromHealth) {
        return false;
    }

    __try {
        const int32_t actual_type = *reinterpret_cast<const int32_t*>(entry);
        if (!IsSpiritStatId(actual_type)) {
            return false;
        }

        const int64_t current_value = *reinterpret_cast<const int64_t*>(entry + 0x08);
        const int64_t max_value = *reinterpret_cast<const int64_t*>(entry + 0x18);
        return max_value >= 100000 &&
               max_value <= 220000 &&
               current_value >= 0 &&
               current_value <= max_value;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

TrackedStatEntryKind ClassifyTrackedStatEntry(const uintptr_t entry) {
    if (entry < kMinimumPointerAddress) {
        return TrackedStatEntryKind::None;
    }

    const ActorResolveSnapshot player_snapshot = g_player_resolve;
    if (entry == player_snapshot.health_entry) {
        return TrackedStatEntryKind::PlayerHealth;
    }

    if (entry == player_snapshot.stamina_entry) {
        return TrackedStatEntryKind::PlayerStamina;
    }

    if (entry == player_snapshot.spirit_entry) {
        return TrackedStatEntryKind::PlayerSpirit;
    }

    if (IsVisiblePlayerSpiritEntry(player_snapshot.health_entry, entry)) {
        return TrackedStatEntryKind::PlayerSpirit;
    }

    const ActorResolveSnapshot mount_snapshot = g_mount_resolve;
    const std::uint32_t mount_confidence =
        g_mount_track_confidence.load(std::memory_order_acquire);
    if (entry == mount_snapshot.stamina_entry &&
        psm::routing::ShouldTrustPartialMountStamina(
            mount_confidence,
            mount_snapshot.stamina_entry >= kMinimumPointerAddress)) {
        return TrackedStatEntryKind::MountStamina;
    }
    const bool mount_trusted =
        mount_snapshot.valid() &&
        mount_confidence >= 3;
    if (mount_trusted) {
        if (entry == mount_snapshot.health_entry) {
            return TrackedStatEntryKind::MountHealth;
        }

        if (entry == mount_snapshot.stamina_entry) {
            return TrackedStatEntryKind::MountStamina;
        }

        if (entry == mount_snapshot.spirit_entry) {
            return TrackedStatEntryKind::MountSpirit;
        }
    }

    return TrackedStatEntryKind::None;
}

int32_t ResolveTrackedStatType(const TrackedStatEntryKind kind) {
    switch (kind) {
    case TrackedStatEntryKind::PlayerHealth:
    case TrackedStatEntryKind::MountHealth:
        return kHealthId;
    case TrackedStatEntryKind::PlayerStamina:
    case TrackedStatEntryKind::MountStamina:
        return GetResolvedStaminaStatId();
    case TrackedStatEntryKind::PlayerSpirit:
    case TrackedStatEntryKind::MountSpirit:
        return GetResolvedSpiritStatId();
    default:
        return -1;
    }
}

bool IsPlayerTrackedStatEntry(const TrackedStatEntryKind kind) {
    return kind == TrackedStatEntryKind::PlayerHealth ||
           kind == TrackedStatEntryKind::PlayerStamina ||
           kind == TrackedStatEntryKind::PlayerSpirit;
}

bool IsMountTrackedStatEntry(const TrackedStatEntryKind kind) {
    return kind == TrackedStatEntryKind::MountHealth ||
           kind == TrackedStatEntryKind::MountStamina ||
           kind == TrackedStatEntryKind::MountSpirit;
}

bool IsPlayerRuntimeReady() {
    return g_player_resolve.valid();
}

bool HasTrackedPlayerDamageOwner() {
    const ActorResolveSnapshot player_snapshot = g_player_resolve;
    return player_snapshot.damage_target >= kMinimumPointerAddress ||
           player_snapshot.root >= kMinimumPointerAddress;
}

void ResetTrackedEntriesLocked() {
    AssignTrackedPlayerResolveLocked({});
    ResetResourceHudLocked();
    ResetNativeMountedResourceLocked();
}

void ResetTrackedMountLocked() {
    g_mount_resolve = {};
    g_mount_last_seen_tick.store(0, std::memory_order_release);
    g_mount_track_confidence.store(0, std::memory_order_release);
}

void ResetTrackedDamageParticipantsLocked() {
    for (auto& participant : g_tracked_damage_participants) {
        participant.store(0, std::memory_order_release);
    }

    g_tracked_damage_participant_cursor.store(0, std::memory_order_release);
}
