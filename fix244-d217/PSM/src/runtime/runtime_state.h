#pragma once

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

constexpr int32_t kHealthId = 0;

// These are startup fallbacks only. Validated adjacent player-resource pairs
// publish the current game's live selectors before resource routing begins.
constexpr int32_t kLegacyStaminaId = 17;
constexpr int32_t kStaminaId = 19;
constexpr int32_t kSpiritId = 20;
constexpr int32_t kAlternateSpiritId = 21;
constexpr int32_t kLegacySpiritId = 17;
constexpr int32_t kLegacySpiritId2 = 18;
inline constexpr uintptr_t kMinimumPointerAddress = 0x10000000;
constexpr uintptr_t kBattleDamageReturnAddressRva = 0x1A61160;
constexpr uintptr_t kLegacyStaminaEntryOffsetFromHealth = 0x480;
constexpr uintptr_t kStaminaEntryOffsetFromHealth = 0x5A0;
constexpr uintptr_t kSpiritEntryOffsetFromHealth = 0x630;
constexpr uintptr_t kLegacySpiritEntryOffsetFromHealth = 0x1B0;
constexpr uintptr_t kVisibleSpiritEntryOffsetFromHealth = 0x240;

int32_t GetResolvedStaminaStatId();
int32_t GetResolvedSpiritStatId();
int32_t GetResolvedAlternateSpiritStatId();
bool PublishResolvedPlayerResourceStatIds(int32_t stamina_type, int32_t spirit_type);
bool IsStaminaStatId(int32_t stat_type);
bool IsSpiritStatId(int32_t stat_type);
bool IsCurrentSpiritStatId(int32_t stat_type);
bool IsPrimarySpiritStatId(int32_t stat_type);

inline bool HasExpectedHealthStaminaLayout(const uintptr_t health_entry, const uintptr_t stamina_entry) {
    if (health_entry < kMinimumPointerAddress || stamina_entry < kMinimumPointerAddress) {
        return false;
    }

    return stamina_entry == health_entry + kStaminaEntryOffsetFromHealth ||
           stamina_entry == health_entry + kLegacyStaminaEntryOffsetFromHealth;
}

constexpr size_t kTrackedDamageParticipantCount = 16;
constexpr int kDamageRelationDepth = 6;
constexpr DWORD kMountResolvePollMs = 1000;

enum class TrackedStatEntryKind {
    None = 0,
    PlayerHealth,
    PlayerStamina,
    PlayerSpirit,
    MountHealth,
    MountStamina,
    MountSpirit,
};

struct ActorResolveSnapshot {
    uintptr_t actor = 0;
    uintptr_t marker = 0;
    uintptr_t root = 0;
    uintptr_t resource_owner = 0;
    uintptr_t health_entry = 0;
    uintptr_t stamina_entry = 0;
    uintptr_t spirit_entry = 0;
    int32_t stamina_type = -1;
    int32_t spirit_type = -1;
    uintptr_t stamina_offset_from_health = 0;
    uintptr_t spirit_offset_from_health = 0;
    uintptr_t damage_source = 0;
    uintptr_t damage_target = 0;

    bool valid() const {
        return marker >= kMinimumPointerAddress &&
               root >= kMinimumPointerAddress &&
               health_entry >= kMinimumPointerAddress &&
               stamina_entry >= kMinimumPointerAddress;
    }
};

extern std::mutex g_state_mutex;
extern ActorResolveSnapshot g_player_resolve;
extern ActorResolveSnapshot g_mount_resolve;
extern std::atomic<ULONGLONG> g_mount_last_seen_tick;
extern std::atomic<std::uint32_t> g_mount_track_confidence;
extern std::atomic<bool> g_mount_resolver_running;
extern HANDLE g_mount_resolver_thread;
extern std::array<std::atomic<uintptr_t>, kTrackedDamageParticipantCount> g_tracked_damage_participants;
extern std::atomic<uint32_t> g_tracked_damage_participant_cursor;
extern std::atomic<bool> g_runtime_enabled;
extern std::atomic<std::uint32_t> g_process_skip_logs;
extern std::atomic<std::uint32_t> g_process_apply_logs;
extern std::atomic<std::uint32_t> g_discovery_logs;
extern std::atomic<std::uint32_t> g_durability_logs;
extern std::atomic<std::uint32_t> g_damage_logs;
extern std::atomic<std::uint32_t> g_affinity_logs;
extern std::atomic<std::uint32_t> g_mount_logs;
extern std::atomic<std::uint32_t> g_actor_resolve_logs;
extern std::atomic<std::uint32_t> g_mount_candidate_logs;

TrackedStatEntryKind ClassifyTrackedStatEntry(uintptr_t entry);
int32_t ResolveTrackedStatType(TrackedStatEntryKind kind);
bool IsPlayerTrackedStatEntry(TrackedStatEntryKind kind);
bool IsMountTrackedStatEntry(TrackedStatEntryKind kind);
bool IsPlayerRuntimeReady();
bool HasTrackedPlayerDamageOwner();

void ResetTrackedEntriesLocked();
// Presentation state shares this mutex; notify after any player binding write.
void ResetResourceHudLocked();
void ResetNativeMountedResourceLocked();
void NotifyResourceHudBindingLocked();
void ResetTrackedMountLocked();
void ResetTrackedDamageParticipantsLocked();

#include "runtime/player_identity_publication.h"
