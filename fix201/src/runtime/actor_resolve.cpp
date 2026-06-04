#include "runtime/actor_resolve.h"

#include "logger.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

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
constexpr int64_t kPlayerCombatStaminaResolveMinMax = 400000;
constexpr int64_t kPlayerCombatStaminaResolveMaxMax = 500000;
constexpr int64_t kPlayerCombatSpiritResolveMinMax = 100000;
constexpr int64_t kPlayerCombatSpiritResolveMaxMax = 220000;

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
    constexpr uintptr_t stamina_offsets[] = {
        kStaminaEntryOffsetFromHealth,
        kLegacyStaminaEntryOffsetFromHealth,
    };
    for (const uintptr_t offset : stamina_offsets) {
        const uintptr_t candidate = health_entry + offset;
        int64_t candidate_max = 0;
        if (TryGetStaminaEntryMaxValue(candidate, &candidate_max) &&
            IsPlayerCombatStaminaMaxValue(candidate_max)) {
            stamina_entry = candidate;
            stamina_max = candidate_max;
            break;
        }
    }

    if (stamina_entry < kMinimumPointerAddress || !HasExpectedHealthStaminaLayout(health_entry, stamina_entry)) {
        return false;
    }

    uintptr_t spirit_entry = 0;
    int64_t spirit_max = 0;
    constexpr uintptr_t spirit_offsets[] = {
        kSpiritEntryOffsetFromHealth,
        kLegacySpiritEntryOffsetFromHealth,
    };
    for (const uintptr_t offset : spirit_offsets) {
        const uintptr_t candidate = health_entry + offset;
        int64_t candidate_max = 0;
        if (TryGetStatEntryMaxValue(candidate, kSpiritId, &candidate_max) &&
            IsPlayerCombatSpiritMaxValue(candidate_max)) {
            spirit_entry = candidate;
            spirit_max = candidate_max;
            break;
        }
    }

    if (spirit_entry < kMinimumPointerAddress) {
        return false;
    }

    *resolved = g_player_resolve;
    resolved->health_entry = health_entry;
    resolved->stamina_entry = stamina_entry;
    resolved->spirit_entry = spirit_entry;
    (void)stamina_max;
    (void)spirit_max;
    return true;
}

bool TryResolvePlayerSnapshotFromStaminaOffset(const uintptr_t stamina_entry,
                                               ActorResolveSnapshot* const resolved) {
    if (resolved == nullptr || stamina_entry < kMinimumPointerAddress || !IsValidStaminaEntry(stamina_entry)) {
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

        const uintptr_t health_entry = stamina_entry - offset;
        int64_t health_max = 0;
        if (!IsValidStatEntry(health_entry, kHealthId) ||
            !TryGetStatEntryMaxValue(health_entry, kHealthId, &health_max) ||
            !IsPlayerHealthFallbackMaxValue(health_max)) {
            continue;
        }

        const uintptr_t spirit_entry = TryResolvePlayerSpiritEntryFromHealth(health_entry);

        resolved->health_entry = health_entry;
        resolved->stamina_entry = stamina_entry;
        resolved->spirit_entry = spirit_entry;
        return true;
    }

    return false;
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

}  // namespace

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

