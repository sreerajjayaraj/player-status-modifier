#pragma once

#include <cstdint>

namespace psm::routing {

enum class ExplicitResourceDeltaRoute : std::uint8_t {
    Unknown = 0,
    Stamina,
    Spirit,
};

enum class DiscoveredPlayerResourceRole : std::uint8_t {
    None = 0,
    Stamina,
    Spirit,
};

constexpr DiscoveredPlayerResourceRole SelectDiscoveredPlayerResourceRole(
    const uintptr_t observed_entry,
    const uintptr_t discovered_stamina_entry,
    const uintptr_t discovered_spirit_entry,
    const bool pair_is_valid) {
    if (!pair_is_valid) {
        return DiscoveredPlayerResourceRole::None;
    }
    if (observed_entry == discovered_stamina_entry) {
        return DiscoveredPlayerResourceRole::Stamina;
    }
    if (observed_entry == discovered_spirit_entry) {
        return DiscoveredPlayerResourceRole::Spirit;
    }
    return DiscoveredPlayerResourceRole::None;
}

// The shared resource-delta function receives the already-resolved stat entry.
// Exact entry identity is therefore stronger than selector IDs or value-shape
// guesses and remains valid when those IDs move in a game update.
constexpr DiscoveredPlayerResourceRole SelectAuthoritativeSharedDeltaRoute(
    const uintptr_t observed_entry,
    const uintptr_t tracked_stamina_entry,
    const uintptr_t tracked_spirit_entry) {
    if (observed_entry == 0 ||
        tracked_stamina_entry == 0 ||
        tracked_spirit_entry == 0 ||
        tracked_stamina_entry == tracked_spirit_entry) {
        return DiscoveredPlayerResourceRole::None;
    }
    if (observed_entry == tracked_stamina_entry) {
        return DiscoveredPlayerResourceRole::Stamina;
    }
    if (observed_entry == tracked_spirit_entry) {
        return DiscoveredPlayerResourceRole::Spirit;
    }
    return DiscoveredPlayerResourceRole::None;
}

constexpr double SelectAuthoritativeSharedDeltaMultiplier(
    const DiscoveredPlayerResourceRole route,
    const std::int64_t delta,
    const double stamina_consumption_multiplier,
    const double stamina_heal_multiplier,
    const double spirit_consumption_multiplier,
    const double spirit_heal_multiplier) {
    if (delta == 0) {
        return 1.0;
    }
    if (route == DiscoveredPlayerResourceRole::Stamina) {
        return delta < 0
            ? stamina_consumption_multiplier
            : stamina_heal_multiplier;
    }
    if (route == DiscoveredPlayerResourceRole::Spirit) {
        return delta < 0
            ? spirit_consumption_multiplier
            : spirit_heal_multiplier;
    }
    return 1.0;
}

constexpr bool ShouldSuppressFallbackPlayerResourceScaling(
    const bool shared_delta_hook_installed,
    const DiscoveredPlayerResourceRole route,
    const uintptr_t authoritative_entry,
    const uintptr_t fallback_entry,
    const std::uint32_t authoritative_thread_id,
    const std::uint32_t fallback_thread_id,
    const std::uint32_t elapsed_ms,
    const std::uint32_t ownership_window_ms) {
    return shared_delta_hook_installed &&
           route != DiscoveredPlayerResourceRole::None &&
           authoritative_entry != 0 &&
           authoritative_entry == fallback_entry &&
           authoritative_thread_id == fallback_thread_id &&
           elapsed_ms <= ownership_window_ms;
}

constexpr bool ShouldPublishDiscoveredPlayerResourceSelectors(
    const bool pair_is_valid,
    const bool pair_was_accepted_for_active_player) {
    return pair_is_valid && pair_was_accepted_for_active_player;
}

constexpr bool ShouldReplaceAcceptedPlayerResourcePair(
    const bool accepted_pair_is_valid,
    const uintptr_t accepted_stamina_entry,
    const uintptr_t accepted_spirit_entry,
    const bool candidate_pair_is_valid,
    const uintptr_t candidate_stamina_entry,
    const uintptr_t candidate_spirit_entry) {
    if (!accepted_pair_is_valid) {
        return true;
    }
    if (candidate_stamina_entry == accepted_stamina_entry &&
        candidate_spirit_entry == accepted_spirit_entry) {
        return true;
    }
    return candidate_pair_is_valid;
}

constexpr bool MatchesResolvedStaminaType(const std::int32_t actual_type,
                                          const std::int32_t resolved_stamina_type) {
    return resolved_stamina_type > 0 && actual_type == resolved_stamina_type;
}

constexpr bool MatchesResolvedSpiritType(const std::int32_t actual_type,
                                         const std::int32_t resolved_spirit_type,
                                         const std::int32_t legacy_spirit_type,
                                         const std::int32_t legacy_spirit_type_2) {
    const std::int32_t alternate_spirit_type = resolved_spirit_type + 1;
    return resolved_spirit_type > 0 &&
           (actual_type == resolved_spirit_type ||
            actual_type == alternate_spirit_type ||
            actual_type == legacy_spirit_type ||
            actual_type == legacy_spirit_type_2);
}

constexpr ExplicitResourceDeltaRoute SelectExplicitResourceDeltaRoute(
    const std::uint16_t selector,
    const std::uint16_t stamina_selector,
    const std::uint16_t spirit_selector) {
    if (selector == stamina_selector) {
        return ExplicitResourceDeltaRoute::Stamina;
    }
    if (selector == spirit_selector) {
        return ExplicitResourceDeltaRoute::Spirit;
    }
    return ExplicitResourceDeltaRoute::Unknown;
}

constexpr uintptr_t SelectExplicitResourceSelectorCalibrationBase(
    const uintptr_t observed_entry,
    const uintptr_t tracked_player_stamina_entry) {
    return tracked_player_stamina_entry != 0
        ? tracked_player_stamina_entry
        : observed_entry;
}

constexpr bool ShouldUseDirectTrackedSpiritEntry(
    const ExplicitResourceDeltaRoute route,
    const bool combat_spirit_shape,
    const bool entry_matches_tracked_player_spirit,
    const bool tracked_mount_resource) {
    return route == ExplicitResourceDeltaRoute::Spirit &&
           combat_spirit_shape &&
           entry_matches_tracked_player_spirit &&
           !tracked_mount_resource;
}

constexpr double SelectExplicitResourceDeltaMultiplier(
    const ExplicitResourceDeltaRoute route,
    const std::int64_t delta,
    const double stamina_consumption_multiplier,
    const double stamina_heal_multiplier,
    const double spirit_consumption_multiplier,
    const double spirit_heal_multiplier) {
    if (delta == 0) {
        return 1.0;
    }
    if (route == ExplicitResourceDeltaRoute::Stamina) {
        return delta < 0 ? stamina_consumption_multiplier : stamina_heal_multiplier;
    }
    if (route == ExplicitResourceDeltaRoute::Spirit) {
        return delta < 0 ? spirit_consumption_multiplier : spirit_heal_multiplier;
    }
    return 1.0;
}

constexpr bool ShouldUseExplicitPlayerResourceRoute(
    const ExplicitResourceDeltaRoute route,
    const bool validated_playable_resource,
    const bool currently_tracked_player_resource,
    const bool tracked_mount_resource,
    const bool exact_active_player_owner = false) {
    return route != ExplicitResourceDeltaRoute::Unknown &&
           (validated_playable_resource ||
            currently_tracked_player_resource ||
            exact_active_player_owner) &&
           !tracked_mount_resource;
}

constexpr bool ShouldPromoteObservedPlayerResourceOwner(
    const bool no_tracked_player_owner,
    const bool exact_active_player_owner,
    const bool player_resource_shape,
    const bool tracked_mount_resource) {
    return (no_tracked_player_owner || exact_active_player_owner) &&
           player_resource_shape &&
           !tracked_mount_resource;
}

constexpr bool ShouldAcceptAuthoritativePlayerDamageTarget(
    const bool target_matches_tracked_player,
    const bool exact_confirmed_player_owner) {
    return target_matches_tracked_player && exact_confirmed_player_owner;
}

constexpr bool ShouldSkipSharedCommitForExplicitDelta(
    const bool marker_active,
    const uintptr_t commit_entry,
    const uintptr_t pending_entry,
    const uintptr_t pending_alternate_entry,
    const uintptr_t commit_context,
    const uintptr_t pending_context,
    const std::uint32_t elapsed_ms,
    const std::uint32_t ownership_window_ms) {
    return marker_active &&
           (commit_entry == pending_entry || commit_entry == pending_alternate_entry) &&
           commit_context == pending_context &&
           elapsed_ms <= ownership_window_ms;
}

constexpr bool DirectStaminaOwnsObservedPlayerDelta(const bool tracked_player_entry,
                                                     const bool direct_callback_fired) {
    return tracked_player_entry && direct_callback_fired;
}

constexpr bool ShouldSkipStatWriteRescaleAfterDirectStaminaDelta(
    const bool marker_active,
    const uintptr_t pending_entry,
    const uintptr_t write_entry,
    const std::uint32_t pending_thread_id,
    const std::uint32_t write_thread_id,
    const std::uint32_t elapsed_ms,
    const std::uint32_t ownership_window_ms) {
    return marker_active &&
           pending_entry == write_entry &&
           pending_thread_id == write_thread_id &&
           elapsed_ms <= ownership_window_ms;
}

constexpr bool DirectStaminaMayPromoteObservedOwner(const bool stat_write_installed) {
    return !stat_write_installed;
}

constexpr bool ShouldPromotePlayerFromStatusResourceHandshake(
    const bool status_evidence_recent,
    const std::uint32_t distinct_nonzero_statuses,
    const bool saw_positive_delta,
    const bool saw_negative_delta,
    const bool saw_valid_source_context,
    const bool resource_owner_matches_status_target,
    const bool has_player_stamina_shape,
    const bool resource_is_tracked_mount,
    const bool would_replace_existing_player_stamina) {
    return status_evidence_recent &&
           distinct_nonzero_statuses >= 3 &&
           saw_positive_delta &&
           saw_negative_delta &&
           saw_valid_source_context &&
           resource_owner_matches_status_target &&
           has_player_stamina_shape &&
           !resource_is_tracked_mount &&
           !would_replace_existing_player_stamina;
}

constexpr bool ShouldActivateValidatedPlayerOwnerFromResourceUse(
    const bool candidate_is_validated,
    const bool resource_matches_candidate_stamina,
    const bool observed_change_is_negative,
    const bool has_player_stamina_shape,
    const bool resource_is_tracked_mount,
    const bool resource_use_is_load_bounce) {
    return candidate_is_validated &&
           resource_matches_candidate_stamina &&
           observed_change_is_negative &&
           has_player_stamina_shape &&
           !resource_is_tracked_mount &&
           !resource_use_is_load_bounce;
}

constexpr bool ShouldValidateObservedPlayerOwnerFromActiveResourceUse(
    const bool resource_matches_candidate_stamina,
    const bool observed_change_is_negative,
    const bool has_complete_player_combat_block,
    const bool resource_is_tracked_mount,
    const bool resource_use_is_load_bounce) {
    return resource_matches_candidate_stamina &&
           observed_change_is_negative &&
           has_complete_player_combat_block &&
           !resource_is_tracked_mount &&
           !resource_use_is_load_bounce;
}

constexpr bool ShouldRejectObservedSpiritCandidate(
    const bool is_validated_playable_stamina_entry,
    const bool observed_owner_matches_player = false) {
    return is_validated_playable_stamina_entry &&
           !observed_owner_matches_player;
}

constexpr bool ObservedSpiritOwnerTokenMatchesPlayer(const uintptr_t active_player_owner,
                                                      const uintptr_t observed_resource_owner) {
    constexpr uintptr_t kAllocationTokenMask = 0xFFFF;
    return active_player_owner != 0 &&
           observed_resource_owner != 0 &&
           (active_player_owner & kAllocationTokenMask) ==
               (observed_resource_owner & kAllocationTokenMask);
}

constexpr bool ShouldAcceptObservedSpiritOwner(const bool observed_owner_present,
                                               const bool owner_token_matches,
                                               const bool owner_activation_grace_active,
                                               const bool correlated_binding_active) {
    static_cast<void>(owner_activation_grace_active);
    static_cast<void>(correlated_binding_active);
    return observed_owner_present && owner_token_matches;
}

constexpr bool RequiresDirectStaminaHook(const bool general_enabled,
                                         const bool player_stamina_requested,
                                         const bool mount_stamina_requested) {
    return general_enabled && (player_stamina_requested || mount_stamina_requested);
}

constexpr bool ObservedOwnerChanged(const uintptr_t current_root,
                                    const uintptr_t current_damage_target,
                                    const uintptr_t observed_owner) {
    return current_root != observed_owner || current_damage_target != observed_owner;
}

constexpr bool ShouldAttemptDamageTargetPromotion(const bool runtime_enabled,
                                                  const bool target_valid,
                                                  const int32_t status_id,
                                                  const int64_t delta,
                                                  const bool source_context_valid) {
    return runtime_enabled &&
           target_valid &&
           status_id == 0 &&
           (delta < 0 || (delta > 0 && !source_context_valid));
}

constexpr bool IsStaminaResourceShape(const int32_t actual_type,
                                      const int64_t maximum,
                                      const int32_t stamina_type,
                                      const int32_t primary_spirit_type,
                                      const int64_t large_resource_minimum,
                                      const int64_t large_resource_maximum) {
    return actual_type == stamina_type ||
           (actual_type == primary_spirit_type &&
            maximum >= large_resource_minimum &&
            maximum <= large_resource_maximum);
}

constexpr bool IsSpiritResourceShape(const int32_t actual_type,
                                     const int64_t maximum,
                                     const int32_t stamina_type,
                                     const int32_t primary_spirit_type,
                                     const int64_t large_resource_minimum,
                                     const int64_t large_resource_maximum,
                                     const int32_t alternate_spirit_type = -1) {
    return (actual_type == primary_spirit_type ||
            actual_type == alternate_spirit_type) &&
           !IsStaminaResourceShape(actual_type,
                                   maximum,
                                   stamina_type,
                                   primary_spirit_type,
                                   large_resource_minimum,
                                   large_resource_maximum);
}

constexpr bool ShouldTraceResourceAuthorityCandidate(const int32_t actual_type,
                                                     const int64_t maximum,
                                                     const int64_t delta) {
    return actual_type >= 17 &&
           actual_type <= 24 &&
           maximum > 0 &&
           maximum <= 10000000 &&
           delta != 0;
}

// A player resource layout is identified by the relationship between two
// adjacent entries, not by version-specific numeric type IDs. The game has
// already moved this pair repeatedly, most recently to 22/23; keeping the roles shape-based
// lets a future sequential type shift remain discoverable while still
// rejecting a single entry type being assigned to both roles.
constexpr bool IsDiscoveredPlayerResourcePair(
    const int32_t stamina_type,
    const int64_t stamina_maximum,
    const int32_t spirit_type,
    const int64_t spirit_maximum,
    const int64_t stamina_minimum,
    const int64_t stamina_maximum_limit,
    const int64_t spirit_minimum,
    const int64_t spirit_maximum_limit) {
    return stamina_type > 0 &&
           spirit_type > stamina_type &&
           static_cast<std::int64_t>(spirit_type) -
                   static_cast<std::int64_t>(stamina_type) ==
               1 &&
           stamina_maximum >= stamina_minimum &&
           stamina_maximum <= stamina_maximum_limit &&
           spirit_maximum >= spirit_minimum &&
           spirit_maximum <= spirit_maximum_limit;
}

// The three playable characters share an adjacent stamina/spirit layout, but
// Damiane and Oongka have a lower stamina maximum than Kliff. Keep the type
// relationship dynamic and constrain only the observed resource shapes.
constexpr std::int64_t kSupportedPlayerStaminaMinimum = 300000;
constexpr std::int64_t kSupportedPlayerStaminaMaximum = 700000;
constexpr std::int64_t kSupportedPlayerSpiritMinimum = 100000;
constexpr std::int64_t kSupportedPlayerSpiritMaximum = 220000;

constexpr bool IsCurrentPatchPlayerResourcePair(
    const std::int32_t stamina_type,
    const std::int64_t stamina_maximum,
    const std::int32_t spirit_type,
    const std::int64_t spirit_maximum) {
    return IsDiscoveredPlayerResourcePair(
        stamina_type,
        stamina_maximum,
        spirit_type,
        spirit_maximum,
        kSupportedPlayerStaminaMinimum,
        kSupportedPlayerStaminaMaximum,
        kSupportedPlayerSpiritMinimum,
        kSupportedPlayerSpiritMaximum);
}

constexpr bool ShouldAdoptExplicitPlayerResourcePair(
    const bool pair_is_valid,
    const bool selector_matches_pair,
    const bool delta_is_nonzero,
    const bool has_authoritative_player_identity,
    const bool overlaps_trusted_mount,
    const bool pair_matches_active_player) {
    return pair_is_valid &&
           selector_matches_pair &&
           delta_is_nonzero &&
           has_authoritative_player_identity &&
           !overlaps_trusted_mount &&
           pair_matches_active_player;
}

constexpr bool ShouldPublishExplicitPlayerResourceSelectors(
    const bool pair_is_valid,
    const bool selector_matches_pair,
    const bool delta_is_nonzero,
    const bool has_authoritative_player_identity,
    const bool overlaps_trusted_mount) {
    return pair_is_valid &&
           selector_matches_pair &&
           delta_is_nonzero &&
           has_authoritative_player_identity &&
           !overlaps_trusted_mount;
}

constexpr bool ShouldAcceptObservedResourceOwnerForActivePlayer(
    const bool has_authoritative_player_owner,
    const bool exact_authoritative_owner_match) {
    return !has_authoritative_player_owner || exact_authoritative_owner_match;
}

constexpr bool ShouldCommitObservedPlayerOwnerPair(
    const bool has_authoritative_player_owner,
    const bool exact_authoritative_owner_match,
    const bool pair_is_valid) {
    return pair_is_valid &&
           ShouldAcceptObservedResourceOwnerForActivePlayer(
               has_authoritative_player_owner,
               exact_authoritative_owner_match);
}

constexpr bool ShouldBindSplitPlayerResourceOwner(
    const bool has_authoritative_player_identity,
    const bool exact_status_owner_match,
    const bool exact_resource_owner_match,
    const bool candidate_pair_is_valid,
    const bool validated_active_resource_use,
    const bool overlaps_trusted_mount) {
    return has_authoritative_player_identity &&
           candidate_pair_is_valid &&
           !overlaps_trusted_mount &&
           (exact_status_owner_match ||
            exact_resource_owner_match ||
            validated_active_resource_use);
}

constexpr bool ShouldBootstrapExactOwnerPlayerResourcePair(
    const bool has_authoritative_player_owner,
    const bool exact_authoritative_owner_match,
    const bool candidate_pair_is_valid,
    const bool overlaps_trusted_mount,
    const bool active_pair_is_valid) {
    return has_authoritative_player_owner &&
           exact_authoritative_owner_match &&
           candidate_pair_is_valid &&
           !overlaps_trusted_mount &&
           !active_pair_is_valid;
}

constexpr bool ShouldBootstrapShiftedResourcePairBeforeTypeGate(
    const bool recognized_by_active_selector,
    const bool has_authoritative_player_owner,
    const bool exact_authoritative_owner_match,
    const bool candidate_pair_is_valid,
    const bool overlaps_trusted_mount,
    const bool active_pair_is_valid) {
    return !recognized_by_active_selector &&
           ShouldBootstrapExactOwnerPlayerResourcePair(
               has_authoritative_player_owner,
               exact_authoritative_owner_match,
               candidate_pair_is_valid,
               overlaps_trusted_mount,
               active_pair_is_valid);
}

constexpr bool ShouldAllowOwnerlessPlayerResourcePromotion(
    const bool has_authoritative_player_owner,
    const bool candidate_matches_active_player) {
    return !has_authoritative_player_owner || candidate_matches_active_player;
}

constexpr bool IsPlayerDamageRoutingReady(
    const bool has_authoritative_owner,
    const bool has_authoritative_identity,
    const bool has_accepted_resource) {
    return has_authoritative_owner &&
           (has_authoritative_identity || has_accepted_resource);
}

constexpr bool IsPlausibleObservedOwnerPointer(const uintptr_t owner,
                                                const uintptr_t minimum_pointer) {
    return owner >= minimum_pointer && (owner % alignof(uintptr_t)) == 0;
}

constexpr bool ShouldSkipWatcherAdjustmentAfterAuthoritativeWrite(
    const bool same_entry,
    const bool same_value,
    const std::uint32_t elapsed_ms,
    const std::uint32_t ownership_window_ms) {
    return same_entry &&
           same_value &&
           elapsed_ms <= ownership_window_ms;
}

constexpr bool IsAuthoritativePlayerResourceConsumption(
    const std::int64_t previous_value,
    const std::int64_t final_value) {
    return final_value < previous_value;
}

constexpr bool ShouldHandleAuthoritativeSpiritDelta(const int32_t actual_type,
                                                     const int64_t maximum,
                                                     const int64_t delta,
                                                     const int32_t stamina_type,
                                                     const int32_t primary_spirit_type,
                                                     const int64_t large_resource_minimum,
                                                     const int64_t large_resource_maximum,
                                                     const int32_t alternate_spirit_type = -1) {
    return delta != 0 &&
           IsSpiritResourceShape(actual_type,
                                 maximum,
                                 stamina_type,
                                 primary_spirit_type,
                                 large_resource_minimum,
                                 large_resource_maximum,
                                 alternate_spirit_type);
}

constexpr bool IsCombatSpiritResourceShape(const int32_t actual_type,
                                           const int64_t maximum,
                                           const int32_t primary_spirit_type,
                                           const int64_t combat_spirit_minimum,
                                           const int64_t combat_spirit_maximum,
                                           const int32_t alternate_spirit_type = -1) {
    return (actual_type == primary_spirit_type ||
            actual_type == alternate_spirit_type) &&
           maximum >= combat_spirit_minimum &&
           maximum <= combat_spirit_maximum;
}

constexpr bool ShouldRouteResourceValueWriteToPlayerSpirit(
    const int32_t actual_type,
    const int64_t maximum,
    const int32_t primary_spirit_type,
    const int64_t combat_spirit_minimum,
    const int64_t combat_spirit_maximum,
    const bool has_adjacent_player_stamina,
    const bool owner_matches_player,
    const bool already_tracked_player_spirit,
    const int32_t alternate_spirit_type = -1) {
    return IsCombatSpiritResourceShape(actual_type,
                                       maximum,
                                       primary_spirit_type,
                                       combat_spirit_minimum,
                                       combat_spirit_maximum,
                                       alternate_spirit_type) &&
           (has_adjacent_player_stamina || owner_matches_player || already_tracked_player_spirit);
}

constexpr int64_t AdjustedRequestedValueFromDelta(const int64_t current_value,
                                                  const int64_t adjusted_delta,
                                                  const int64_t maximum) {
    if (maximum <= 0) {
        return current_value;
    }
    if (adjusted_delta < -current_value) {
        return 0;
    }
    if (adjusted_delta > maximum - current_value) {
        return maximum;
    }
    return current_value + adjusted_delta;
}

constexpr int64_t ResolveExplicitDeltaCommitValue(const bool matching_explicit_delta,
                                                  const int64_t current_value,
                                                  const int64_t adjusted_delta,
                                                  const int64_t game_requested_value,
                                                  const int64_t maximum) {
    // The explicit selector hook adjusts the delta before the game computes
    // its requested value. Reapplying that delta at commit time makes the
    // result depend on a later mutable current value and double-scales it.
    static_cast<void>(matching_explicit_delta);
    static_cast<void>(current_value);
    static_cast<void>(adjusted_delta);
    static_cast<void>(maximum);
    return game_requested_value;
}

constexpr bool ShouldTreatType20ResourceAsLooseMountStamina(
    const int32_t actual_type,
    const int64_t maximum,
    const int32_t primary_spirit_type,
    const int64_t combat_spirit_maximum,
    const bool owner_matches_player,
    const bool has_adjacent_player_pair,
    const bool already_tracked_player_resource,
    const int64_t mount_resource_minimum,
    const int64_t mount_resource_maximum,
    const int64_t player_stamina_minimum) {
    return actual_type == primary_spirit_type &&
           maximum > combat_spirit_maximum &&
           maximum >= mount_resource_minimum &&
           maximum <= mount_resource_maximum &&
           maximum < player_stamina_minimum &&
           !owner_matches_player &&
           !has_adjacent_player_pair &&
           !already_tracked_player_resource;
}

constexpr bool ShouldTrustPartialMountStamina(const std::uint32_t confidence,
                                              const bool has_stamina_entry) {
    return confidence >= 3 && has_stamina_entry;
}

constexpr bool ShouldRelockPartialMountStamina(const std::uint32_t confidence,
                                               const bool has_stamina_entry,
                                               const bool lock_enabled) {
    return lock_enabled && ShouldTrustPartialMountStamina(confidence, has_stamina_entry);
}

constexpr bool ShouldBindObservedSpiritToConfirmedPlayer(
    const bool has_player_owner,
    const bool has_player_stamina,
    const bool player_confirmation_is_recent,
    const bool observed_resource_is_spirit,
    const bool observed_change_is_active) {
    return has_player_owner &&
           has_player_stamina &&
           player_confirmation_is_recent &&
           observed_resource_is_spirit &&
           observed_change_is_active;
}

constexpr bool ShouldRouteLargeType20CostToPlayerSpirit(
    const int32_t actual_type,
    const int64_t maximum,
    const int64_t delta,
    const int32_t primary_spirit_type,
    const int64_t large_resource_minimum,
    const int64_t large_resource_maximum,
    const bool has_player_owner,
    const bool has_player_stamina,
    const bool is_tracked_player_stamina,
    const bool is_tracked_mount_stamina,
    const bool is_tracked_mount_spirit) {
    return actual_type == primary_spirit_type &&
           maximum >= large_resource_minimum &&
           maximum <= large_resource_maximum &&
           delta < 0 &&
           has_player_owner &&
           has_player_stamina &&
           !is_tracked_player_stamina &&
           !is_tracked_mount_stamina &&
           !is_tracked_mount_spirit;
}

constexpr bool ShouldDeferSpiritDeltaToRecentValueWrite(
    const bool value_write_hook_installed,
    const bool same_entry,
    const std::uint32_t elapsed_ms,
    const std::uint32_t ownership_window_ms) {
    return value_write_hook_installed &&
           same_entry &&
           elapsed_ms <= ownership_window_ms;
}

constexpr bool SharedResourceCommitOwnsCombatSpirit(
    const bool shared_commit_hook_installed,
    const bool combat_spirit_resource) {
    return shared_commit_hook_installed && combat_spirit_resource;
}

constexpr bool AuthoritativeResourceCommitOwnsTrackedRole(
    const bool resource_commit_hook_installed,
    const DiscoveredPlayerResourceRole role) {
    return resource_commit_hook_installed &&
           role != DiscoveredPlayerResourceRole::None;
}

constexpr bool ShouldDeferUpstreamResourceMutationToCommit(
    const bool resource_commit_hook_installed,
    const DiscoveredPlayerResourceRole role) {
    return AuthoritativeResourceCommitOwnsTrackedRole(
        resource_commit_hook_installed,
        role);
}

constexpr bool ShouldSkipConditionalResourceWriteAfterCommit(
    const bool resource_commit_hook_installed,
    const bool tracked_player_resource,
    const bool same_entry,
    const std::uint32_t elapsed_ms,
    const std::uint32_t ownership_window_ms) {
    return resource_commit_hook_installed &&
           tracked_player_resource &&
           same_entry &&
           elapsed_ms <= ownership_window_ms;
}

constexpr bool ShouldSkipConditionalSpiritWriteAfterSharedCommit(
    const bool shared_commit_hook_installed,
    const bool tracked_player_spirit,
    const bool same_entry,
    const std::uint32_t elapsed_ms,
    const std::uint32_t ownership_window_ms) {
    return ShouldSkipConditionalResourceWriteAfterCommit(
        shared_commit_hook_installed,
        tracked_player_spirit,
        same_entry,
        elapsed_ms,
        ownership_window_ms);
}

constexpr bool ShouldAcceptLearnedOutgoingSource(const bool target_is_player,
                                                 const bool learned_owner_matches_player,
                                                 const uintptr_t source_context,
                                                 const uintptr_t learned_source_context) {
    return !target_is_player &&
           learned_owner_matches_player &&
           source_context != 0 &&
           source_context == learned_source_context;
}

constexpr bool ShouldIgnorePointerFallbackForResolvedOwner(
    const bool has_player_owner,
    const bool has_player_stamina) {
    return has_player_owner && has_player_stamina;
}

constexpr bool ShouldReplaceObservedOwnerFromAuthoritativePlayerCapture(
    const bool authoritative_capture,
    const bool identity_changed) {
    return authoritative_capture && identity_changed;
}

constexpr bool ShouldUseObservedOwnerPairForAuthoritativeCapture(
    const bool authoritative_capture,
    const bool identity_changed,
    const bool observed_owner_matches_resolved_owner,
    const bool observed_pair_is_valid,
    const bool resolved_pair_is_valid) {
    return authoritative_capture &&
           identity_changed &&
           observed_owner_matches_resolved_owner &&
           observed_pair_is_valid &&
           !resolved_pair_is_valid;
}

constexpr bool ShouldAcceptRelatedOutgoingSource(const bool related_to_player,
                                                 const bool mount_marker_present) {
    static_cast<void>(mount_marker_present);
    return related_to_player;
}

constexpr bool MatchesPlayerDamageIdentity(const uintptr_t candidate,
                                           const uintptr_t actor,
                                           const uintptr_t marker,
                                           const uintptr_t root,
                                           const uintptr_t damage_target,
                                           const uintptr_t damage_source) {
    return candidate != 0 &&
           ((actor != 0 && candidate == actor) ||
            (marker != 0 && candidate == marker) ||
            (root != 0 && candidate == root) ||
            (damage_target != 0 && candidate == damage_target) ||
            (damage_source != 0 && candidate == damage_source));
}

}  // namespace psm::routing
