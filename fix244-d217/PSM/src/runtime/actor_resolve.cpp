#include "runtime/actor_resolve.h"

#include "logger.h"
#include "runtime/resource_routing_policy.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <mutex>

namespace {

constexpr int64_t kMountHealthResolveMinMax = 2500000;
constexpr int64_t kMountStaminaResolveMinMax = 300000;
constexpr int64_t kMountHealthResolveMaxMax = 100000000;
constexpr int64_t kMountStaminaResolveMaxMax = 10000000;
constexpr int64_t kGroundMountHealthResolveMinMax = 10000;
constexpr int64_t kGroundMountStaminaResolveMinMax = 500;
constexpr int64_t kGroundMountHealthResolveMaxMax = 750000;
constexpr int64_t kGroundMountStaminaResolveMaxMax = 500000;
constexpr int64_t kPlayerHealthWriteFallbackMinMax = 500000;
constexpr uintptr_t kResolveRootEntryScanStart = 0x08;
constexpr uintptr_t kResolveRootEntryScanEnd = 0x200;
constexpr uintptr_t kNearbyStatEntrySearchBack = 0x100;
constexpr uintptr_t kNearbyStatEntrySearchForward = 0x900;
constexpr int64_t kPlayerCombatHealthResolveMinMax = 100000;
constexpr int64_t kPlayerCombatHealthResolveMaxMax = 2500000;
constexpr int64_t kPlayerCombatStaminaResolveMinMax =
    psm::routing::kSupportedPlayerStaminaMinimum;
constexpr int64_t kPlayerCombatStaminaResolveMaxMax =
    psm::routing::kSupportedPlayerStaminaMaximum;
constexpr int64_t kPlayerCombatSpiritResolveMinMax =
    psm::routing::kSupportedPlayerSpiritMinimum;
constexpr int64_t kPlayerCombatSpiritResolveMaxMax =
    psm::routing::kSupportedPlayerSpiritMaximum;
constexpr ULONGLONG kActivePlayerConfirmationWindowMs = 60000;
constexpr ULONGLONG kObservedSpiritOwnerGraceMs = 3000;
constexpr ULONGLONG kPlayerStatusBootstrapWindowMs = 5000;
constexpr std::size_t kObservedPlayerOwnerCandidateCount = 8;
constexpr std::size_t kObservedPlayerStatusCount = 8;
constexpr uintptr_t kResourceEntryStride = 0x90;
constexpr uintptr_t kPlayerResourcePairScanStart = 0x100;
constexpr uintptr_t kPlayerResourcePairScanEnd = 0x900;

struct ObservedPlayerOwnerCandidate {
    uintptr_t owner = 0;
    uintptr_t stamina_entry = 0;
    int64_t stamina_max = 0;
    ULONGLONG last_seen_tick = 0;
    int64_t last_positive_resource_delta = 0;
    ULONGLONG last_positive_resource_tick = 0;
    std::array<int32_t, kObservedPlayerStatusCount> status_ids{};
    std::uint32_t distinct_status_count = 0;
    bool saw_positive_status_delta = false;
    bool saw_negative_status_delta = false;
    bool saw_valid_status_source = false;
    bool validated_playable_owner = false;
    ULONGLONG status_first_seen_tick = 0;
    ULONGLONG status_last_seen_tick = 0;
};

struct DiscoveredPlayerResourcePair {
    uintptr_t stamina_entry = 0;
    uintptr_t spirit_entry = 0;
    int32_t stamina_type = -1;
    int32_t spirit_type = -1;
    int64_t stamina_max = 0;
    int64_t spirit_max = 0;
};

bool TryAddAddressOffset(const uintptr_t base,
                         const uintptr_t offset,
                         uintptr_t* const result) {
    if (result == nullptr ||
        base < kMinimumPointerAddress ||
        base > (std::numeric_limits<uintptr_t>::max)() - offset) {
        return false;
    }

    *result = base + offset;
    return true;
}

std::mutex g_observed_player_owner_mutex;
std::array<ObservedPlayerOwnerCandidate, kObservedPlayerOwnerCandidateCount>
    g_observed_player_owner_candidates{};
std::atomic<uintptr_t> g_confirmed_player_owner{0};
std::atomic<ULONGLONG> g_confirmed_player_owner_tick{0};
std::atomic<ULONGLONG> g_player_owner_activation_tick{0};
std::atomic<uintptr_t> g_correlated_player_spirit_owner{0};

bool IsValidStatEntry(const uintptr_t entry, const int32_t expected_type) {
    if (entry < kMinimumPointerAddress) {
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

        const int64_t current_value = *reinterpret_cast<const int64_t*>(entry + 0x08);
        const int64_t max_value = *reinterpret_cast<const int64_t*>(entry + 0x18);
        return max_value > 0 && current_value >= 0 && current_value <= max_value;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The stat lookup uses the owner status array at +0x58. The first
// entry is Health for the observed player banks. Confirm the full chain,
// including the actor identity and reciprocal owner links, before using it.
struct NativePlayerHealthBank {
    uintptr_t actor = 0;
    uintptr_t table = 0;
    uintptr_t marker = 0;
    uintptr_t owner = 0;
    uintptr_t health = 0;
    uint32_t uid = 0;
    uint8_t category = 0;
    bool operator==(const NativePlayerHealthBank&) const = default;
};

template<class T>
bool ReadCombatField(uintptr_t base, uintptr_t offset, T* value) {
    uintptr_t address = 0;
    if (!TryAddAddressOffset(base, offset, &address) || address > UINTPTR_MAX - sizeof(T)) {
        return false;
    }
    __try {
        *value = *reinterpret_cast<const T*>(address);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ReadNativePlayerHealthBank(uintptr_t actor, NativePlayerHealthBank* result,
                                bool require_health = true) {
    NativePlayerHealthBank bank{};
    bank.actor = actor;
    uintptr_t metadata = 0, reverse_actor = 0, reverse_marker = 0;
    if (!ReadCombatField(actor, 0x60, &bank.uid) || !bank.uid ||
        !ReadCombatField(actor, 0x88, &metadata) ||
        !ReadCombatField(metadata, 1, &bank.category) ||
        // The authoritative capture includes type 4 for Damiane and Oongka.
        // Type identifies the layout. Capture ownership and the full UID
        // authenticate the active actor and its server mirror.
        (bank.category != 1 && bank.category != 4) ||
        !ReadCombatField(actor, 0x68, &bank.table) ||
        !ReadCombatField(bank.table, 0x20, &bank.marker) ||
        !ReadCombatField(bank.marker, 8, &reverse_actor) || reverse_actor != actor ||
        !ReadCombatField(bank.marker, 0x18, &bank.owner) ||
        !ReadCombatField(bank.owner, 0, &reverse_marker) || reverse_marker != bank.marker) {
        return false;
    }
    if (!ReadCombatField(bank.owner, 0x58, &bank.health) || !IsValidStatEntry(bank.health, kHealthId)) {
        if (require_health) return false;
        bank.health = 0;
    }
    *result = bank;
    return true;
}

bool ReadPrimaryHealthBank(uintptr_t actor, uintptr_t marker, uintptr_t owner,
                           NativePlayerHealthBank* bank, bool require_health = true) {
    uintptr_t native = 0;
    return ReadCombatField(marker, 8, &native) &&
           ReadNativePlayerHealthBank(native, bank, require_health) &&
           (actor == native || actor == bank->table) && marker == bank->marker && owner == bank->owner;
}

std::atomic<uintptr_t> g_combat_server_actor_vtable{0};
psm::AtomicWords<8> g_combat_mirror;
std::mutex g_combat_mirror_mutex;
enum CombatMirrorWord : unsigned { Generation, Actor, Marker, Owner, ServerActor, ServerMarker, ServerOwner, ServerHealth };

bool IsNativeCombatServerActor(uintptr_t actor) {
    const auto expected = g_combat_server_actor_vtable.load(std::memory_order_acquire);
    uintptr_t vtable = 0;
    return expected && ReadCombatField(actor, 0, &vtable) && vtable == expected;
}

bool MatchesPrimary(const psm::AtomicWords<8>::Words& mirror, const psm::authority::Token& token) {
    return mirror[Generation] == token.generation && mirror[Actor] == token.actor &&
           mirror[Marker] == token.marker && mirror[Owner] == token.root;
}

bool TryReadStatTypeValue(const uintptr_t entry, int32_t* const stat_type) {
    if (entry < kMinimumPointerAddress || stat_type == nullptr) {
        return false;
    }

    __try {
        *stat_type = *reinterpret_cast<const int32_t*>(entry);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool TryReadObservedStatEntry(const uintptr_t entry,
                              int32_t* const stat_type,
                              int64_t* const current_value,
                              int64_t* const max_value) {
    if (entry < kMinimumPointerAddress ||
        stat_type == nullptr ||
        current_value == nullptr ||
        max_value == nullptr) {
        return false;
    }

    __try {
        *stat_type = *reinterpret_cast<const int32_t*>(entry);
        *current_value = *reinterpret_cast<const int64_t*>(entry + 0x08);
        *max_value = *reinterpret_cast<const int64_t*>(entry + 0x18);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool IsValidStaminaEntry(const uintptr_t entry) {
    return IsValidStatEntry(entry, kStaminaId);
}

bool TryGetStatEntryMaxValue(const uintptr_t entry, const int32_t expected_type, int64_t* const max_value) {
    if (max_value == nullptr) {
        return false;
    }

    __try {
        if (!IsValidStatEntry(entry, expected_type)) {
            return false;
        }

        *max_value = *reinterpret_cast<const int64_t*>(entry + 0x18);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool TryGetStaminaEntryMaxValue(const uintptr_t entry, int64_t* const max_value) {
    return TryGetStatEntryMaxValue(entry, kStaminaId, max_value);
}

bool IsInClosedRange(const int64_t value, const int64_t minimum, const int64_t maximum) {
    return value >= minimum && value <= maximum;
}

bool IsSupportedMountHealthMax(const int64_t max_value) {
    return IsInClosedRange(max_value, kGroundMountHealthResolveMinMax, kGroundMountHealthResolveMaxMax) ||
           IsInClosedRange(max_value, kMountHealthResolveMinMax, kMountHealthResolveMaxMax);
}

bool IsSupportedMountStaminaMax(const int64_t max_value) {
    return IsInClosedRange(max_value, kGroundMountStaminaResolveMinMax, kGroundMountStaminaResolveMaxMax) ||
           IsInClosedRange(max_value, kMountStaminaResolveMinMax, kMountStaminaResolveMaxMax);
}

bool TryResolveAdjacentStaminaEntry(const uintptr_t health_entry,
                                    uintptr_t* const stamina_entry,
                                    int64_t* const stamina_max) {
    if (stamina_entry == nullptr || stamina_max == nullptr || health_entry < kMinimumPointerAddress) {
        return false;
    }

    constexpr uintptr_t candidate_offsets[] = {
        kStaminaEntryOffsetFromHealth,
        kLegacyStaminaEntryOffsetFromHealth,
    };

    for (const uintptr_t offset : candidate_offsets) {
        const uintptr_t candidate_stamina = health_entry + offset;
        int64_t candidate_max = 0;
        if (TryGetStaminaEntryMaxValue(candidate_stamina, &candidate_max) &&
            IsSupportedMountStaminaMax(candidate_max)) {
            *stamina_entry = candidate_stamina;
            *stamina_max = candidate_max;
            return true;
        }
    }

    return false;
}

bool TryResolveAdjacentHealthEntry(const uintptr_t stamina_entry,
                                   uintptr_t* const health_entry,
                                   int64_t* const health_max) {
    if (health_entry == nullptr || health_max == nullptr || stamina_entry < kMinimumPointerAddress) {
        return false;
    }

    constexpr uintptr_t candidate_offsets[] = {
        kStaminaEntryOffsetFromHealth,
        kLegacyStaminaEntryOffsetFromHealth,
    };

    for (const uintptr_t offset : candidate_offsets) {
        if (stamina_entry <= offset) {
            continue;
        }

        const uintptr_t candidate_health = stamina_entry - offset;
        int64_t candidate_max = 0;
        if (TryGetStatEntryMaxValue(candidate_health, kHealthId, &candidate_max) &&
            IsSupportedMountHealthMax(candidate_max)) {
            *health_entry = candidate_health;
            *health_max = candidate_max;
            return true;
        }
    }

    return false;
}

uintptr_t TryFindUniqueNearbyStatEntry(const uintptr_t anchor_entry, const int32_t expected_type) {
    if (anchor_entry < kMinimumPointerAddress) {
        return 0;
    }

    const uintptr_t search_start =
        anchor_entry > kNearbyStatEntrySearchBack ? anchor_entry - kNearbyStatEntrySearchBack : anchor_entry;
    const uintptr_t search_end = anchor_entry + kNearbyStatEntrySearchForward;

    uintptr_t found = 0;
    int found_count = 0;

    for (uintptr_t candidate = search_start; candidate <= search_end; candidate += sizeof(uintptr_t)) {
        if (candidate == anchor_entry) {
            continue;
        }

        if (!IsValidStatEntry(candidate, expected_type)) {
            continue;
        }

        found = candidate;
        ++found_count;
        if (found_count > 1) {
            return 0;
        }
    }

    return found_count == 1 ? found : 0;
}

uintptr_t TryResolvePlayerSpiritEntryFromHealth(const uintptr_t health_entry) {
    if (health_entry < kMinimumPointerAddress) {
        return 0;
    }

    const uintptr_t candidate_spirit_entry = health_entry + kSpiritEntryOffsetFromHealth;
    if (IsValidStatEntry(candidate_spirit_entry, kSpiritId)) {
        return candidate_spirit_entry;
    }

    const uintptr_t legacy_candidate_spirit_entry = health_entry + kLegacySpiritEntryOffsetFromHealth;
    if (IsValidStatEntry(legacy_candidate_spirit_entry, kSpiritId)) {
        return legacy_candidate_spirit_entry;
    }

    return TryFindUniqueNearbyStatEntry(health_entry, kSpiritId);
}

bool IsNearbyTrackedPlayerStatCandidate(const uintptr_t entry, const uintptr_t health_entry) {
    if (entry < kMinimumPointerAddress || health_entry < kMinimumPointerAddress) {
        return false;
    }

    const uintptr_t lower = health_entry > kNearbyStatEntrySearchBack ? health_entry - kNearbyStatEntrySearchBack : health_entry;
    const uintptr_t upper = health_entry + kNearbyStatEntrySearchForward;
    return entry >= lower && entry <= upper;
}

bool IsPlayerHealthFallbackMaxValue(const int64_t max_value) {
    return max_value >= kPlayerHealthWriteFallbackMinMax &&
           max_value < kMountHealthResolveMinMax;
}

bool IsPlayerCombatHealthMaxValue(const int64_t max_value) {
    return IsInClosedRange(max_value, kPlayerCombatHealthResolveMinMax, kPlayerCombatHealthResolveMaxMax);
}

bool IsPlayerCombatStaminaMaxValue(const int64_t max_value) {
    return IsInClosedRange(max_value, kPlayerCombatStaminaResolveMinMax, kPlayerCombatStaminaResolveMaxMax);
}

bool IsPlayerCombatSpiritMaxValue(const int64_t max_value) {
    return IsInClosedRange(max_value, kPlayerCombatSpiritResolveMinMax, kPlayerCombatSpiritResolveMaxMax);
}

bool IsPlayerLargeSpiritMirrorMaxValue(const int64_t max_value) {
    return IsPlayerCombatStaminaMaxValue(max_value);
}

bool TryReadObservedStatEntryIfShape(const uintptr_t entry,
                                     const int32_t expected_type,
                                     const bool allow_primary_type20_stamina_shape,
                                     int64_t* const current_value,
                                     int64_t* const max_value) {
    int32_t stat_type = -1;
    int64_t current = 0;
    int64_t maximum = 0;
    if (!TryReadObservedStatEntry(entry, &stat_type, &current, &maximum) ||
        maximum <= 0 ||
        current < 0 ||
        current > maximum) {
        return false;
    }

    const bool type_matches = expected_type == kStaminaId
        ? (IsStaminaStatId(stat_type) ||
           (allow_primary_type20_stamina_shape &&
            IsPrimarySpiritStatId(stat_type) &&
            IsPlayerCombatStaminaMaxValue(maximum)))
        : (expected_type == kSpiritId ? IsSpiritStatId(stat_type) : stat_type == expected_type);
    if (!type_matches) {
        return false;
    }

    if (current_value != nullptr) {
        *current_value = current;
    }
    if (max_value != nullptr) {
        *max_value = maximum;
    }
    return true;
}

bool TryReadDiscoveredPlayerResourcePair(const uintptr_t stamina_entry,
                                         DiscoveredPlayerResourcePair* const pair) {
    if (pair == nullptr || stamina_entry < kMinimumPointerAddress) {
        return false;
    }

    int32_t stamina_type = -1;
    int64_t stamina_current = 0;
    int64_t stamina_max = 0;
    int32_t spirit_type = -1;
    int64_t spirit_current = 0;
    int64_t spirit_max = 0;
    uintptr_t spirit_entry = 0;
    if (!TryAddAddressOffset(stamina_entry, kResourceEntryStride, &spirit_entry)) {
        return false;
    }
    if (!TryReadObservedStatEntry(stamina_entry, &stamina_type, &stamina_current, &stamina_max) ||
        !TryReadObservedStatEntry(spirit_entry, &spirit_type, &spirit_current, &spirit_max) ||
        stamina_current < 0 || stamina_current > stamina_max ||
        spirit_current < 0 || spirit_current > spirit_max ||
        !psm::routing::IsCurrentPatchPlayerResourcePair(
            stamina_type,
            stamina_max,
            spirit_type,
            spirit_max)) {
        return false;
    }

    pair->stamina_entry = stamina_entry;
    pair->spirit_entry = spirit_entry;
    pair->stamina_type = stamina_type;
    pair->spirit_type = spirit_type;
    pair->stamina_max = stamina_max;
    pair->spirit_max = spirit_max;
    return true;
}

bool TryReadDiscoveredPlayerResourcePairForEntry(
    const uintptr_t entry,
    DiscoveredPlayerResourcePair* const pair,
    psm::routing::DiscoveredPlayerResourceRole* const role) {
    if (pair == nullptr || role == nullptr || entry < kMinimumPointerAddress) {
        return false;
    }

    DiscoveredPlayerResourcePair candidate{};
    if (TryReadDiscoveredPlayerResourcePair(entry, &candidate)) {
        *pair = candidate;
        *role = psm::routing::SelectDiscoveredPlayerResourceRole(
            entry,
            candidate.stamina_entry,
            candidate.spirit_entry,
            true);
        return *role != psm::routing::DiscoveredPlayerResourceRole::None;
    }

    if (entry <= kResourceEntryStride ||
        !TryReadDiscoveredPlayerResourcePair(entry - kResourceEntryStride, &candidate)) {
        return false;
    }

    *pair = candidate;
    *role = psm::routing::SelectDiscoveredPlayerResourceRole(
        entry,
        candidate.stamina_entry,
        candidate.spirit_entry,
        true);
    return *role != psm::routing::DiscoveredPlayerResourceRole::None;
}

void PublishAcceptedPlayerResourcePair(
    const DiscoveredPlayerResourcePair& pair,
    const char* const reason) {
    const bool pair_is_valid =
        pair.stamina_entry >= kMinimumPointerAddress &&
        pair.spirit_entry >= kMinimumPointerAddress &&
        pair.stamina_type > 0 &&
        pair.spirit_type > 0;
    if (!psm::routing::ShouldPublishDiscoveredPlayerResourceSelectors(
            pair_is_valid,
            true)) {
        return;
    }

    if (PublishResolvedPlayerResourceStatIds(pair.stamina_type, pair.spirit_type)) {
        Log("runtime: published accepted player resource selectors reason=%s stamina=%d spirit=%d base=0x%p",
            reason != nullptr ? reason : "unknown",
            pair.stamina_type,
            pair.spirit_type,
            reinterpret_cast<void*>(pair.stamina_entry));
    }
}

void PublishAcceptedPlayerResourceSnapshot(
    const ActorResolveSnapshot& snapshot,
    const char* const reason) {
    DiscoveredPlayerResourcePair pair{};
    if (!TryReadDiscoveredPlayerResourcePair(snapshot.stamina_entry, &pair) ||
        pair.spirit_entry != snapshot.spirit_entry) {
        return;
    }
    PublishAcceptedPlayerResourcePair(pair, reason);
}

bool TryDiscoverPlayerResourcePairFromHealth(const uintptr_t health_entry,
                                             DiscoveredPlayerResourcePair* const pair) {
    if (pair == nullptr || health_entry < kMinimumPointerAddress) {
        return false;
    }

    // Prefer layouts already validated by earlier game builds, but validate
    // their live type/value contract instead of trusting the numeric IDs.
    constexpr uintptr_t preferred_offsets[] = {
        kStaminaEntryOffsetFromHealth,
        kLegacyStaminaEntryOffsetFromHealth,
    };
    for (const uintptr_t offset : preferred_offsets) {
        DiscoveredPlayerResourcePair candidate{};
        uintptr_t candidate_entry = 0;
        if (TryAddAddressOffset(health_entry, offset, &candidate_entry) &&
            TryReadDiscoveredPlayerResourcePair(candidate_entry, &candidate)) {
            *pair = candidate;
            return true;
        }
    }

    // Update-resilient fallback: accept only one shape-valid adjacent pair in
    // the bounded player status block. Ambiguous layouts fail closed.
    DiscoveredPlayerResourcePair found{};
    std::uint32_t match_count = 0;
    for (uintptr_t offset = kPlayerResourcePairScanStart;
         offset + kResourceEntryStride <= kPlayerResourcePairScanEnd;
         offset += sizeof(uintptr_t)) {
        DiscoveredPlayerResourcePair candidate{};
        uintptr_t candidate_entry = 0;
        if (!TryAddAddressOffset(health_entry, offset, &candidate_entry) ||
            !TryReadDiscoveredPlayerResourcePair(candidate_entry, &candidate)) {
            continue;
        }
        found = candidate;
        ++match_count;
        if (match_count > 1) {
            return false;
        }
    }

    if (match_count != 1) {
        return false;
    }
    *pair = found;
    return true;
}

bool TryResolveAdjacentPlayerCombatStaminaFromSpirit(const uintptr_t spirit_entry, uintptr_t* const stamina_entry) {
    if (stamina_entry == nullptr || spirit_entry < kMinimumPointerAddress) {
        return false;
    }

    constexpr uintptr_t delta = kResourceEntryStride;
    if (spirit_entry > delta) {
        const uintptr_t candidate = spirit_entry - delta;
        DiscoveredPlayerResourcePair pair{};
        if (TryReadDiscoveredPlayerResourcePair(candidate, &pair) &&
            pair.spirit_entry == spirit_entry) {
            *stamina_entry = candidate;
            return true;
        }
    }

    return false;
}

bool TryResolveAdjacentPlayerCombatSpiritFromStamina(const uintptr_t stamina_entry, uintptr_t* const spirit_entry) {
    if (spirit_entry == nullptr || stamina_entry < kMinimumPointerAddress) {
        return false;
    }

    DiscoveredPlayerResourcePair pair{};
    if (TryReadDiscoveredPlayerResourcePair(stamina_entry, &pair)) {
        *spirit_entry = pair.spirit_entry;
        return true;
    }

    return false;
}

bool TryPromoteObservedPlayerOwnerPartialLocked(const uintptr_t entry,
                                                const uintptr_t owner_root,
                                                const bool is_player_stamina,
                                                const bool is_player_spirit,
                                                const char* const reason) {
    const ActorResolveSnapshot existing = g_player_resolve;
    const bool has_established_player_identity =
        existing.root >= kMinimumPointerAddress ||
        existing.damage_target >= kMinimumPointerAddress;
    const bool exact_authoritative_owner_match =
        owner_root == existing.root ||
        owner_root == existing.damage_target ||
        owner_root == existing.resource_owner;
    if (!psm::routing::ShouldAcceptObservedResourceOwnerForActivePlayer(
            has_established_player_identity,
            exact_authoritative_owner_match)) {
        return false;
    }
    const bool owner_changed = !has_established_player_identity &&
        psm::routing::ObservedOwnerChanged(
            existing.root,
            existing.damage_target,
            owner_root);
    ActorResolveSnapshot next = existing;
    if (!has_established_player_identity) {
        next.root = owner_root;
        next.damage_target = owner_root;
        if (next.damage_source < kMinimumPointerAddress) {
            next.damage_source = owner_root;
        }
    }
    next.resource_owner = owner_root;

    DiscoveredPlayerResourcePair pair{};
    psm::routing::DiscoveredPlayerResourceRole role =
        psm::routing::DiscoveredPlayerResourceRole::None;
    const bool has_pair =
        TryReadDiscoveredPlayerResourcePairForEntry(entry, &pair, &role);
    if (has_pair) {
        next.stamina_entry = pair.stamina_entry;
        next.spirit_entry = pair.spirit_entry;
        next.stamina_type = pair.stamina_type;
        next.spirit_type = pair.spirit_type;
        next.stamina_offset_from_health = 0;
        next.spirit_offset_from_health = 0;
    } else if (is_player_stamina) {
        next.stamina_entry = entry;
    } else if (is_player_spirit) {
        next.spirit_entry = entry;
    } else {
        return false;
    }

    if (next.stamina_entry < kMinimumPointerAddress && next.spirit_entry < kMinimumPointerAddress) {
        return false;
    }

    AssignTrackedPlayerResolveLocked(next);
    NotifyResourceHudBindingLocked();
    if (has_pair) {
        PublishAcceptedPlayerResourcePair(pair, reason);
    }
    if (g_mount_resolve.root == owner_root ||
        g_mount_resolve.damage_target == owner_root ||
        g_mount_resolve.health_entry == entry ||
        g_mount_resolve.stamina_entry == entry ||
        g_mount_resolve.spirit_entry == entry) {
        ResetTrackedMountLocked();
    }
    if (owner_changed) {
        ResetTrackedDamageParticipantsLocked();
    }

    const auto current = g_discovery_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 64) {
        Log("runtime: promoted active player owner-only from observed %s reason=%s owner=0x%p health=0x%p stamina=0x%p spirit=0x%p old_root=0x%p old_damage_target=0x%p owner_changed=%d",
            role == psm::routing::DiscoveredPlayerResourceRole::Stamina
                ? "stamina"
                : (role == psm::routing::DiscoveredPlayerResourceRole::Spirit
                    ? "spirit"
                    : (is_player_stamina ? "stamina" : "spirit")),
            reason != nullptr ? reason : "unknown",
            reinterpret_cast<void*>(owner_root),
            reinterpret_cast<void*>(g_player_resolve.health_entry),
            reinterpret_cast<void*>(g_player_resolve.stamina_entry),
            reinterpret_cast<void*>(g_player_resolve.spirit_entry),
            reinterpret_cast<void*>(existing.root),
            reinterpret_cast<void*>(existing.damage_target),
            owner_changed ? 1 : 0);
    }
    return true;
}

bool TryResolvePlayerCombatBlockFromHealth(const uintptr_t health_entry,
                                           ActorResolveSnapshot* const resolved) {
    if (resolved == nullptr || health_entry < kMinimumPointerAddress) {
        return false;
    }

    int64_t health_max = 0;
    if (!IsValidStatEntry(health_entry, kHealthId) ||
        !TryGetStatEntryMaxValue(health_entry, kHealthId, &health_max) ||
        !IsPlayerCombatHealthMaxValue(health_max)) {
        return false;
    }

    uintptr_t stamina_entry = 0;
    int64_t stamina_max = 0;
    uintptr_t spirit_entry = 0;
    int64_t spirit_max = 0;
    int32_t stamina_type = -1;
    int32_t spirit_type = -1;

    DiscoveredPlayerResourcePair discovered_pair{};
    if (TryDiscoverPlayerResourcePairFromHealth(health_entry, &discovered_pair)) {
        stamina_entry = discovered_pair.stamina_entry;
        spirit_entry = discovered_pair.spirit_entry;
        stamina_type = discovered_pair.stamina_type;
        spirit_type = discovered_pair.spirit_type;
        stamina_max = discovered_pair.stamina_max;
        spirit_max = discovered_pair.spirit_max;
    }

    // Preserve the legacy exact-ID resolver for old layouts whose two resource
    // entries were not adjacent.
    constexpr uintptr_t stamina_offsets[] = {
        kStaminaEntryOffsetFromHealth,
        kLegacyStaminaEntryOffsetFromHealth,
    };
    for (const uintptr_t offset : stamina_offsets) {
        if (stamina_entry >= kMinimumPointerAddress) {
            break;
        }
        uintptr_t candidate = 0;
        if (!TryAddAddressOffset(health_entry, offset, &candidate)) {
            continue;
        }
        int64_t candidate_max = 0;
        if (TryGetStaminaEntryMaxValue(candidate, &candidate_max) &&
            IsPlayerCombatStaminaMaxValue(candidate_max)) {
            stamina_entry = candidate;
            stamina_max = candidate_max;
            break;
        }
    }

    if (stamina_entry < kMinimumPointerAddress) {
        const uintptr_t nearby_stamina = TryFindUniqueNearbyStatEntry(health_entry, kStaminaId);
        int64_t nearby_stamina_max = 0;
        if (TryGetStaminaEntryMaxValue(nearby_stamina, &nearby_stamina_max) &&
            IsPlayerCombatStaminaMaxValue(nearby_stamina_max) &&
            IsNearbyTrackedPlayerStatCandidate(nearby_stamina, health_entry)) {
            stamina_entry = nearby_stamina;
            stamina_max = nearby_stamina_max;
        }
    }

    if (stamina_entry < kMinimumPointerAddress) {
        return false;
    }

    constexpr uintptr_t spirit_offsets[] = {
        kSpiritEntryOffsetFromHealth,
        kLegacySpiritEntryOffsetFromHealth,
    };
    for (const uintptr_t offset : spirit_offsets) {
        if (spirit_entry >= kMinimumPointerAddress) {
            break;
        }
        uintptr_t candidate = 0;
        if (!TryAddAddressOffset(health_entry, offset, &candidate)) {
            continue;
        }
        int64_t candidate_max = 0;
        if (TryGetStatEntryMaxValue(candidate, kSpiritId, &candidate_max) &&
            IsPlayerCombatSpiritMaxValue(candidate_max)) {
            spirit_entry = candidate;
            spirit_max = candidate_max;
            break;
        }
    }

    if (spirit_entry < kMinimumPointerAddress) {
        const uintptr_t nearby_spirit = TryResolvePlayerSpiritEntryFromHealth(health_entry);
        int64_t nearby_spirit_max = 0;
        if (TryGetStatEntryMaxValue(nearby_spirit, kSpiritId, &nearby_spirit_max) &&
            IsPlayerCombatSpiritMaxValue(nearby_spirit_max) &&
            IsNearbyTrackedPlayerStatCandidate(nearby_spirit, health_entry)) {
            spirit_entry = nearby_spirit;
            spirit_max = nearby_spirit_max;
        }
    }

    if (spirit_entry < kMinimumPointerAddress) {
        return false;
    }

    if (stamina_entry < health_entry || spirit_entry < health_entry) {
        return false;
    }

    *resolved = g_player_resolve;
    resolved->health_entry = health_entry;
    resolved->stamina_entry = stamina_entry;
    resolved->spirit_entry = spirit_entry;
    if (stamina_type < 0) {
        TryReadStatTypeValue(stamina_entry, &stamina_type);
    }
    if (spirit_type < 0) {
        TryReadStatTypeValue(spirit_entry, &spirit_type);
    }
    resolved->stamina_type = stamina_type;
    resolved->spirit_type = spirit_type;
    resolved->stamina_offset_from_health = stamina_entry - health_entry;
    resolved->spirit_offset_from_health = spirit_entry - health_entry;
    (void)stamina_max;
    (void)spirit_max;
    return true;
}

bool TryResolvePlayerCombatBlockFromResourceEntry(const uintptr_t resource_entry,
                                                  const bool is_stamina_entry,
                                                  ActorResolveSnapshot* const resolved) {
    if (resolved == nullptr || resource_entry < kMinimumPointerAddress) {
        return false;
    }

    const auto matches_resource = [&](const ActorResolveSnapshot& candidate) {
        return is_stamina_entry
            ? candidate.stamina_entry == resource_entry
            : candidate.spirit_entry == resource_entry;
    };

    const uintptr_t preferred_offsets[] = {
        is_stamina_entry ? kStaminaEntryOffsetFromHealth : kSpiritEntryOffsetFromHealth,
        is_stamina_entry ? kLegacyStaminaEntryOffsetFromHealth : kLegacySpiritEntryOffsetFromHealth,
    };
    for (const uintptr_t offset : preferred_offsets) {
        if (resource_entry <= offset) {
            continue;
        }

        ActorResolveSnapshot candidate{};
        if (TryResolvePlayerCombatBlockFromHealth(resource_entry - offset, &candidate) &&
            matches_resource(candidate)) {
            *resolved = candidate;
            return true;
        }
    }

    ActorResolveSnapshot found{};
    std::uint32_t match_count = 0;
    for (uintptr_t offset = kPlayerResourcePairScanStart;
         offset <= kPlayerResourcePairScanEnd;
         offset += sizeof(uintptr_t)) {
        if (resource_entry <= offset) {
            continue;
        }

        ActorResolveSnapshot candidate{};
        if (!TryResolvePlayerCombatBlockFromHealth(resource_entry - offset, &candidate) ||
            !matches_resource(candidate)) {
            continue;
        }

        found = candidate;
        ++match_count;
        if (match_count > 1) {
            return false;
        }
    }

    if (match_count != 1) {
        return false;
    }
    *resolved = found;
    return true;
}

bool TryResolvePlayerSnapshotFromStaminaOffset(const uintptr_t stamina_entry,
                                               ActorResolveSnapshot* const resolved) {
    return TryResolvePlayerCombatBlockFromResourceEntry(stamina_entry, true, resolved);
}

void TryResolveLargeMountStatEntries(const uintptr_t root,
                                     uintptr_t* const health_entry,
                                     uintptr_t* const stamina_entry) {
    if (health_entry == nullptr || stamina_entry == nullptr || root < kMinimumPointerAddress) {
        return;
    }

    uintptr_t best_health_entry = 0;
    int64_t best_health_max = 0;
    uintptr_t best_stamina_entry = 0;
    int64_t best_stamina_max = 0;

    const auto consider_pair = [&](const uintptr_t candidate_health, const uintptr_t candidate_stamina) {
        if (!HasExpectedHealthStaminaLayout(candidate_health, candidate_stamina)) {
            return;
        }

        int64_t candidate_health_max = 0;
        int64_t candidate_stamina_max = 0;
        if (!TryGetStatEntryMaxValue(candidate_health, kHealthId, &candidate_health_max) ||
            !TryGetStaminaEntryMaxValue(candidate_stamina, &candidate_stamina_max) ||
            !IsSupportedMountHealthMax(candidate_health_max) ||
            !IsSupportedMountStaminaMax(candidate_stamina_max)) {
            return;
        }

        if (candidate_health_max > best_health_max ||
            (candidate_health_max == best_health_max && candidate_stamina_max > best_stamina_max)) {
            best_health_entry = candidate_health;
            best_health_max = candidate_health_max;
            best_stamina_entry = candidate_stamina;
            best_stamina_max = candidate_stamina_max;
        }
    };

    if (*health_entry >= kMinimumPointerAddress) {
        uintptr_t candidate_stamina = 0;
        int64_t candidate_stamina_max = 0;
        if (TryResolveAdjacentStaminaEntry(*health_entry, &candidate_stamina, &candidate_stamina_max)) {
            consider_pair(*health_entry, candidate_stamina);
        }
    }

    if (*stamina_entry >= kMinimumPointerAddress) {
        uintptr_t candidate_health = 0;
        int64_t candidate_health_max = 0;
        if (TryResolveAdjacentHealthEntry(*stamina_entry, &candidate_health, &candidate_health_max)) {
            consider_pair(candidate_health, *stamina_entry);
        }
    }

    for (uintptr_t offset = kResolveRootEntryScanStart; offset <= kResolveRootEntryScanEnd; offset += sizeof(uintptr_t)) {
        uintptr_t candidate = 0;
        if (!TryReadPointer(root + offset, &candidate) || candidate < kMinimumPointerAddress) {
            continue;
        }

        int64_t candidate_max = 0;
        if (TryGetStatEntryMaxValue(candidate, kHealthId, &candidate_max) &&
            IsSupportedMountHealthMax(candidate_max)) {
            uintptr_t candidate_stamina = 0;
            int64_t candidate_stamina_max = 0;
            if (TryResolveAdjacentStaminaEntry(candidate, &candidate_stamina, &candidate_stamina_max)) {
                consider_pair(candidate, candidate_stamina);
            }
        }

        if (TryGetStaminaEntryMaxValue(candidate, &candidate_max) &&
            IsSupportedMountStaminaMax(candidate_max)) {
            uintptr_t candidate_health = 0;
            int64_t candidate_health_max = 0;
            if (TryResolveAdjacentHealthEntry(candidate, &candidate_health, &candidate_health_max)) {
                consider_pair(candidate_health, candidate);
            }
        }
    }

    if (best_health_entry >= kMinimumPointerAddress && best_stamina_entry >= kMinimumPointerAddress) {
        *health_entry = best_health_entry;
        *stamina_entry = best_stamina_entry;
    }
}

bool TryResolveActorFromMarker(const uintptr_t marker, uintptr_t* const actor) {
    if (actor == nullptr || marker < kMinimumPointerAddress) {
        return false;
    }

    uintptr_t marker_owner = 0;
    if (!TryReadPointer(marker + 0x8, &marker_owner) || marker_owner < kMinimumPointerAddress) {
        return false;
    }

    uintptr_t resolved_actor = 0;
    if (!TryReadPointer(marker_owner + 0x68, &resolved_actor) || resolved_actor < kMinimumPointerAddress) {
        return false;
    }

    uintptr_t confirmed_marker = 0;
    if (!TryReadPointer(resolved_actor + 0x20, &confirmed_marker) || confirmed_marker != marker) {
        return false;
    }

    *actor = resolved_actor;
    return true;
}

ObservedPlayerOwnerCandidate* FindObservedPlayerOwnerCandidateLocked(const uintptr_t owner) {
    for (auto& candidate : g_observed_player_owner_candidates) {
        if (candidate.owner == owner) {
            return &candidate;
        }
    }
    return nullptr;
}

ObservedPlayerOwnerCandidate* SelectObservedPlayerOwnerCandidateSlotLocked() {
    ObservedPlayerOwnerCandidate* oldest = &g_observed_player_owner_candidates.front();
    for (auto& candidate : g_observed_player_owner_candidates) {
        if (candidate.owner == 0) {
            return &candidate;
        }
        if (candidate.last_seen_tick < oldest->last_seen_tick) {
            oldest = &candidate;
        }
    }
    return oldest;
}

bool IsValidatedPlayableStaminaEntry(const uintptr_t entry) {
    if (entry < kMinimumPointerAddress) {
        return false;
    }

    std::lock_guard candidate_lock(g_observed_player_owner_mutex);
    for (const auto& candidate : g_observed_player_owner_candidates) {
        if (candidate.validated_playable_owner &&
            candidate.stamina_entry == entry) {
            return true;
        }
    }
    return false;
}

}  // namespace

bool TryResolvePlayerResourceSelectorsFromBase(
    const uintptr_t stamina_entry,
    std::uint16_t* const stamina_selector,
    std::uint16_t* const spirit_selector) {
    if (stamina_selector == nullptr || spirit_selector == nullptr) {
        return false;
    }

    DiscoveredPlayerResourcePair pair{};
    if (!TryReadDiscoveredPlayerResourcePair(stamina_entry, &pair) ||
        pair.stamina_type < 0 ||
        pair.spirit_type < 0 ||
        pair.stamina_type > (std::numeric_limits<std::uint16_t>::max)() ||
        pair.spirit_type > (std::numeric_limits<std::uint16_t>::max)()) {
        return false;
    }

    *stamina_selector = static_cast<std::uint16_t>(pair.stamina_type);
    *spirit_selector = static_cast<std::uint16_t>(pair.spirit_type);
    return true;
}

bool TryAdoptExplicitPlayerResourcePair(
    const uintptr_t observed_entry,
    const std::uint16_t explicit_selector,
    const int64_t observed_delta,
    const char* const reason) {
    if (observed_entry < kMinimumPointerAddress || observed_delta == 0) {
        return false;
    }

    DiscoveredPlayerResourcePair pair{};
    psm::routing::DiscoveredPlayerResourceRole observed_role =
        psm::routing::DiscoveredPlayerResourceRole::None;
    const bool pair_is_valid =
        TryReadDiscoveredPlayerResourcePairForEntry(
            observed_entry,
            &pair,
            &observed_role);
    const bool selector_matches_pair =
        pair_is_valid &&
        (explicit_selector == static_cast<std::uint16_t>(pair.stamina_type) ||
         explicit_selector == static_cast<std::uint16_t>(pair.spirit_type));

    ActorResolveSnapshot previous{};
    bool pair_matches_active_player = false;
    bool selector_contract_valid = false;
    {
        std::lock_guard state_lock(g_state_mutex);
        const ActorResolveSnapshot player = g_player_resolve;
        const ActorResolveSnapshot mount = g_mount_resolve;
        const bool has_identity_anchor =
            player.actor >= kMinimumPointerAddress ||
            player.marker >= kMinimumPointerAddress;
        const bool has_owner_anchor =
            player.root >= kMinimumPointerAddress ||
            player.damage_target >= kMinimumPointerAddress ||
            player.damage_source >= kMinimumPointerAddress;
        const bool has_authoritative_player_identity =
            has_identity_anchor && has_owner_anchor;
        const bool mount_is_trusted =
            g_mount_track_confidence.load(std::memory_order_acquire) >= 3 &&
            (mount.root >= kMinimumPointerAddress ||
             mount.damage_target >= kMinimumPointerAddress ||
             mount.stamina_entry >= kMinimumPointerAddress ||
             mount.spirit_entry >= kMinimumPointerAddress);
        const bool overlaps_trusted_mount =
            mount_is_trusted &&
            (pair.stamina_entry == mount.health_entry ||
             pair.stamina_entry == mount.stamina_entry ||
             pair.stamina_entry == mount.spirit_entry ||
             pair.spirit_entry == mount.health_entry ||
             pair.spirit_entry == mount.stamina_entry ||
             pair.spirit_entry == mount.spirit_entry);

        selector_contract_valid =
            psm::routing::ShouldPublishExplicitPlayerResourceSelectors(
                pair_is_valid,
                selector_matches_pair,
                observed_delta != 0,
                has_authoritative_player_identity,
                overlaps_trusted_mount);
        pair_matches_active_player =
            player.stamina_entry == pair.stamina_entry &&
            player.spirit_entry == pair.spirit_entry;
        if (!psm::routing::ShouldAdoptExplicitPlayerResourcePair(
                pair_is_valid,
                selector_matches_pair,
                observed_delta != 0,
                has_authoritative_player_identity,
                overlaps_trusted_mount,
                pair_matches_active_player)) {
            previous = player;
        } else {
            previous = player;
            g_player_resolve.stamina_type = pair.stamina_type;
            g_player_resolve.spirit_type = pair.spirit_type;
            g_player_resolve.stamina_offset_from_health = 0;
            g_player_resolve.spirit_offset_from_health = 0;
        }
    }

    if (!selector_contract_valid) {
        return false;
    }
    PublishAcceptedPlayerResourcePair(
        pair,
        reason != nullptr ? reason : "explicit selector calibration");
    if (!pair_matches_active_player) {
        const auto current = g_discovery_logs.fetch_add(1, std::memory_order_acq_rel);
        if (selector_contract_valid && current < 64) {
            Log("runtime: explicit selector calibrated but ownership rejected reason=%s observed=0x%p role=%d selector=%u candidate_stamina=0x%p candidate_spirit=0x%p active_stamina=0x%p active_spirit=0x%p",
                reason != nullptr ? reason : "explicit selector contract",
                reinterpret_cast<void*>(observed_entry),
                static_cast<int>(observed_role),
                static_cast<unsigned>(explicit_selector),
                reinterpret_cast<void*>(pair.stamina_entry),
                reinterpret_cast<void*>(pair.spirit_entry),
                reinterpret_cast<void*>(previous.stamina_entry),
                reinterpret_cast<void*>(previous.spirit_entry));
        }
        return false;
    }
    if (previous.stamina_type != pair.stamina_type ||
        previous.spirit_type != pair.spirit_type) {
        Log("runtime: explicit selector refreshed active player resource types reason=%s observed=0x%p role=%d selector=%u stamina=0x%p/type%d spirit=0x%p/type%d",
            reason != nullptr ? reason : "explicit selector contract",
            reinterpret_cast<void*>(observed_entry),
            static_cast<int>(observed_role),
            static_cast<unsigned>(explicit_selector),
            reinterpret_cast<void*>(pair.stamina_entry),
            pair.stamina_type,
            reinterpret_cast<void*>(pair.spirit_entry),
            pair.spirit_type);
    }
    return true;
}

bool SelectConfig(const ModConfig& config, const int32_t stat_type, StatConfig* const selected) {
    if (selected == nullptr) {
        return false;
    }

    if (stat_type == kHealthId) {
        *selected = config.health;
        return true;
    }

    if (IsStaminaStatId(stat_type)) {
        *selected = config.stamina;
        return true;
    }

    if (IsSpiritStatId(stat_type)) {
        *selected = config.spirit;
        return true;
    }

    return false;
}

bool IsValidatedPlayableStaminaResource(const uintptr_t stamina_entry) {
    return IsValidatedPlayableStaminaEntry(stamina_entry);
}

int64_t ClampToRange(const int64_t value, const int64_t minimum, const int64_t maximum) {
    return std::max(minimum, std::min(value, maximum));
}

int64_t ScaleDelta(const int64_t delta, const double multiplier) {
    const double scaled = std::floor(static_cast<double>(delta) * multiplier);
    if (scaled <= 0.0) {
        return 0;
    }

    if (scaled >= static_cast<double>(std::numeric_limits<int64_t>::max())) {
        return std::numeric_limits<int64_t>::max();
    }

    return static_cast<int64_t>(scaled);
}

bool IsTrackedStat(const int32_t stat_type) {
    return stat_type == kHealthId || IsStaminaStatId(stat_type) || IsSpiritStatId(stat_type);
}

void ObservePlayerStaminaOwnerCandidate(const uintptr_t owner_root,
                                        const uintptr_t stamina_entry) {
    if (owner_root < kMinimumPointerAddress || stamina_entry < kMinimumPointerAddress) {
        return;
    }

    int64_t current_value = 0;
    int64_t max_value = 0;
    if (!TryReadObservedStatEntryIfShape(stamina_entry,
                                        kStaminaId,
                                        true,
                                        &current_value,
                                        &max_value) ||
        !IsPlayerCombatStaminaMaxValue(max_value)) {
        return;
    }

    const ULONGLONG now = GetTickCount64();
    std::lock_guard candidate_lock(g_observed_player_owner_mutex);
    ObservedPlayerOwnerCandidate* candidate =
        FindObservedPlayerOwnerCandidateLocked(owner_root);
    if (candidate == nullptr) {
        candidate = SelectObservedPlayerOwnerCandidateSlotLocked();
        *candidate = {};
    }
    candidate->owner = owner_root;
    if (!candidate->validated_playable_owner ||
        candidate->stamina_entry < kMinimumPointerAddress ||
        candidate->stamina_entry == stamina_entry) {
        candidate->stamina_entry = stamina_entry;
        candidate->stamina_max = max_value;
    }
    candidate->last_seen_tick = now;
}

bool TryApplyObservedPlayerResourcePairForAuthoritativeOwner(
    const uintptr_t owner_root,
    const bool authoritative_capture,
    const bool identity_changed,
    ActorResolveSnapshot* const resolved) {
    if (resolved == nullptr || owner_root < kMinimumPointerAddress) {
        return false;
    }

    uintptr_t observed_stamina_entry = 0;
    {
        std::lock_guard candidate_lock(g_observed_player_owner_mutex);
        const ObservedPlayerOwnerCandidate* const candidate =
            FindObservedPlayerOwnerCandidateLocked(owner_root);
        if (candidate == nullptr) {
            return false;
        }
        observed_stamina_entry = candidate->stamina_entry;
    }

    DiscoveredPlayerResourcePair observed_pair{};
    const bool observed_pair_is_valid =
        TryReadDiscoveredPlayerResourcePair(observed_stamina_entry, &observed_pair);
    DiscoveredPlayerResourcePair resolved_pair{};
    const bool resolved_pair_is_valid =
        TryReadDiscoveredPlayerResourcePair(resolved->stamina_entry, &resolved_pair) &&
        resolved_pair.spirit_entry == resolved->spirit_entry;
    if (!psm::routing::ShouldUseObservedOwnerPairForAuthoritativeCapture(
            authoritative_capture,
            identity_changed,
            true,
            observed_pair_is_valid,
            resolved_pair_is_valid)) {
        return false;
    }

    const uintptr_t replaced_stamina = resolved->stamina_entry;
    const uintptr_t replaced_spirit = resolved->spirit_entry;
    resolved->stamina_entry = observed_pair.stamina_entry;
    resolved->spirit_entry = observed_pair.spirit_entry;
    resolved->resource_owner = owner_root;
    resolved->stamina_type = observed_pair.stamina_type;
    resolved->spirit_type = observed_pair.spirit_type;
    resolved->stamina_offset_from_health = 0;
    resolved->spirit_offset_from_health = 0;
    PublishAcceptedPlayerResourcePair(
        observed_pair,
        "authoritative owner adjacent-pair handoff");
    Log("runtime: authoritative owner adopted cached adjacent resources owner=0x%p stamina=0x%p spirit=0x%p replaced_stamina=0x%p replaced_spirit=0x%p",
        reinterpret_cast<void*>(owner_root),
        reinterpret_cast<void*>(observed_pair.stamina_entry),
        reinterpret_cast<void*>(observed_pair.spirit_entry),
        reinterpret_cast<void*>(replaced_stamina),
        reinterpret_cast<void*>(replaced_spirit));
    return true;
}

void ObservePlayerStatusOwnerCandidate(const uintptr_t owner_root,
                                       const int32_t status_id,
                                       const int64_t delta,
                                       const uintptr_t source_context) {
    if (owner_root < kMinimumPointerAddress || status_id <= 0 || delta == 0) {
        return;
    }

    const ULONGLONG now = GetTickCount64();
    std::lock_guard candidate_lock(g_observed_player_owner_mutex);
    ObservedPlayerOwnerCandidate* candidate =
        FindObservedPlayerOwnerCandidateLocked(owner_root);
    if (candidate == nullptr) {
        candidate = SelectObservedPlayerOwnerCandidateSlotLocked();
        *candidate = {};
        candidate->owner = owner_root;
    } else if (candidate->status_last_seen_tick != 0 &&
               now - candidate->status_last_seen_tick > kPlayerStatusBootstrapWindowMs) {
        candidate->status_ids = {};
        candidate->distinct_status_count = 0;
        candidate->saw_positive_status_delta = false;
        candidate->saw_negative_status_delta = false;
        candidate->saw_valid_status_source = false;
        candidate->status_first_seen_tick = 0;
    }

    candidate->owner = owner_root;
    candidate->last_seen_tick = now;
    candidate->status_last_seen_tick = now;
    if (candidate->status_first_seen_tick == 0) {
        candidate->status_first_seen_tick = now;
    }
    candidate->saw_positive_status_delta |= delta > 0;
    candidate->saw_negative_status_delta |= delta < 0;
    candidate->saw_valid_status_source |= source_context >= kMinimumPointerAddress;

    bool status_seen = false;
    for (std::uint32_t index = 0; index < candidate->distinct_status_count; ++index) {
        if (candidate->status_ids[index] == status_id) {
            status_seen = true;
            break;
        }
    }
    if (!status_seen && candidate->distinct_status_count < candidate->status_ids.size()) {
        candidate->status_ids[candidate->distinct_status_count++] = status_id;
    }
}

bool TryActivateCorrelatedPlayerOwnerCandidate(const uintptr_t owner_root,
                                               const uintptr_t stamina_entry,
                                               const int64_t observed_delta,
                                               const char* const reason) {
    if (owner_root < kMinimumPointerAddress || stamina_entry < kMinimumPointerAddress) {
        return false;
    }

    ObservePlayerStaminaOwnerCandidate(owner_root, stamina_entry);

    ObservedPlayerOwnerCandidate candidate{};
    {
        std::lock_guard candidate_lock(g_observed_player_owner_mutex);
        const ObservedPlayerOwnerCandidate* const found =
            FindObservedPlayerOwnerCandidateLocked(owner_root);
        if (found == nullptr) {
            return false;
        }
        candidate = *found;
    }

    const ULONGLONG now = GetTickCount64();
    const bool status_evidence_recent =
        candidate.status_first_seen_tick != 0 &&
        candidate.status_last_seen_tick != 0 &&
        now - candidate.status_last_seen_tick <= kPlayerStatusBootstrapWindowMs &&
        candidate.status_last_seen_tick - candidate.status_first_seen_tick <= kPlayerStatusBootstrapWindowMs;

    int64_t stamina_current = 0;
    int64_t stamina_max = 0;
    const bool has_player_stamina_shape =
        TryReadObservedStatEntryIfShape(stamina_entry,
                                        kStaminaId,
                                        true,
                                        &stamina_current,
                                        &stamina_max) &&
        IsPlayerCombatStaminaMaxValue(stamina_max);

    ActorResolveSnapshot observed_player_block{};
    constexpr uintptr_t stamina_offsets[] = {
        kStaminaEntryOffsetFromHealth,
        kLegacyStaminaEntryOffsetFromHealth,
    };
    for (const uintptr_t offset : stamina_offsets) {
        if (stamina_entry <= offset) {
            continue;
        }
        if (TryResolvePlayerCombatBlockFromHealth(
                stamina_entry - offset,
                &observed_player_block) &&
            observed_player_block.stamina_entry == stamina_entry) {
            break;
        }
        observed_player_block = {};
    }
    const bool has_complete_player_combat_block =
        observed_player_block.health_entry >= kMinimumPointerAddress &&
        observed_player_block.stamina_entry == stamina_entry &&
        observed_player_block.spirit_entry >= kMinimumPointerAddress;

    ActorResolveSnapshot player{};
    ActorResolveSnapshot mount{};
    {
        std::lock_guard state_lock(g_state_mutex);
        player = g_player_resolve;
        mount = g_mount_resolve;
    }
    const bool resource_is_tracked_mount =
        mount.root == owner_root ||
        mount.damage_target == owner_root ||
        mount.stamina_entry == stamina_entry ||
        mount.spirit_entry == stamina_entry;
    const bool same_player_owner =
        player.root == owner_root ||
        player.damage_target == owner_root ||
        player.resource_owner == owner_root;
    const bool would_replace_existing_player_stamina =
        same_player_owner &&
        player.stamina_entry >= kMinimumPointerAddress &&
        player.stamina_entry != stamina_entry;

    const bool handshake_valid =
        psm::routing::ShouldPromotePlayerFromStatusResourceHandshake(
            status_evidence_recent,
            candidate.distinct_status_count,
            candidate.saw_positive_status_delta,
            candidate.saw_negative_status_delta,
            candidate.saw_valid_status_source,
            candidate.owner == owner_root,
            has_player_stamina_shape,
            resource_is_tracked_mount,
            would_replace_existing_player_stamina);

    bool candidate_is_validated = candidate.validated_playable_owner;
    bool resource_matches_candidate_stamina = candidate.stamina_entry == stamina_entry;
    if (handshake_valid) {
        std::lock_guard candidate_lock(g_observed_player_owner_mutex);
        ObservedPlayerOwnerCandidate* const found =
            FindObservedPlayerOwnerCandidateLocked(owner_root);
        if (found != nullptr && found->stamina_entry == stamina_entry) {
            found->validated_playable_owner = true;
            found->stamina_max = stamina_max;
            found->last_seen_tick = now;
            candidate_is_validated = true;
            resource_matches_candidate_stamina = true;
        }
    }

    const bool recent_opposite_load_fill =
        observed_delta < 0 &&
        candidate.last_positive_resource_delta > 0 &&
        observed_delta == -candidate.last_positive_resource_delta &&
        candidate.last_positive_resource_tick != 0 &&
        now - candidate.last_positive_resource_tick <= 500;

    const bool active_resource_valid =
        psm::routing::ShouldValidateObservedPlayerOwnerFromActiveResourceUse(
            resource_matches_candidate_stamina,
            observed_delta < 0,
            has_complete_player_combat_block,
            resource_is_tracked_mount,
            recent_opposite_load_fill);
    if (!candidate_is_validated && active_resource_valid) {
        std::lock_guard candidate_lock(g_observed_player_owner_mutex);
        ObservedPlayerOwnerCandidate* const found =
            FindObservedPlayerOwnerCandidateLocked(owner_root);
        if (found != nullptr && found->stamina_entry == stamina_entry) {
            found->validated_playable_owner = true;
            found->stamina_max = stamina_max;
            found->last_seen_tick = now;
            candidate_is_validated = true;
            resource_matches_candidate_stamina = true;
        }
    }

    {
        std::lock_guard candidate_lock(g_observed_player_owner_mutex);
        ObservedPlayerOwnerCandidate* const found =
            FindObservedPlayerOwnerCandidateLocked(owner_root);
        if (found != nullptr && found->stamina_entry == stamina_entry) {
            if (observed_delta > 0) {
                found->last_positive_resource_delta = observed_delta;
                found->last_positive_resource_tick = now;
            } else if (observed_delta < 0) {
                found->last_positive_resource_delta = 0;
                found->last_positive_resource_tick = 0;
            }
        }
    }

    const uintptr_t confirmed_owner =
        g_confirmed_player_owner.load(std::memory_order_acquire);
    if (candidate_is_validated && confirmed_owner < kMinimumPointerAddress) {
        return TryActivateObservedPlayerOwnerCandidate(
            owner_root,
            "initial validated player resource");
    }

    if (!psm::routing::ShouldActivateValidatedPlayerOwnerFromResourceUse(
            candidate_is_validated,
            resource_matches_candidate_stamina,
            observed_delta < 0,
            has_player_stamina_shape,
            resource_is_tracked_mount,
            recent_opposite_load_fill)) {
        if (recent_opposite_load_fill) {
            const auto current = g_discovery_logs.fetch_add(1, std::memory_order_acq_rel);
            if (current < 96) {
                Log("runtime: ignored paired load-time stamina reversal owner=0x%p entry=0x%p delta=%lld",
                    reinterpret_cast<void*>(owner_root),
                    reinterpret_cast<void*>(stamina_entry),
                    static_cast<long long>(observed_delta));
            }
        }
        return false;
    }

    return TryActivateObservedPlayerOwnerCandidate(
        owner_root,
        reason,
        true);
}

bool TryActivateObservedPlayerOwnerCandidate(const uintptr_t owner_root,
                                             const char* const reason,
                                             const bool allow_split_resource_owner) {
    if (owner_root < kMinimumPointerAddress) {
        return false;
    }

    ObservedPlayerOwnerCandidate candidate{};
    {
        std::lock_guard candidate_lock(g_observed_player_owner_mutex);
        const ObservedPlayerOwnerCandidate* const found =
            FindObservedPlayerOwnerCandidateLocked(owner_root);
        if (found == nullptr || !found->validated_playable_owner) {
            return false;
        }
        candidate = *found;
    }

    DiscoveredPlayerResourcePair accepted_pair{};
    if (!TryReadDiscoveredPlayerResourcePair(candidate.stamina_entry, &accepted_pair)) {
        return false;
    }
    const int64_t stamina_max = accepted_pair.stamina_max;

    ActorResolveSnapshot observed_player_block{};
    constexpr uintptr_t stamina_offsets[] = {
        kStaminaEntryOffsetFromHealth,
        kLegacyStaminaEntryOffsetFromHealth,
    };
    for (const uintptr_t offset : stamina_offsets) {
        if (candidate.stamina_entry <= offset) {
            continue;
        }
        if (TryResolvePlayerCombatBlockFromHealth(
                candidate.stamina_entry - offset,
                &observed_player_block) &&
            observed_player_block.stamina_entry == candidate.stamina_entry) {
            break;
        }
        observed_player_block = {};
    }
    const bool has_complete_player_combat_block =
        observed_player_block.health_entry >= kMinimumPointerAddress &&
        observed_player_block.stamina_entry == candidate.stamina_entry &&
        observed_player_block.spirit_entry >= kMinimumPointerAddress;

    ActorResolveSnapshot previous{};
    bool owner_changed = false;
    bool resource_owner_changed = false;
    uintptr_t active_identity_owner = owner_root;
    {
        std::lock_guard state_lock(g_state_mutex);
        previous = g_player_resolve;
        const bool has_established_player_identity =
            previous.root >= kMinimumPointerAddress ||
            previous.damage_target >= kMinimumPointerAddress;
        const bool exact_authoritative_owner_match =
            previous.root == owner_root ||
            previous.damage_target == owner_root;
        const bool exact_resource_owner_match =
            previous.resource_owner == owner_root;
        const ActorResolveSnapshot mount = g_mount_resolve;
        const bool mount_is_trusted =
            g_mount_track_confidence.load(std::memory_order_acquire) >= 3 &&
            (mount.root >= kMinimumPointerAddress ||
             mount.damage_target >= kMinimumPointerAddress ||
             mount.stamina_entry >= kMinimumPointerAddress ||
             mount.spirit_entry >= kMinimumPointerAddress);
        const bool overlaps_trusted_mount =
            mount_is_trusted &&
            (owner_root == mount.root ||
             owner_root == mount.damage_target ||
             accepted_pair.stamina_entry == mount.health_entry ||
             accepted_pair.stamina_entry == mount.stamina_entry ||
             accepted_pair.stamina_entry == mount.spirit_entry ||
             accepted_pair.spirit_entry == mount.health_entry ||
             accepted_pair.spirit_entry == mount.stamina_entry ||
             accepted_pair.spirit_entry == mount.spirit_entry);
        const bool legacy_owner_commit =
            psm::routing::ShouldCommitObservedPlayerOwnerPair(
                has_established_player_identity,
                exact_authoritative_owner_match,
                true);
        const bool split_owner_commit =
            allow_split_resource_owner &&
            psm::routing::ShouldBindSplitPlayerResourceOwner(
                has_established_player_identity,
                exact_authoritative_owner_match,
                exact_resource_owner_match,
                true,
                true,
                overlaps_trusted_mount);
        if (!legacy_owner_commit && !split_owner_commit) {
            const auto current = g_discovery_logs.fetch_add(1, std::memory_order_acq_rel);
            if (current < 96) {
                Log("runtime: rejected observed player owner pair reason=%s owner=0x%p active_root=0x%p active_target=0x%p active_resource_owner=0x%p candidate_stamina=0x%p candidate_spirit=0x%p mount_overlap=%d split_allowed=%d",
                    reason != nullptr ? reason : "unknown",
                    reinterpret_cast<void*>(owner_root),
                    reinterpret_cast<void*>(previous.root),
                    reinterpret_cast<void*>(previous.damage_target),
                    reinterpret_cast<void*>(previous.resource_owner),
                    reinterpret_cast<void*>(accepted_pair.stamina_entry),
                    reinterpret_cast<void*>(accepted_pair.spirit_entry),
                    overlaps_trusted_mount ? 1 : 0,
                    allow_split_resource_owner ? 1 : 0);
            }
            return false;
        }
        owner_changed = !has_established_player_identity &&
            psm::routing::ObservedOwnerChanged(
                previous.root,
                previous.damage_target,
                owner_root);
        resource_owner_changed = previous.resource_owner != owner_root;
        ActorResolveSnapshot next = previous;
        if (has_complete_player_combat_block &&
            (!has_established_player_identity || exact_authoritative_owner_match)) {
            next.health_entry = observed_player_block.health_entry;
            next.stamina_entry = observed_player_block.stamina_entry;
            next.spirit_entry = observed_player_block.spirit_entry;
        }
        if (!has_established_player_identity) {
            next.root = owner_root;
            next.damage_target = owner_root;
            next.damage_source = owner_root;
        }
        next.resource_owner = owner_root;
        next.stamina_entry = accepted_pair.stamina_entry;
        next.spirit_entry = accepted_pair.spirit_entry;
        next.stamina_type = accepted_pair.stamina_type;
        next.spirit_type = accepted_pair.spirit_type;
        AssignTrackedPlayerResolveLocked(next);
        NotifyResourceHudBindingLocked();
        active_identity_owner = next.root >= kMinimumPointerAddress
            ? next.root
            : (next.damage_target >= kMinimumPointerAddress
                ? next.damage_target
                : owner_root);
        PublishAcceptedPlayerResourcePair(
            accepted_pair,
            reason != nullptr ? reason : "validated player resource");

        if (g_mount_resolve.root == owner_root ||
            g_mount_resolve.damage_target == owner_root ||
            g_mount_resolve.stamina_entry == candidate.stamina_entry) {
            ResetTrackedMountLocked();
        }
        if (owner_changed) {
            ResetTrackedDamageParticipantsLocked();
        }
    }

    const ULONGLONG now = GetTickCount64();
    g_confirmed_player_owner.store(active_identity_owner, std::memory_order_release);
    g_confirmed_player_owner_tick.store(now, std::memory_order_release);
    if (owner_changed || resource_owner_changed) {
        g_player_owner_activation_tick.store(now, std::memory_order_release);
        g_correlated_player_spirit_owner.store(0, std::memory_order_release);
    }

    const auto current = g_discovery_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 96) {
        Log("runtime: activated observed player resource owner reason=%s owner=0x%p health=0x%p stamina=0x%p spirit=0x%p stamina_max=%lld status_root=0x%p old_resource_owner=0x%p identity_owner_changed=%d resource_owner_changed=%d complete_block=%d split=%d",
            reason != nullptr ? reason : "unknown",
            reinterpret_cast<void*>(owner_root),
            reinterpret_cast<void*>(has_complete_player_combat_block
                                        ? observed_player_block.health_entry
                                        : 0),
            reinterpret_cast<void*>(candidate.stamina_entry),
            reinterpret_cast<void*>(has_complete_player_combat_block
                                        ? observed_player_block.spirit_entry
                                        : 0),
            static_cast<long long>(stamina_max),
            reinterpret_cast<void*>(previous.root),
            reinterpret_cast<void*>(previous.resource_owner),
            owner_changed ? 1 : 0,
            resource_owner_changed ? 1 : 0,
            has_complete_player_combat_block ? 1 : 0,
            allow_split_resource_owner ? 1 : 0);
    }
    return true;
}

bool MarkTrackedPlayerOwnerConfirmed(const uintptr_t owner_root,
                                     const char* const reason) {
    if (owner_root < kMinimumPointerAddress) {
        return false;
    }

    ActorResolveSnapshot player{};
    {
        std::lock_guard state_lock(g_state_mutex);
        player = g_player_resolve;
    }
    if (player.root != owner_root && player.damage_target != owner_root) {
        return false;
    }

    const ULONGLONG now = GetTickCount64();
    const uintptr_t previous_confirmed_owner =
        g_confirmed_player_owner.exchange(owner_root, std::memory_order_acq_rel);
    g_confirmed_player_owner_tick.store(now, std::memory_order_release);
    if (previous_confirmed_owner != owner_root) {
        g_player_owner_activation_tick.store(now, std::memory_order_release);
        g_correlated_player_spirit_owner.store(0, std::memory_order_release);
    }
    static std::atomic<std::uint32_t> confirmation_logs{0};
    const auto current = confirmation_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 24) {
        Log("runtime: confirmed active player owner reason=%s owner=0x%p stamina=0x%p spirit=0x%p",
            reason != nullptr ? reason : "unknown",
            reinterpret_cast<void*>(owner_root),
            reinterpret_cast<void*>(player.stamina_entry),
            reinterpret_cast<void*>(player.spirit_entry));
    }
    return true;
}

bool IsExactConfirmedTrackedPlayerOwner(const uintptr_t owner_root) {
    if (owner_root < kMinimumPointerAddress ||
        g_confirmed_player_owner.load(std::memory_order_acquire) != owner_root) {
        return false;
    }

    std::lock_guard state_lock(g_state_mutex);
    return g_player_resolve.root == owner_root ||
           g_player_resolve.damage_target == owner_root;
}

bool ObservedResourceOwnerMatchesConfirmedPlayerToken(const uintptr_t observed_owner) {
    const uintptr_t confirmed_owner =
        g_confirmed_player_owner.load(std::memory_order_acquire);
    return confirmed_owner >= kMinimumPointerAddress &&
           observed_owner >= kMinimumPointerAddress &&
           psm::routing::ObservedSpiritOwnerTokenMatchesPlayer(
               confirmed_owner,
               observed_owner);
}

bool TryBindObservedSpiritToConfirmedPlayer(const uintptr_t spirit_entry,
                                            const uintptr_t observed_owner,
                                            const int64_t observed_delta,
                                            const char* const reason) {
    if (spirit_entry < kMinimumPointerAddress) {
        return false;
    }

    const uintptr_t confirmed_owner =
        g_confirmed_player_owner.load(std::memory_order_acquire);
    const bool observed_owner_matches_player =
        observed_owner >= kMinimumPointerAddress &&
        psm::routing::ObservedSpiritOwnerTokenMatchesPlayer(
            confirmed_owner,
            observed_owner);
    if (psm::routing::ShouldRejectObservedSpiritCandidate(
            IsValidatedPlayableStaminaEntry(spirit_entry),
            observed_owner_matches_player)) {
        return false;
    }

    int64_t spirit_current = 0;
    int64_t spirit_max = 0;
    if (!TryReadObservedStatEntryIfShape(spirit_entry,
                                        kSpiritId,
                                        false,
                                        &spirit_current,
                                        &spirit_max)) {
        return false;
    }

    const bool compact_spirit_shape = IsPlayerCombatSpiritMaxValue(spirit_max);
    const bool large_spirit_mirror_shape = IsPlayerLargeSpiritMirrorMaxValue(spirit_max);
    if (!compact_spirit_shape && !large_spirit_mirror_shape) {
        return false;
    }

    const ULONGLONG confirmed_tick =
        g_confirmed_player_owner_tick.load(std::memory_order_acquire);
    const ULONGLONG now = GetTickCount64();
    const bool confirmation_is_recent =
        confirmed_tick != 0 && now - confirmed_tick <= kActivePlayerConfirmationWindowMs;
    const ULONGLONG owner_activation_tick =
        g_player_owner_activation_tick.load(std::memory_order_acquire);
    const bool owner_activation_grace_active =
        owner_activation_tick != 0 &&
        now - owner_activation_tick <= kObservedSpiritOwnerGraceMs;
    const bool observed_owner_present = observed_owner >= kMinimumPointerAddress;
    const bool owner_token_matches = observed_owner_matches_player;
    const bool correlated_binding_active =
        g_correlated_player_spirit_owner.load(std::memory_order_acquire) >=
        kMinimumPointerAddress;
    if (!psm::routing::ShouldAcceptObservedSpiritOwner(
            observed_owner_present,
            owner_token_matches,
            owner_activation_grace_active,
            correlated_binding_active)) {
        static std::atomic<std::uint32_t> owner_rejection_logs{0};
        const auto current = owner_rejection_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 48) {
            Log("runtime: rejected observed spirit owner reason=%s player_owner=0x%p observed_owner=0x%p entry=0x%p grace=%d correlated=%d",
                reason != nullptr ? reason : "unknown",
                reinterpret_cast<void*>(confirmed_owner),
                reinterpret_cast<void*>(observed_owner),
                reinterpret_cast<void*>(spirit_entry),
                owner_activation_grace_active ? 1 : 0,
                correlated_binding_active ? 1 : 0);
        }
        return false;
    }

    ActorResolveSnapshot previous{};
    {
        std::lock_guard state_lock(g_state_mutex);
        previous = g_player_resolve;

        DiscoveredPlayerResourcePair active_pair{};
        const bool active_pair_is_valid =
            TryReadDiscoveredPlayerResourcePair(previous.stamina_entry, &active_pair) &&
            active_pair.spirit_entry == previous.spirit_entry;
        if (active_pair_is_valid && spirit_entry != active_pair.spirit_entry) {
            const auto current = g_discovery_logs.fetch_add(1, std::memory_order_acq_rel);
            if (current < 96) {
                Log("runtime: rejected observed spirit replacement reason=%s accepted_stamina=0x%p accepted_spirit=0x%p candidate_spirit=0x%p",
                    reason != nullptr ? reason : "unknown",
                    reinterpret_cast<void*>(active_pair.stamina_entry),
                    reinterpret_cast<void*>(active_pair.spirit_entry),
                    reinterpret_cast<void*>(spirit_entry));
            }
            return false;
        }

        int64_t stamina_current = 0;
        int64_t stamina_max = 0;
        const bool has_player_stamina =
            TryReadObservedStatEntryIfShape(previous.stamina_entry,
                                            kStaminaId,
                                            true,
                                            &stamina_current,
                                            &stamina_max) &&
            IsPlayerCombatStaminaMaxValue(stamina_max);
        const bool owner_matches =
            confirmed_owner >= kMinimumPointerAddress &&
            (previous.root == confirmed_owner || previous.damage_target == confirmed_owner);
        const bool observed_active_cost = observed_delta < 0;
        if (large_spirit_mirror_shape &&
            !psm::routing::ShouldRouteLargeType20CostToPlayerSpirit(
                GetResolvedSpiritStatId(),
                spirit_max,
                observed_delta,
                GetResolvedSpiritStatId(),
                kPlayerCombatStaminaResolveMinMax,
                kPlayerCombatStaminaResolveMaxMax,
                owner_matches,
                has_player_stamina,
                spirit_entry == previous.stamina_entry,
                spirit_entry == g_mount_resolve.stamina_entry,
                spirit_entry == g_mount_resolve.spirit_entry)) {
            return false;
        }
        if (!psm::routing::ShouldBindObservedSpiritToConfirmedPlayer(
                owner_matches,
                has_player_stamina,
                confirmation_is_recent,
                true,
                observed_active_cost)) {
            return false;
        }

        g_player_resolve.spirit_entry = spirit_entry;
        NotifyResourceHudBindingLocked();
        if (g_mount_resolve.spirit_entry == spirit_entry ||
            g_mount_resolve.stamina_entry == spirit_entry) {
            ResetTrackedMountLocked();
        }
    }

    if (owner_token_matches) {
        g_correlated_player_spirit_owner.store(observed_owner, std::memory_order_release);
    }

    const auto current = g_discovery_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 96) {
        Log("runtime: bound observed spirit to confirmed player reason=%s owner=0x%p observed_owner=0x%p entry=0x%p current=%lld max=%lld previous_spirit=0x%p correlated=%d legacy_fallback=%d",
            reason != nullptr ? reason : "unknown",
            reinterpret_cast<void*>(confirmed_owner),
            reinterpret_cast<void*>(observed_owner),
            reinterpret_cast<void*>(spirit_entry),
            static_cast<long long>(spirit_current),
            static_cast<long long>(spirit_max),
            reinterpret_cast<void*>(previous.spirit_entry),
            owner_token_matches ? 1 : 0,
            owner_token_matches ? 0 : 1);
    }
    return true;
}

void ResetObservedPlayerOwnerCandidates() {
    {
        std::lock_guard candidate_lock(g_observed_player_owner_mutex);
        g_observed_player_owner_candidates = {};
    }
    g_confirmed_player_owner.store(0, std::memory_order_release);
    g_confirmed_player_owner_tick.store(0, std::memory_order_release);
    g_player_owner_activation_tick.store(0, std::memory_order_release);
    g_correlated_player_spirit_owner.store(0, std::memory_order_release);
}

bool TryAssignPlayerResolvedEntry(const uintptr_t entry, const int32_t stat_type) {
    if (entry < kMinimumPointerAddress || !IsTrackedStat(stat_type)) {
        return false;
    }

    auto resolved = g_player_resolve;
    if (!resolved.valid()) {
        return false;
    }

    DiscoveredPlayerResourcePair active_pair{};
    const bool active_pair_is_valid =
        TryReadDiscoveredPlayerResourcePair(resolved.stamina_entry, &active_pair) &&
        active_pair.spirit_entry == resolved.spirit_entry;

    bool changed = false;
    if (stat_type == kHealthId) {
        if (resolved.health_entry >= kMinimumPointerAddress &&
            resolved.health_entry != entry) {
            return false;
        }
        if (resolved.health_entry != entry) {
            resolved.health_entry = entry;
            changed = true;
        }
    } else if (IsStaminaStatId(stat_type)) {
        if (active_pair_is_valid && entry != active_pair.stamina_entry) {
            return false;
        }
        if (resolved.stamina_entry != entry) {
            resolved.stamina_entry = entry;
            changed = true;
        }
    } else if (IsSpiritStatId(stat_type)) {
        if (active_pair_is_valid && entry != active_pair.spirit_entry) {
            return false;
        }
        if (resolved.spirit_entry == entry) {
            return false;
        }

        int32_t existing_type = -1;
        if (resolved.spirit_entry >= kMinimumPointerAddress &&
            TryReadStatTypeValue(resolved.spirit_entry, &existing_type)) {

            const bool existing_primary = IsCurrentSpiritStatId(existing_type);
            const bool candidate_primary = IsCurrentSpiritStatId(stat_type);
            if (existing_primary || !candidate_primary) {
                return false;
            }
        }

        if (resolved.spirit_entry != entry) {
            resolved.spirit_entry = entry;
            changed = true;
        }
    }

    if (!changed) {
        return false;
    }

    AssignTrackedPlayerResolveLocked(resolved);
    NotifyResourceHudBindingLocked();
    return true;
}

bool TryBootstrapPlayerResolveFromStatComponent(const uintptr_t entry, const uintptr_t component) {
    if (entry < kMinimumPointerAddress || component < kMinimumPointerAddress) {
        return false;
    }

    const int32_t stat_type = *reinterpret_cast<const int32_t*>(entry);
    if (!IsTrackedStat(stat_type)) {
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

    if (stat_type == kHealthId) {
        if (resolved.health_entry != entry) {
            return false;
        }
    } else if (IsStaminaStatId(stat_type)) {
        if (resolved.stamina_entry != entry) {
            return false;
        }
    } else if (IsSpiritStatId(stat_type)) {
        if (resolved.spirit_entry != entry) {
            return false;
        }
    } else {
        return false;
    }

    std::lock_guard state_lock(g_state_mutex);

    const ActorResolveSnapshot existing = g_player_resolve;
    if (existing.health_entry >= kMinimumPointerAddress) {
        // Fix-13: do not let a later stats/component resolver replace the
        // health-write fallback player entry.  In 1.05.01 the stats callback
        // can observe other player-like/companion components long after fall
        // damage has already identified the active player health entry.  The
        // previous fix-12B behavior overwrote g_player_resolve with that later
        // component, causing real player health writes to become "untracked"
        // again.  Only merge the stats resolve when it refers to the same
        // health entry; otherwise keep the working health-write fallback.
        if (resolved.health_entry != existing.health_entry) {
            return false;
        }

        ActorResolveSnapshot merged = existing;
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
        if (resolved.stamina_entry == merged.stamina_entry) {
            merged.stamina_type = resolved.stamina_type;
            merged.stamina_offset_from_health = resolved.stamina_offset_from_health;
        }
        if (resolved.spirit_entry == merged.spirit_entry) {
            merged.spirit_type = resolved.spirit_type;
            merged.spirit_offset_from_health = resolved.spirit_offset_from_health;
        }

        AssignTrackedPlayerResolveLocked(merged);
        NotifyResourceHudBindingLocked();
        return true;
    }

    if (g_player_resolve.valid()) {
        return false;
    }

    AssignTrackedPlayerResolveLocked(resolved);
    NotifyResourceHudBindingLocked();
    ResetTrackedDamageParticipantsLocked();
    return true;
}


bool TryBootstrapPlayerResolveFromHealthWrite(const uintptr_t health_entry) {
    if (health_entry < kMinimumPointerAddress || !IsValidStatEntry(health_entry, kHealthId)) {
        return false;
    }

    int64_t health_max = 0;
    if (!TryGetStatEntryMaxValue(health_entry, kHealthId, &health_max) ||
        health_max < kPlayerHealthWriteFallbackMinMax ||
        health_max >= kMountHealthResolveMinMax) {
        return false;
    }

    ActorResolveSnapshot resolved_from_health{};
    const bool has_complete_block =
        TryResolvePlayerCombatBlockFromHealth(health_entry, &resolved_from_health) &&
        resolved_from_health.health_entry == health_entry;

    std::lock_guard state_lock(g_state_mutex);

    if (g_player_resolve.health_entry >= kMinimumPointerAddress) {
        if (g_player_resolve.health_entry == health_entry) {
            if (has_complete_block) {
                const ActorResolveSnapshot existing = g_player_resolve;
                resolved_from_health.actor = existing.actor;
                resolved_from_health.marker = existing.marker;
                resolved_from_health.root = existing.root;
                resolved_from_health.damage_source = existing.damage_source;
                resolved_from_health.damage_target = existing.damage_target;
                AssignTrackedPlayerResolveLocked(resolved_from_health);
                NotifyResourceHudBindingLocked();
            }
            return true;
        }

        if (!has_complete_block) {
            return false;
        }

        const ActorResolveSnapshot existing = g_player_resolve;
        resolved_from_health.actor = existing.actor;
        resolved_from_health.marker = existing.marker;
        resolved_from_health.root = existing.root;
        resolved_from_health.damage_source = existing.damage_source;
        resolved_from_health.damage_target = existing.damage_target;
        AssignTrackedPlayerResolveLocked(resolved_from_health);
        NotifyResourceHudBindingLocked();
        ResetTrackedDamageParticipantsLocked();
        return true;
    }

    if (has_complete_block) {
        AssignTrackedPlayerResolveLocked(resolved_from_health);
        NotifyResourceHudBindingLocked();
    } else {
        // Fall/environment damage may identify the active Health entry before
        // the complete resource block is initialized. Keep Health tracking but
        // never guess Stamina or Spirit from stale numeric IDs.
        AssignTrackedPlayerPointerFieldLocked(&ActorResolveSnapshot::health_entry, health_entry);
    }
    ResetTrackedDamageParticipantsLocked();
    return true;
}


bool TryBootstrapPlayerStaminaFromEntry(const uintptr_t stamina_entry) {
    if (stamina_entry < kMinimumPointerAddress) {
        return false;
    }

    ActorResolveSnapshot resolved_from_stamina{};
    if (!TryResolvePlayerSnapshotFromStaminaOffset(stamina_entry, &resolved_from_stamina) ||
        resolved_from_stamina.stamina_entry != stamina_entry) {
        return false;
    }

    std::lock_guard state_lock(g_state_mutex);

    if (g_player_resolve.stamina_entry == stamina_entry) {
        g_player_resolve.stamina_type = resolved_from_stamina.stamina_type;
        g_player_resolve.spirit_type = resolved_from_stamina.spirit_type;
        g_player_resolve.stamina_offset_from_health = resolved_from_stamina.stamina_offset_from_health;
        g_player_resolve.spirit_offset_from_health = resolved_from_stamina.spirit_offset_from_health;
        PublishAcceptedPlayerResourceSnapshot(g_player_resolve, "stamina bootstrap refresh");
        return true;
    }

    if (g_player_resolve.stamina_entry >= kMinimumPointerAddress) {
        return false;
    }

    if (g_player_resolve.health_entry >= kMinimumPointerAddress &&
        g_player_resolve.health_entry != resolved_from_stamina.health_entry) {
        return false;
    }

    AssignTrackedPlayerResolveLocked(resolved_from_stamina);
    NotifyResourceHudBindingLocked();
    PublishAcceptedPlayerResourceSnapshot(g_player_resolve, "stamina bootstrap");
    ResetTrackedDamageParticipantsLocked();
    return true;
}

bool TryBootstrapPlayerSpiritFromEntry(const uintptr_t spirit_entry) {
    if (spirit_entry < kMinimumPointerAddress) {
        return false;
    }

    ActorResolveSnapshot resolved_from_spirit{};
    if (!TryResolvePlayerCombatBlockFromResourceEntry(spirit_entry, false, &resolved_from_spirit) ||
        resolved_from_spirit.spirit_entry != spirit_entry) {
        return false;
    }

    std::lock_guard state_lock(g_state_mutex);
    if (g_player_resolve.spirit_entry == spirit_entry) {
        g_player_resolve.stamina_type = resolved_from_spirit.stamina_type;
        g_player_resolve.spirit_type = resolved_from_spirit.spirit_type;
        g_player_resolve.stamina_offset_from_health = resolved_from_spirit.stamina_offset_from_health;
        g_player_resolve.spirit_offset_from_health = resolved_from_spirit.spirit_offset_from_health;
        PublishAcceptedPlayerResourceSnapshot(g_player_resolve, "spirit bootstrap refresh");
        return true;
    }

    if (g_player_resolve.spirit_entry >= kMinimumPointerAddress ||
        (g_player_resolve.health_entry >= kMinimumPointerAddress &&
         g_player_resolve.health_entry != resolved_from_spirit.health_entry)) {
        return false;
    }

    AssignTrackedPlayerResolveLocked(resolved_from_spirit);
    NotifyResourceHudBindingLocked();
    PublishAcceptedPlayerResourceSnapshot(g_player_resolve, "spirit bootstrap");
    ResetTrackedDamageParticipantsLocked();
    return true;
}

bool TryPromotePlayerCombatResourcesFromStaminaEntry(const uintptr_t stamina_entry, const char* const reason) {
    if (stamina_entry < kMinimumPointerAddress) {
        return false;
    }

    ActorResolveSnapshot resolved{};
    if (!TryResolvePlayerCombatBlockFromResourceEntry(stamina_entry, true, &resolved) ||
        resolved.stamina_entry != stamina_entry ||
        resolved.health_entry < kMinimumPointerAddress) {
        return false;
    }

    std::lock_guard state_lock(g_state_mutex);
    const ActorResolveSnapshot existing = g_player_resolve;
    DiscoveredPlayerResourcePair existing_pair{};
    const bool existing_pair_is_valid =
        TryReadDiscoveredPlayerResourcePair(existing.stamina_entry, &existing_pair) &&
        existing_pair.spirit_entry == existing.spirit_entry;
    DiscoveredPlayerResourcePair candidate_pair{};
    const bool candidate_pair_is_valid =
        TryReadDiscoveredPlayerResourcePair(resolved.stamina_entry, &candidate_pair) &&
        candidate_pair.spirit_entry == resolved.spirit_entry;
    const bool candidate_matches_active_pair =
        existing.stamina_entry == resolved.stamina_entry &&
        existing.spirit_entry == resolved.spirit_entry;
    if (!psm::routing::ShouldAllowOwnerlessPlayerResourcePromotion(
            existing_pair_is_valid,
            candidate_matches_active_pair)) {
        const auto current = g_discovery_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 64) {
            Log("runtime: rejected ownerless stamina-pair replacement reason=%s accepted_stamina=0x%p accepted_spirit=0x%p candidate_stamina=0x%p candidate_spirit=0x%p",
                reason != nullptr ? reason : "unknown",
                reinterpret_cast<void*>(existing.stamina_entry),
                reinterpret_cast<void*>(existing.spirit_entry),
                reinterpret_cast<void*>(resolved.stamina_entry),
                reinterpret_cast<void*>(resolved.spirit_entry));
        }
        return false;
    }
    if (!psm::routing::ShouldReplaceAcceptedPlayerResourcePair(
            existing_pair_is_valid,
            existing.stamina_entry,
            existing.spirit_entry,
            candidate_pair_is_valid,
            resolved.stamina_entry,
            resolved.spirit_entry)) {
        const auto current = g_discovery_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 64) {
            Log("runtime: rejected legacy player resource replacement reason=%s accepted_stamina=0x%p accepted_spirit=0x%p candidate_stamina=0x%p candidate_spirit=0x%p candidate_stamina_type=%d candidate_spirit_type=%d",
                reason != nullptr ? reason : "unknown",
                reinterpret_cast<void*>(existing.stamina_entry),
                reinterpret_cast<void*>(existing.spirit_entry),
                reinterpret_cast<void*>(resolved.stamina_entry),
                reinterpret_cast<void*>(resolved.spirit_entry),
                resolved.stamina_type,
                resolved.spirit_type);
        }
        return false;
    }
    if (existing.health_entry == resolved.health_entry &&
        existing.stamina_entry == resolved.stamina_entry &&
        existing.spirit_entry == resolved.spirit_entry) {
        g_player_resolve.stamina_type = resolved.stamina_type;
        g_player_resolve.spirit_type = resolved.spirit_type;
        g_player_resolve.stamina_offset_from_health = resolved.stamina_offset_from_health;
        g_player_resolve.spirit_offset_from_health = resolved.spirit_offset_from_health;
        PublishAcceptedPlayerResourceSnapshot(g_player_resolve, reason);
        return true;
    }

    AssignTrackedPlayerResolveLocked(resolved);
    NotifyResourceHudBindingLocked();
    PublishAcceptedPlayerResourceSnapshot(g_player_resolve, reason);
    ResetTrackedMountLocked();
    ResetTrackedDamageParticipantsLocked();
    const auto current = g_discovery_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 32) {
        Log("runtime: promoted active player resources from stamina reason=%s health=0x%p stamina=0x%p spirit=0x%p stamina_type=%d spirit_type=%d stamina_offset=0x%llX spirit_offset=0x%llX previous_health=0x%p previous_stamina=0x%p previous_spirit=0x%p",
            reason != nullptr ? reason : "unknown",
            reinterpret_cast<void*>(g_player_resolve.health_entry),
            reinterpret_cast<void*>(g_player_resolve.stamina_entry),
            reinterpret_cast<void*>(g_player_resolve.spirit_entry),
            g_player_resolve.stamina_type,
            g_player_resolve.spirit_type,
            static_cast<unsigned long long>(g_player_resolve.stamina_offset_from_health),
            static_cast<unsigned long long>(g_player_resolve.spirit_offset_from_health),
            reinterpret_cast<void*>(existing.health_entry),
            reinterpret_cast<void*>(existing.stamina_entry),
            reinterpret_cast<void*>(existing.spirit_entry));
    }
    return true;
}

bool TryPromotePlayerCombatResourcesFromHealthEntry(const uintptr_t health_entry, const char* const reason) {
    if (health_entry < kMinimumPointerAddress) {
        return false;
    }

    ActorResolveSnapshot resolved{};
    if (!TryResolvePlayerCombatBlockFromHealth(health_entry, &resolved) ||
        resolved.health_entry != health_entry) {
        return false;
    }

    std::lock_guard state_lock(g_state_mutex);
    const ActorResolveSnapshot existing = g_player_resolve;
    if (existing.health_entry == resolved.health_entry &&
        existing.stamina_entry == resolved.stamina_entry &&
        existing.spirit_entry == resolved.spirit_entry) {
        g_player_resolve.stamina_type = resolved.stamina_type;
        g_player_resolve.spirit_type = resolved.spirit_type;
        g_player_resolve.stamina_offset_from_health = resolved.stamina_offset_from_health;
        g_player_resolve.spirit_offset_from_health = resolved.spirit_offset_from_health;
        PublishAcceptedPlayerResourceSnapshot(g_player_resolve, reason);
        return true;
    }

    AssignTrackedPlayerResolveLocked(resolved);
    NotifyResourceHudBindingLocked();
    PublishAcceptedPlayerResourceSnapshot(g_player_resolve, reason);
    ResetTrackedMountLocked();
    ResetTrackedDamageParticipantsLocked();
    const auto current = g_discovery_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 32) {
        Log("runtime: promoted active player resources from health reason=%s health=0x%p stamina=0x%p spirit=0x%p previous_health=0x%p previous_stamina=0x%p previous_spirit=0x%p",
            reason != nullptr ? reason : "unknown",
            reinterpret_cast<void*>(g_player_resolve.health_entry),
            reinterpret_cast<void*>(g_player_resolve.stamina_entry),
            reinterpret_cast<void*>(g_player_resolve.spirit_entry),
            reinterpret_cast<void*>(existing.health_entry),
            reinterpret_cast<void*>(existing.stamina_entry),
            reinterpret_cast<void*>(existing.spirit_entry));
    }
    return true;
}

bool TryPromotePlayerCombatResourcesFromHealthDamageTarget(const uintptr_t health_entry,
                                                           const uintptr_t damage_target,
                                                           const char* const reason) {
    if (health_entry < kMinimumPointerAddress || damage_target < kMinimumPointerAddress) {
        return false;
    }

    ActorResolveSnapshot resolved{};
    if (TryResolveActorResolveFromRoot(damage_target, &resolved)) {
        if (resolved.health_entry != health_entry) {
            resolved = {};
        }
    }

    if (resolved.health_entry != health_entry &&
        (!TryResolvePlayerCombatBlockFromHealth(health_entry, &resolved) ||
         resolved.health_entry != health_entry)) {
        return false;
    }

    resolved.damage_target = damage_target;

    std::lock_guard state_lock(g_state_mutex);
    const ActorResolveSnapshot existing = g_player_resolve;
    if (existing.health_entry == resolved.health_entry &&
        existing.stamina_entry == resolved.stamina_entry &&
        existing.spirit_entry == resolved.spirit_entry &&
        existing.damage_target == resolved.damage_target) {
        PublishAcceptedPlayerResourceSnapshot(g_player_resolve, reason);
        return true;
    }

    AssignTrackedPlayerResolveLocked(resolved);
    NotifyResourceHudBindingLocked();
    PublishAcceptedPlayerResourceSnapshot(g_player_resolve, reason);
    if (g_mount_resolve.root == resolved.root ||
        g_mount_resolve.damage_target == resolved.damage_target ||
        g_mount_resolve.health_entry == resolved.health_entry ||
        g_mount_resolve.stamina_entry == resolved.stamina_entry ||
        g_mount_resolve.spirit_entry == resolved.spirit_entry) {
        ResetTrackedMountLocked();
    }
    ResetTrackedDamageParticipantsLocked();

    const auto current = g_discovery_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 48) {
        Log("runtime: promoted active player resources from health damage target reason=%s target=0x%p health=0x%p stamina=0x%p spirit=0x%p previous_target=0x%p previous_health=0x%p previous_stamina=0x%p previous_spirit=0x%p",
            reason != nullptr ? reason : "unknown",
            reinterpret_cast<void*>(g_player_resolve.damage_target),
            reinterpret_cast<void*>(g_player_resolve.health_entry),
            reinterpret_cast<void*>(g_player_resolve.stamina_entry),
            reinterpret_cast<void*>(g_player_resolve.spirit_entry),
            reinterpret_cast<void*>(existing.damage_target),
            reinterpret_cast<void*>(existing.health_entry),
            reinterpret_cast<void*>(existing.stamina_entry),
            reinterpret_cast<void*>(existing.spirit_entry));
    }
    return true;
}
bool TryPromotePlayerCombatResourcesFromSpiritEntry(const uintptr_t spirit_entry, const char* const reason) {
    if (spirit_entry < kMinimumPointerAddress) {
        return false;
    }

    ActorResolveSnapshot resolved{};
    if (!TryResolvePlayerCombatBlockFromResourceEntry(spirit_entry, false, &resolved) ||
        resolved.spirit_entry != spirit_entry ||
        resolved.health_entry < kMinimumPointerAddress) {
        return false;
    }

    std::lock_guard state_lock(g_state_mutex);
    const ActorResolveSnapshot existing = g_player_resolve;
    DiscoveredPlayerResourcePair existing_pair{};
    const bool existing_pair_is_valid =
        TryReadDiscoveredPlayerResourcePair(existing.stamina_entry, &existing_pair) &&
        existing_pair.spirit_entry == existing.spirit_entry;
    const bool candidate_matches_active_pair =
        existing.stamina_entry == resolved.stamina_entry &&
        existing.spirit_entry == resolved.spirit_entry;
    if (!psm::routing::ShouldAllowOwnerlessPlayerResourcePromotion(
            existing_pair_is_valid,
            candidate_matches_active_pair)) {
        const auto current = g_discovery_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 64) {
            Log("runtime: rejected ownerless spirit-pair replacement reason=%s accepted_stamina=0x%p accepted_spirit=0x%p candidate_stamina=0x%p candidate_spirit=0x%p",
                reason != nullptr ? reason : "unknown",
                reinterpret_cast<void*>(existing.stamina_entry),
                reinterpret_cast<void*>(existing.spirit_entry),
                reinterpret_cast<void*>(resolved.stamina_entry),
                reinterpret_cast<void*>(resolved.spirit_entry));
        }
        return false;
    }
    if (existing.health_entry == resolved.health_entry &&
        existing.stamina_entry == resolved.stamina_entry &&
        existing.spirit_entry == resolved.spirit_entry) {
        g_player_resolve.stamina_type = resolved.stamina_type;
        g_player_resolve.spirit_type = resolved.spirit_type;
        g_player_resolve.stamina_offset_from_health = resolved.stamina_offset_from_health;
        g_player_resolve.spirit_offset_from_health = resolved.spirit_offset_from_health;
        PublishAcceptedPlayerResourceSnapshot(g_player_resolve, reason);
        return true;
    }