bool TryAssignPlayerResolvedEntry(const uintptr_t entry, const int32_t stat_type) {
    if (entry < kMinimumPointerAddress || !IsTrackedStat(stat_type)) {
        return false;
    }

    auto resolved = g_player_resolve;
    if (!resolved.valid()) {
        return false;
    }

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
        if (resolved.stamina_entry != entry) {
            resolved.stamina_entry = entry;
            changed = true;
        }
    } else if (IsSpiritStatId(stat_type)) {
        if (resolved.spirit_entry == entry) {
            return false;
        }

        int32_t existing_type = -1;
        if (resolved.spirit_entry >= kMinimumPointerAddress &&
            TryReadStatTypeValue(resolved.spirit_entry, &existing_type)) {

            const bool existing_primary = IsPrimarySpiritStatId(existing_type);
            const bool candidate_primary = IsPrimarySpiritStatId(stat_type);
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

    g_player_resolve = resolved;
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

    std::lock_guard lock(g_state_mutex);

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

        g_player_resolve = merged;
        return true;
    }

    if (g_player_resolve.valid()) {
        return false;
    }

    g_player_resolve = resolved;
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

    // Crimson Desert 1.05.01 can route fall/environment damage through this
    // health writer before the marker/root resolver has observed the player.
    // Fix-3 was stable, but it still required the old fixed stamina offset
    // (health + 0x480), so the valid player health write from 1.05.01 was
    // rejected as "untracked health write skipped".  Keep this fallback narrow:
    // accept the verified health entry only, optionally fill the old stamina /
    // spirit offsets when they happen to match, and avoid any heap/window scan.
    uintptr_t stamina_entry = 0;
    const uintptr_t candidate_stamina_entry = health_entry + kStaminaEntryOffsetFromHealth;
    if (IsValidStaminaEntry(candidate_stamina_entry)) {
        stamina_entry = candidate_stamina_entry;
    } else {
        const uintptr_t legacy_candidate_stamina_entry = health_entry + kLegacyStaminaEntryOffsetFromHealth;
        if (IsValidStaminaEntry(legacy_candidate_stamina_entry)) {
            stamina_entry = legacy_candidate_stamina_entry;
        } else {
            stamina_entry = TryFindUniqueNearbyStatEntry(health_entry, kStaminaId);
        }
    }

    const uintptr_t spirit_entry = TryResolvePlayerSpiritEntryFromHealth(health_entry);

    std::lock_guard lock(g_state_mutex);

    if (g_player_resolve.health_entry >= kMinimumPointerAddress) {
        if (g_player_resolve.health_entry == health_entry) {
            return true;
        }

        ActorResolveSnapshot resolved_from_health{};
        if (!TryResolvePlayerCombatBlockFromHealth(health_entry, &resolved_from_health) ||
            resolved_from_health.health_entry != health_entry) {
            return false;
        }

        const ActorResolveSnapshot existing = g_player_resolve;
        resolved_from_health.actor = existing.actor;
        resolved_from_health.marker = existing.marker;
        resolved_from_health.root = existing.root;
        resolved_from_health.damage_source = existing.damage_source;
        resolved_from_health.damage_target = existing.damage_target;
        g_player_resolve = resolved_from_health;
        ResetTrackedDamageParticipantsLocked();
        return true;
    }

    g_player_resolve.health_entry = health_entry;
    g_player_resolve.stamina_entry = stamina_entry;
    g_player_resolve.spirit_entry = spirit_entry;
    ResetTrackedDamageParticipantsLocked();
    return true;
}


bool TryBootstrapPlayerStaminaFromEntry(const uintptr_t stamina_entry) {
    if (stamina_entry < kMinimumPointerAddress || !IsValidStaminaEntry(stamina_entry)) {
        return false;
    }

    int64_t stamina_max = 0;
    if (!TryGetStaminaEntryMaxValue(stamina_entry, &stamina_max)) {
        return false;
    }

    std::lock_guard lock(g_state_mutex);

    if (g_player_resolve.stamina_entry == stamina_entry) {
        return true;
    }

    if (g_player_resolve.stamina_entry >= kMinimumPointerAddress) {
        return false;
    }

    if (g_player_resolve.health_entry >= kMinimumPointerAddress) {
        if (!IsNearbyTrackedPlayerStatCandidate(stamina_entry, g_player_resolve.health_entry) ||
            stamina_entry == g_player_resolve.health_entry ||
            stamina_entry == g_player_resolve.spirit_entry) {
            return false;
        }

        // The 1.05.01 player stamina maximum may be high enough to overlap the
        // old mount threshold.  The nearby-player-health relationship is a
        // stronger discriminator than a raw stamina max cutoff, so do not reject
        // a valid adjacent stamina entry just because stamina_max is large.
        g_player_resolve.stamina_entry = stamina_entry;
        return true;
    }

    ActorResolveSnapshot resolved_from_stamina{};
    if (!TryResolvePlayerSnapshotFromStaminaOffset(stamina_entry, &resolved_from_stamina)) {
        return false;
    }

    g_player_resolve.health_entry = resolved_from_stamina.health_entry;
    g_player_resolve.stamina_entry = resolved_from_stamina.stamina_entry;
    g_player_resolve.spirit_entry = resolved_from_stamina.spirit_entry;
    ResetTrackedDamageParticipantsLocked();
    return true;
}

bool TryBootstrapPlayerSpiritFromEntry(const uintptr_t spirit_entry) {
    if (spirit_entry < kMinimumPointerAddress || !IsValidStatEntry(spirit_entry, kSpiritId)) {
        return false;
    }

    std::lock_guard lock(g_state_mutex);

    int32_t stat_type = -1;
    if (!TryReadStatTypeValue(spirit_entry, &stat_type)) {
        return false;
    }
    if (g_player_resolve.spirit_entry == spirit_entry) {
        return true;
    }

    if (g_player_resolve.health_entry < kMinimumPointerAddress ||
        !IsNearbyTrackedPlayerStatCandidate(spirit_entry, g_player_resolve.health_entry) ||
        spirit_entry == g_player_resolve.health_entry ||
        spirit_entry == g_player_resolve.stamina_entry) {
        return false;
    }

    int32_t existing_type = -1;
    if (g_player_resolve.spirit_entry >= kMinimumPointerAddress &&
        TryReadStatTypeValue(g_player_resolve.spirit_entry, &existing_type)) {

        const bool existing_primary = IsPrimarySpiritStatId(existing_type);
        const bool candidate_primary = IsPrimarySpiritStatId(stat_type);
        if (!existing_primary || !candidate_primary) {
            return false;
        }
    }

    g_player_resolve.spirit_entry = spirit_entry;
    return true;
}

