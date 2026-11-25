#pragma once

#include "runtime/runtime_state.h"

#include "config.h"

bool SelectConfig(const ModConfig& config, int32_t stat_type, StatConfig* selected);
int64_t ClampToRange(int64_t value, int64_t minimum, int64_t maximum);
int64_t ScaleDelta(int64_t delta, double multiplier);
bool IsTrackedStat(int32_t stat_type);
bool TryAssignPlayerResolvedEntry(uintptr_t entry, int32_t stat_type);
bool TryBootstrapPlayerResolveFromStatComponent(uintptr_t entry, uintptr_t component);
bool TryBootstrapPlayerResolveFromHealthWrite(uintptr_t health_entry);
bool TryBootstrapPlayerStaminaFromEntry(uintptr_t stamina_entry);
bool TryBootstrapPlayerSpiritFromEntry(uintptr_t spirit_entry);
bool TryResolvePlayerResourceSelectorsFromBase(uintptr_t stamina_entry,
                                               std::uint16_t* stamina_selector,
                                               std::uint16_t* spirit_selector);
bool TryAdoptExplicitPlayerResourcePair(uintptr_t observed_entry,
                                        std::uint16_t explicit_selector,
                                        int64_t observed_delta,
                                        const char* reason);
bool TryPromotePlayerCombatResourcesFromHealthEntry(uintptr_t health_entry, const char* reason);
bool TryPromotePlayerCombatResourcesFromHealthDamageTarget(uintptr_t health_entry,
                                                           uintptr_t damage_target,
                                                           const char* reason);
bool TryPromotePlayerCombatResourcesFromStaminaEntry(uintptr_t stamina_entry, const char* reason);
bool TryPromotePlayerCombatResourcesFromSpiritEntry(uintptr_t spirit_entry, const char* reason);
bool TryPromotePlayerObservedResourceOwner(uintptr_t entry, uintptr_t owner_root, int32_t stat_type, const char* reason);
void ObservePlayerStaminaOwnerCandidate(uintptr_t owner_root, uintptr_t stamina_entry);
bool TryApplyObservedPlayerResourcePairForAuthoritativeOwner(
    uintptr_t owner_root,
    bool authoritative_capture,
    bool identity_changed,
    ActorResolveSnapshot* resolved);
void ObservePlayerStatusOwnerCandidate(uintptr_t owner_root,
                                       int32_t status_id,
                                       int64_t delta,
                                       uintptr_t source_context);
bool TryActivateCorrelatedPlayerOwnerCandidate(uintptr_t owner_root,
                                               uintptr_t stamina_entry,
                                               int64_t observed_delta,
                                               const char* reason);
bool TryActivateObservedPlayerOwnerCandidate(uintptr_t owner_root,
                                             const char* reason,
                                             bool allow_split_resource_owner = false);
bool MarkTrackedPlayerOwnerConfirmed(uintptr_t owner_root, const char* reason);
bool IsExactConfirmedTrackedPlayerOwner(uintptr_t owner_root);
bool IsValidatedPlayableStaminaResource(uintptr_t stamina_entry);
bool TryBindObservedSpiritToConfirmedPlayer(uintptr_t spirit_entry,
                                            uintptr_t observed_owner,
                                            int64_t observed_delta,
                                            const char* reason);
bool ObservedResourceOwnerMatchesConfirmedPlayerToken(uintptr_t observed_owner);
void ResetObservedPlayerOwnerCandidates();
bool TryReadPointer(uintptr_t address, uintptr_t* value);
bool TryResolveActorResolveFromMarker(uintptr_t marker, ActorResolveSnapshot* resolved, uintptr_t actor_hint = 0);
bool TryResolveActorResolveFromActor(uintptr_t actor, ActorResolveSnapshot* resolved);
bool TryResolveActorResolveFromRoot(uintptr_t root, ActorResolveSnapshot* resolved);
bool TryResolveActorResolveFromContextRoot(uintptr_t context_root,
                                          ActorResolveSnapshot* resolved,
                                          const ActorResolveSnapshot& player_snapshot);


// Exact native Health banks for the current player and the paired server actor.
namespace psm::combat_health {
bool BindPrimary(ActorResolveSnapshot& player, uintptr_t observed_entry = 0);
void SetServerActorVtable(uintptr_t vtable);
void ObserveServerActor(uintptr_t actor);
uintptr_t HealthEntry(uintptr_t owner = 0, uintptr_t entry = 0);
}