    AssignTrackedPlayerResolveLocked(resolved);
    NotifyResourceHudBindingLocked();
    PublishAcceptedPlayerResourceSnapshot(g_player_resolve, reason);
    ResetTrackedMountLocked();
    ResetTrackedDamageParticipantsLocked();
    const auto current = g_discovery_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 32) {
        Log("runtime: promoted active player resources from spirit reason=%s health=0x%p stamina=0x%p spirit=0x%p stamina_type=%d spirit_type=%d stamina_offset=0x%llX spirit_offset=0x%llX previous_health=0x%p previous_stamina=0x%p previous_spirit=0x%p",
            reason != nullptr ? reason : "unknown",
            reinterpret_cast<void*>(g_player_resolve.health_entry),
            reinterpret_cast<void*>(g_player_resolve.stamina_entry),
            reinterpret_cast<void*>(g_player_resolve.spirit_entry),
            g_player_resolve.stamina_type,
            g_player_resolve.spirit_type,
            static_cast<unsigned long long>(g_player_resolve.stamina_offset_from_health),
            static_cast<unsigned long long>(g_player_resolve.spirit_offset_from_health),
            reinterpret_cast<void*>(existing.health_entry),
            reinterpret_cast<void*>(existing.stamina_entry),
            reinterpret_cast<void*>(existing.spirit_entry));
    }
    return true;
}