bool TryPromotePlayerCombatResourcesFromStaminaEntry(const uintptr_t stamina_entry, const char* const reason) {
    if (stamina_entry < kMinimumPointerAddress) {
        return false;
    }

    constexpr uintptr_t stamina_offsets[] = {
        kStaminaEntryOffsetFromHealth,
        kLegacyStaminaEntryOffsetFromHealth,
    };

    ActorResolveSnapshot resolved{};
    for (const uintptr_t offset : stamina_offsets) {
        if (stamina_entry <= offset) {
            continue;
        }

        if (TryResolvePlayerCombatBlockFromHealth(stamina_entry - offset, &resolved) &&
            resolved.stamina_entry == stamina_entry) {
            break;
        }
        resolved = {};
    }

    if (resolved.stamina_entry != stamina_entry || resolved.health_entry < kMinimumPointerAddress) {
        return false;
    }

    std::lock_guard lock(g_state_mutex);
    const ActorResolveSnapshot existing = g_player_resolve;
    if (existing.health_entry == resolved.health_entry &&
        existing.stamina_entry == resolved.stamina_entry &&
        existing.spirit_entry == resolved.spirit_entry) {
        return true;
    }

    g_player_resolve.health_entry = resolved.health_entry;
    g_player_resolve.stamina_entry = resolved.stamina_entry;
    g_player_resolve.spirit_entry = resolved.spirit_entry;
    ResetTrackedMountLocked();
    ResetTrackedDamageParticipantsLocked();
    const auto current = g_discovery_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 32) {
        Log("runtime: promoted active player resources from stamina reason=%s health=0x%p stamina=0x%p spirit=0x%p previous_health=0x%p previous_stamina=0x%p previous_spirit=0x%p",
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

bool TryPromotePlayerCombatResourcesFromHealthEntry(const uintptr_t health_entry, const char* const reason) {
    if (health_entry < kMinimumPointerAddress) {
        return false;
    }

    ActorResolveSnapshot resolved{};
    if (!TryResolvePlayerCombatBlockFromHealth(health_entry, &resolved) ||
        resolved.health_entry != health_entry) {
        return false;
    }

    std::lock_guard lock(g_state_mutex);
    const ActorResolveSnapshot existing = g_player_resolve;
    if (existing.health_entry == resolved.health_entry &&
        existing.stamina_entry == resolved.stamina_entry &&
        existing.spirit_entry == resolved.spirit_entry) {
        return true;
    }

    g_player_resolve.health_entry = resolved.health_entry;
    g_player_resolve.stamina_entry = resolved.stamina_entry;
    g_player_resolve.spirit_entry = resolved.spirit_entry;
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

bool TryResolvePlayerCombatResourcesFromHealthEntry(const uintptr_t health_entry, ActorResolveSnapshot* const resolved) {
    if (health_entry < kMinimumPointerAddress || resolved == nullptr) {
        return false;
    }

    ActorResolveSnapshot candidate{};
    if (!TryResolvePlayerCombatBlockFromHealth(health_entry, &candidate) ||
        candidate.health_entry != health_entry) {
        return false;
    }

    *resolved = candidate;
    return true;
}

bool TryPromotePlayerCombatResourcesFromSpiritEntry(const uintptr_t spirit_entry, const char* const reason) {
    if (spirit_entry < kMinimumPointerAddress) {
        return false;
    }

    constexpr uintptr_t spirit_offsets[] = {
        kSpiritEntryOffsetFromHealth,
        kLegacySpiritEntryOffsetFromHealth,
    };

    ActorResolveSnapshot resolved{};
    for (const uintptr_t offset : spirit_offsets) {
        if (spirit_entry <= offset) {
            continue;
        }

        if (TryResolvePlayerCombatBlockFromHealth(spirit_entry - offset, &resolved) &&
            resolved.spirit_entry == spirit_entry) {
            break;
        }
        resolved = {};
    }

    if (resolved.spirit_entry != spirit_entry || resolved.health_entry < kMinimumPointerAddress) {
        return false;
    }

    std::lock_guard lock(g_state_mutex);
    const ActorResolveSnapshot existing = g_player_resolve;
    if (existing.health_entry == resolved.health_entry &&
        existing.stamina_entry == resolved.stamina_entry &&
        existing.spirit_entry == resolved.spirit_entry) {
        return true;
    }

    g_player_resolve.health_entry = resolved.health_entry;
    g_player_resolve.stamina_entry = resolved.stamina_entry;
    g_player_resolve.spirit_entry = resolved.spirit_entry;
    ResetTrackedMountLocked();
    ResetTrackedDamageParticipantsLocked();
    const auto current = g_discovery_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 32) {
        Log("runtime: promoted active player resources from spirit reason=%s health=0x%p stamina=0x%p spirit=0x%p previous_health=0x%p previous_stamina=0x%p previous_spirit=0x%p",
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