bool TryPromotePlayerObservedResourceOwner(const uintptr_t entry,
                                           const uintptr_t owner_root,
                                           const int32_t stat_type,
                                           const char* const reason) {
    if (entry < kMinimumPointerAddress || owner_root < kMinimumPointerAddress) {
        return false;
    }

    int32_t actual_type = -1;
    int64_t current_value = 0;
    int64_t max_value = 0;
    if (!TryReadObservedStatEntry(entry, &actual_type, &current_value, &max_value)) {
        return false;
    }

    if (actual_type != stat_type || max_value <= 0 || current_value < 0 || current_value > max_value) {
        return false;
    }

    DiscoveredPlayerResourcePair discovered_pair{};
    psm::routing::DiscoveredPlayerResourceRole discovered_role =
        psm::routing::DiscoveredPlayerResourceRole::None;
    const bool has_discovered_pair =
        TryReadDiscoveredPlayerResourcePairForEntry(
            entry,
            &discovered_pair,
            &discovered_role);

    const bool is_player_stamina = has_discovered_pair
        ? discovered_role == psm::routing::DiscoveredPlayerResourceRole::Stamina
        : ((IsStaminaStatId(actual_type) || IsPrimarySpiritStatId(actual_type)) &&
           IsPlayerCombatStaminaMaxValue(max_value));
    const bool is_player_spirit = has_discovered_pair
        ? discovered_role == psm::routing::DiscoveredPlayerResourceRole::Spirit
        : (IsCurrentSpiritStatId(actual_type) &&
           (IsPlayerCombatSpiritMaxValue(max_value) || IsPlayerLargeSpiritMirrorMaxValue(max_value)));
    if (!is_player_stamina && !is_player_spirit) {
        return false;
    }

    ActorResolveSnapshot existing{};
    {
        std::lock_guard state_lock(g_state_mutex);
        existing = g_player_resolve;
        const bool has_authoritative_player_owner =
            existing.root >= kMinimumPointerAddress ||
            existing.damage_target >= kMinimumPointerAddress;
        const bool exact_authoritative_owner_match =
            owner_root == existing.root ||
            owner_root == existing.damage_target ||
            owner_root == existing.resource_owner;
        if (!psm::routing::ShouldAcceptObservedResourceOwnerForActivePlayer(
                has_authoritative_player_owner,
                exact_authoritative_owner_match)) {
            const auto current = g_discovery_logs.fetch_add(1, std::memory_order_acq_rel);
            if (current < 64) {
                Log("runtime: observed resource owner rejected foreign owner reason=%s owner=0x%p entry=0x%p active_root=0x%p active_damage_target=0x%p",
                    reason != nullptr ? reason : "unknown",
                    reinterpret_cast<void*>(owner_root),
                    reinterpret_cast<void*>(entry),
                    reinterpret_cast<void*>(existing.root),
                    reinterpret_cast<void*>(existing.damage_target));
            }
            return false;
        }
        const ActorResolveSnapshot mount_snapshot = g_mount_resolve;
        const bool mount_trusted =
            mount_snapshot.valid() &&
            g_mount_track_confidence.load(std::memory_order_acquire) >= 3;
        if (mount_trusted &&
            (owner_root == mount_snapshot.root ||
             owner_root == mount_snapshot.damage_target ||
             entry == mount_snapshot.health_entry ||
             entry == mount_snapshot.stamina_entry ||
             entry == mount_snapshot.spirit_entry)) {
            return false;
        }
    }

    const bool promoted = is_player_stamina
        ? TryPromotePlayerCombatResourcesFromStaminaEntry(entry, reason)
        : TryPromotePlayerCombatResourcesFromSpiritEntry(entry, reason);
    if (!promoted) {
        {
            std::lock_guard state_lock(g_state_mutex);
            if (TryPromoteObservedPlayerOwnerPartialLocked(entry,
                                                           owner_root,
                                                           is_player_stamina,
                                                           is_player_spirit,
                                                           reason)) {
                return true;
            }
        }
        const auto current = g_discovery_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 48) {
            Log("runtime: observed player owner skipped partial %s reason=%s owner=0x%p entry=0x%p max=%lld old_root=0x%p old_damage_target=0x%p",
                is_player_stamina ? "stamina" : "spirit",
                reason != nullptr ? reason : "unknown",
                reinterpret_cast<void*>(owner_root),
                reinterpret_cast<void*>(entry),
                static_cast<long long>(max_value),
                reinterpret_cast<void*>(existing.root),
                reinterpret_cast<void*>(existing.damage_target));
        }
        return false;
    }

    {
        std::lock_guard state_lock(g_state_mutex);
        const bool has_established_player_identity =
            g_player_resolve.root >= kMinimumPointerAddress ||
            g_player_resolve.damage_target >= kMinimumPointerAddress;
        const bool owner_changed = !has_established_player_identity &&
            (g_player_resolve.root != owner_root ||
             g_player_resolve.damage_target != owner_root);
        if (!has_established_player_identity) {
            AssignTrackedPlayerPointerFieldLocked(&ActorResolveSnapshot::root, owner_root);
            NotifyResourceHudBindingLocked();
            AssignTrackedPlayerPointerFieldLocked(&ActorResolveSnapshot::damage_target, owner_root);
            if (g_player_resolve.damage_source < kMinimumPointerAddress) {
                AssignTrackedPlayerPointerFieldLocked(&ActorResolveSnapshot::damage_source, owner_root);
            }
        }
        AssignTrackedPlayerPointerFieldLocked(&ActorResolveSnapshot::resource_owner, owner_root);
        NotifyResourceHudBindingLocked();
        if (has_discovered_pair) {
            PublishAcceptedPlayerResourcePair(discovered_pair, reason);
        } else {
            PublishAcceptedPlayerResourceSnapshot(g_player_resolve, reason);
        }
        if (g_mount_resolve.root == owner_root ||
            g_mount_resolve.damage_target == owner_root ||
            g_mount_resolve.health_entry == entry ||
            g_mount_resolve.stamina_entry == entry ||
            g_mount_resolve.spirit_entry == entry) {
            ResetTrackedMountLocked();
        }
        if (owner_changed) {
            ResetTrackedDamageParticipantsLocked();
        }
    }
    return true;
}
bool TryReadPointer(const uintptr_t address, uintptr_t* const value) {
    if (value == nullptr || address < kMinimumPointerAddress) {
        return false;
    }

    __try {
        *value = *reinterpret_cast<const uintptr_t*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool TryResolveActorResolveFromMarker(const uintptr_t marker,
                                      ActorResolveSnapshot* const resolved,
                                      const uintptr_t actor_hint) {
    if (resolved == nullptr || marker < kMinimumPointerAddress) {
        return false;
    }

    uintptr_t root = 0;
    if (!TryReadPointer(marker + 0x18, &root) || root < kMinimumPointerAddress) {
        return false;
    }

    uintptr_t root_marker = 0;
    if (!TryReadPointer(root, &root_marker) || root_marker != marker) {
        return false;
    }

    uintptr_t health_entry = 0;
    if (TryReadPointer(root + 0x58, &health_entry) && !IsValidStatEntry(health_entry, kHealthId)) {
        health_entry = 0;
    }

    uintptr_t stamina_entry = 0;
    if (health_entry >= kMinimumPointerAddress) {
        const uintptr_t candidate_stamina_entry = health_entry + kStaminaEntryOffsetFromHealth;
        if (IsValidStaminaEntry(candidate_stamina_entry)) {
            stamina_entry = candidate_stamina_entry;
        } else {
            const uintptr_t legacy_candidate_stamina_entry = health_entry + kLegacyStaminaEntryOffsetFromHealth;
            if (IsValidStaminaEntry(legacy_candidate_stamina_entry)) {
                stamina_entry = legacy_candidate_stamina_entry;
            }
        }
    }

    if (health_entry < kMinimumPointerAddress || stamina_entry < kMinimumPointerAddress) {
        TryResolveLargeMountStatEntries(root, &health_entry, &stamina_entry);
    }

    if (health_entry < kMinimumPointerAddress && stamina_entry >= kMinimumPointerAddress) {
        const uintptr_t candidate_health_entry = stamina_entry - kStaminaEntryOffsetFromHealth;
        if (IsValidStatEntry(candidate_health_entry, kHealthId)) {
            health_entry = candidate_health_entry;
        } else {
            const uintptr_t legacy_candidate_health_entry = stamina_entry - kLegacyStaminaEntryOffsetFromHealth;
            if (IsValidStatEntry(legacy_candidate_health_entry, kHealthId)) {
                health_entry = legacy_candidate_health_entry;
            }
        }
    }

    if (stamina_entry < kMinimumPointerAddress && health_entry >= kMinimumPointerAddress) {
        const uintptr_t candidate_stamina_entry = health_entry + kStaminaEntryOffsetFromHealth;
        if (IsValidStaminaEntry(candidate_stamina_entry)) {
            stamina_entry = candidate_stamina_entry;
        } else {
            const uintptr_t legacy_candidate_stamina_entry = health_entry + kLegacyStaminaEntryOffsetFromHealth;
            if (IsValidStaminaEntry(legacy_candidate_stamina_entry)) {
                stamina_entry = legacy_candidate_stamina_entry;
            }
        }
    }

    if (!IsValidStatEntry(health_entry, kHealthId) ||
        !IsValidStaminaEntry(stamina_entry) ||
        !HasExpectedHealthStaminaLayout(health_entry, stamina_entry)) {
        return false;
    }

    const uintptr_t spirit_entry = TryResolvePlayerSpiritEntryFromHealth(health_entry);

    uintptr_t resolved_actor = 0;
    if (actor_hint >= kMinimumPointerAddress) {
        uintptr_t hint_marker = 0;
        if (TryReadPointer(actor_hint + 0x20, &hint_marker) && hint_marker == marker) {
            resolved_actor = actor_hint;
        }
    }

    if (resolved_actor < kMinimumPointerAddress && !TryResolveActorFromMarker(marker, &resolved_actor)) {
        return false;
    }

    resolved->actor = resolved_actor;
    resolved->marker = marker;
    resolved->root = root;
    resolved->resource_owner = root;
    resolved->health_entry = health_entry;
    resolved->stamina_entry = stamina_entry;
    resolved->spirit_entry = spirit_entry;
    resolved->damage_source = resolved_actor;
    resolved->damage_target = root;
    return true;
}

bool TryResolveActorResolveFromActor(const uintptr_t actor, ActorResolveSnapshot* const resolved) {
    if (resolved == nullptr || actor < kMinimumPointerAddress) {
        return false;
    }

    uintptr_t marker = 0;
    if (!TryReadPointer(actor + 0x20, &marker) || marker < kMinimumPointerAddress) {
        return false;
    }

    return TryResolveActorResolveFromMarker(marker, resolved, actor);
}

bool TryResolveActorResolveFromRoot(const uintptr_t root, ActorResolveSnapshot* const resolved) {
    if (resolved == nullptr || root < kMinimumPointerAddress) {
        return false;
    }

    uintptr_t marker = 0;
    if (!TryReadPointer(root, &marker) || marker < kMinimumPointerAddress) {
        return false;
    }

    return TryResolveActorResolveFromMarker(marker, resolved);
}

bool TryResolveActorResolveFromContextRoot(const uintptr_t context_root,
                                          ActorResolveSnapshot* const resolved,
                                          const ActorResolveSnapshot& player_snapshot) {
    if (!TryResolveActorResolveFromRoot(context_root, resolved)) {
        return false;
    }

    if (!resolved->valid()) {
        return false;
    }

    if (player_snapshot.valid() &&
        (resolved->marker == player_snapshot.marker || resolved->root == player_snapshot.root)) {
        return false;
    }

    return true;
}



namespace psm::combat_health {
bool BindPrimary(ActorResolveSnapshot& player, uintptr_t observed_entry) {
    if (player.health_entry >= kMinimumPointerAddress) return false;
    NativePlayerHealthBank bank{};
    if (!ReadPrimaryHealthBank(player.actor, player.marker, player.root, &bank) ||
        (observed_entry && observed_entry != bank.health)) return false;
    player.health_entry = bank.health;
    Log("combat-health: bound active Health actor=0x%p owner=0x%p entry=0x%p uid=%u category=%u",
        reinterpret_cast<void*>(bank.actor), reinterpret_cast<void*>(bank.owner),
        reinterpret_cast<void*>(bank.health), bank.uid, unsigned(bank.category));
    return true;
}

void SetServerActorVtable(uintptr_t vtable) {
    std::lock_guard lock(g_combat_mirror_mutex);
    g_combat_mirror.Store({});
    g_combat_server_actor_vtable.store(vtable, std::memory_order_release);
}

void ObserveServerActor(uintptr_t actor) {
    if (!IsNativeCombatServerActor(actor)) return;
    psm::authority::Token token{};
    if (!psm::authority::Read(&token) || !token.generation) return;
    psm::AtomicWords<8>::Words existing{};
    uintptr_t live_health = 0;
    if (g_combat_mirror.Load(&existing) && MatchesPrimary(existing, token) &&
        existing[ServerActor] == actor &&
        ReadCombatField(existing[ServerOwner], 0x58, &live_health) &&
        existing[ServerHealth] == live_health) return;

    std::unique_lock lock(g_combat_mirror_mutex, std::try_to_lock);
    if (!lock.owns_lock() || !IsNativeCombatServerActor(actor)) return;
    NativePlayerHealthBank player{}, server{}, again{};
    psm::authority::Token after{};
    if (!ReadPrimaryHealthBank(token.actor, token.marker, token.root, &player, false) ||
        actor == player.actor || !ReadNativePlayerHealthBank(actor, &server) ||
        server.uid != player.uid || server.category != player.category ||
        server.owner == player.owner || server.health == player.health ||
        !ReadNativePlayerHealthBank(actor, &again) || again != server ||
        !psm::authority::Read(&after) || after != token) return;
    g_combat_mirror.Store({token.generation, token.actor, token.marker, token.root,
                          server.actor, server.marker, server.owner, server.health});
    Log("combat-health: bound server Health actor=0x%p owner=0x%p entry=0x%p uid=%u generation=%llu",
        reinterpret_cast<void*>(server.actor), reinterpret_cast<void*>(server.owner),
        reinterpret_cast<void*>(server.health), server.uid,
        static_cast<unsigned long long>(token.generation));
}

uintptr_t HealthEntry(uintptr_t owner, uintptr_t entry) {
    psm::authority::Token token{}, after{};
    if (!psm::authority::Read(&token) || !token.generation) return 0;
    psm::AtomicWords<8>::Words mirror{};
    const bool candidate = g_combat_mirror.Load(&mirror) && MatchesPrimary(mirror, token) &&
        (!owner || owner == mirror[ServerOwner]) && (!entry || entry == mirror[ServerHealth]) &&
        IsNativeCombatServerActor(mirror[ServerActor]);
    if (!candidate && owner && owner != token.root) return 0;

    NativePlayerHealthBank player{}, server{};
    if (!ReadPrimaryHealthBank(token.actor, token.marker, token.root, &player, false)) return 0;
    if (candidate && ReadNativePlayerHealthBank(mirror[ServerActor], &server) &&
        server.uid == player.uid && server.category == player.category &&
        server.marker == mirror[ServerMarker] &&
        server.owner == mirror[ServerOwner] && server.owner != player.owner &&
        server.health == mirror[ServerHealth] && server.health != player.health &&
        psm::authority::Read(&after) && after == token) {
        return server.health;
    }
    return (!owner || player.owner == owner) && (!entry || player.health == entry) &&
           psm::authority::Read(&after) && after == token ? player.health : 0;
}
} // namespace psm::combat_health
