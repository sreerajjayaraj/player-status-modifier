#include "runtime/resource_routing_policy.h"
#include "scanner.h"

#include <cstdint>
#include <cstdio>

namespace {

int g_failures = 0;

void Check(const bool condition, const char* const name) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", name);
        ++g_failures;
    }
}

}  // namespace

int main() {
    using namespace psm::routing;

    Check(ResolveRelativeBranchTarget(0x1000, 0x20) == 0x1025,
          "relative call target resolves a forward displacement");
    Check(ResolveRelativeBranchTarget(0x1000, -0x25) == 0x0FE0,
          "relative jump target resolves a backward displacement");

    Check(SelectItemGainAmount(
              ItemGainCaptureKind::LegacyAmountRcxTargetR8Rdi, 11, 22) == 22,
          "legacy item-gain contract reads amount from RCX");
    Check(SelectItemGainAmount(
              ItemGainCaptureKind::CurrentAmountRaxTargetRsiRcx, 11, 22) == 11,
          "current item-gain contract reads amount from RAX");
    Check(SelectItemGainTargetAddress(
              ItemGainCaptureKind::LegacyAmountRcxTargetR8Rdi,
              0x1000, 0x20, 0x2000, 0x40) == 0x2050,
          "legacy item-gain contract computes R8+RDI+0x10 target");
    Check(SelectItemGainTargetAddress(
              ItemGainCaptureKind::CurrentAmountRaxTargetRsiRcx,
              0x1000, 0x20, 0x2000, 0x40) == 0x1030,
          "current item-gain contract computes RSI+RCX+0x10 target");
    Check(SelectItemGainAmount(
              ItemGainCaptureKind::CurrentAmountRcxTargetRdx, 11, 22) == 22,
          "2435 item-gain contract reads amount from RCX");
    Check(SelectItemGainTargetAddress(
              ItemGainCaptureKind::CurrentAmountRcxTargetRdx,
              0x1000, 0x20, 0x2000, 0x40, 0x3000) == 0x3010,
          "2435 item-gain contract computes RDX+0x10 target");

    Check(SelectExplicitResourceDeltaRoute(19, 19, 20) ==
              ExplicitResourceDeltaRoute::Stamina,
          "wrapper selector 19 routes only to player stamina");
    Check(SelectExplicitResourceDeltaRoute(20, 19, 20) ==
              ExplicitResourceDeltaRoute::Spirit,
          "wrapper selector 20 routes only to player spirit");
    Check(SelectExplicitResourceDeltaRoute(17, 19, 20) ==
              ExplicitResourceDeltaRoute::Unknown,
          "legacy selector 17 fails closed instead of guessing a resource");
    Check(SelectExplicitResourceDeltaRoute(0, 19, 20) ==
              ExplicitResourceDeltaRoute::Unknown,
          "missing wrapper selector fails closed");
    Check(SelectExplicitResourceDeltaRoute(20, 20, 21) ==
              ExplicitResourceDeltaRoute::Stamina,
          "current shifted selector 20 routes to stamina after live calibration");
    Check(SelectExplicitResourceDeltaRoute(21, 20, 21) ==
              ExplicitResourceDeltaRoute::Spirit,
          "current shifted selector 21 routes to spirit after live calibration");
    Check(SelectExplicitResourceDeltaRoute(22, 22, 23) ==
              ExplicitResourceDeltaRoute::Stamina,
          "the 1.0.0.2443 wrapper selector 22 routes to player stamina");
    Check(SelectExplicitResourceDeltaRoute(23, 22, 23) ==
              ExplicitResourceDeltaRoute::Spirit,
          "the 1.0.0.2443 wrapper selector 23 routes to player spirit");
    Check(SelectExplicitResourceDeltaRoute(24, 22, 23) ==
              ExplicitResourceDeltaRoute::Unknown,
          "an unrecognized 1.0.0.2443 wrapper selector fails closed");
    Check(SelectExplicitResourceDeltaRoute(27, 27, 28) ==
              ExplicitResourceDeltaRoute::Stamina,
          "future calibrated stamina selector does not require a source rebuild");
    Check(SelectExplicitResourceDeltaRoute(28, 27, 28) ==
              ExplicitResourceDeltaRoute::Spirit,
          "future calibrated spirit selector does not require a source rebuild");
    Check(MatchesResolvedStaminaType(22, 22),
          "the live 1.0.0.2264 stamina type matches its calibrated selector");
    Check(!MatchesResolvedStaminaType(22, 19),
          "a shifted stamina type is rejected before calibration");
    Check(MatchesResolvedSpiritType(23, 23, 17, 18),
          "the live 1.0.0.2264 spirit type matches its calibrated selector");
    Check(MatchesResolvedSpiritType(24, 23, 17, 18),
          "the alternate live spirit mirror follows the calibrated selector");
    Check(MatchesResolvedSpiritType(17, 23, 17, 18),
          "a validated legacy visible-spirit mirror remains recognized");
    Check(!MatchesResolvedSpiritType(22, 23, 17, 18),
          "the calibrated stamina type cannot be mistaken for spirit");
    Check(SelectExplicitResourceSelectorCalibrationBase(0x1090, 0x1000) == 0x1000,
          "a directly selected spirit entry calibrates selectors from tracked player stamina");
    Check(SelectExplicitResourceSelectorCalibrationBase(0x7000, 0x1000) == 0x1000,
          "a transient resource entry cannot replace the active player selector base");
    Check(SelectExplicitResourceSelectorCalibrationBase(0x7000, 0) == 0x7000,
          "selector calibration falls back to the observed entry before player tracking exists");
    Check(ShouldUseDirectTrackedSpiritEntry(
              ExplicitResourceDeltaRoute::Spirit, true, true, false),
          "the directly selected tracked combat-spirit entry owns its explicit delta");
    Check(!ShouldUseDirectTrackedSpiritEntry(
              ExplicitResourceDeltaRoute::Stamina, true, true, false),
          "a stamina selector cannot claim the direct spirit route");
    Check(!ShouldUseDirectTrackedSpiritEntry(
              ExplicitResourceDeltaRoute::Spirit, false, true, false),
          "the direct spirit route still requires a combat-spirit value shape");
    Check(!ShouldUseDirectTrackedSpiritEntry(
              ExplicitResourceDeltaRoute::Spirit, true, false, false),
          "an untracked transient spirit-shaped entry fails closed");
    Check(!ShouldUseDirectTrackedSpiritEntry(
              ExplicitResourceDeltaRoute::Spirit, true, true, true),
          "a tracked mount resource cannot use the direct player-spirit route");
    Check(SelectExplicitResourceDeltaMultiplier(
              ExplicitResourceDeltaRoute::Stamina, -100, 0.25, 4.0, 0.75, 10.0) == 0.25,
          "explicit stamina cost uses the stamina consumption multiplier");
    Check(SelectExplicitResourceDeltaMultiplier(
              ExplicitResourceDeltaRoute::Stamina, 100, 0.25, 4.0, 0.75, 10.0) == 4.0,
          "explicit stamina recovery uses the stamina heal multiplier");
    Check(SelectExplicitResourceDeltaMultiplier(
              ExplicitResourceDeltaRoute::Spirit, -100, 0.25, 4.0, 0.75, 10.0) == 0.75,
          "explicit spirit cost uses the spirit consumption multiplier");
    Check(SelectExplicitResourceDeltaMultiplier(
              ExplicitResourceDeltaRoute::Spirit, 100, 0.25, 4.0, 0.75, 10.0) == 10.0,
          "explicit spirit recovery uses the spirit heal multiplier");
    Check(SelectExplicitResourceDeltaMultiplier(
              ExplicitResourceDeltaRoute::Unknown, -100, 0.25, 4.0, 0.75, 10.0) == 1.0,
          "an unknown selector preserves the game's delta");
    Check(ShouldUseExplicitPlayerResourceRoute(
              ExplicitResourceDeltaRoute::Spirit, true, false, false),
          "a validated playable resource may use the explicit spirit route");
    Check(ShouldUseExplicitPlayerResourceRoute(
              ExplicitResourceDeltaRoute::Spirit, false, true, false),
          "the currently tracked player resource may use the explicit spirit route");
    Check(!ShouldUseExplicitPlayerResourceRoute(
              ExplicitResourceDeltaRoute::Unknown, true, true, false),
          "an unknown selector cannot claim a player resource");
    Check(!ShouldUseExplicitPlayerResourceRoute(
              ExplicitResourceDeltaRoute::Spirit, false, false, false),
          "an unvalidated unrelated resource cannot use the explicit spirit route");
    Check(!ShouldUseExplicitPlayerResourceRoute(
              ExplicitResourceDeltaRoute::Spirit, true, true, true),
          "a tracked mount resource cannot use the explicit player route");
    Check(ShouldUseExplicitPlayerResourceRoute(
              ExplicitResourceDeltaRoute::Spirit, false, false, false, true),
          "an exact authoritative player owner may bind an untracked spirit route");
    Check(!ShouldUseExplicitPlayerResourceRoute(
              ExplicitResourceDeltaRoute::Spirit, false, false, false, false),
          "a resource from a different owner cannot bind the active player");

    Check(ShouldPromoteObservedPlayerResourceOwner(true, false, true, false),
          "an observed player resource may establish the first owner profile");
    Check(ShouldPromoteObservedPlayerResourceOwner(false, true, true, false),
          "an exact current-player owner may repopulate resources after a character switch");
    Check(!ShouldPromoteObservedPlayerResourceOwner(false, false, true, false),
          "an unrelated owner cannot replace an authoritative current player");
    Check(!ShouldPromoteObservedPlayerResourceOwner(false, true, false, false),
          "an exact owner still requires a player-shaped resource");
    Check(!ShouldPromoteObservedPlayerResourceOwner(false, true, true, true),
          "a tracked mount resource cannot populate player resources");

    Check(ShouldAcceptAuthoritativePlayerDamageTarget(true, true),
          "incoming damage to the confirmed current-player root is ready before resource discovery");
    Check(!ShouldAcceptAuthoritativePlayerDamageTarget(true, false),
          "an unconfirmed partial owner cannot bypass resource discovery");
    Check(!ShouldAcceptAuthoritativePlayerDamageTarget(false, true),
          "a different damage target is not authorized by the current-player capture");

    Check(ShouldSkipSharedCommitForExplicitDelta(
              true, 0x1000, 0x1000, 0x1090, 0x2000, 0x2000, 5, 100),
          "the matching synchronous commit is already owned by the explicit delta route");
    Check(ShouldSkipSharedCommitForExplicitDelta(
              true, 0x1090, 0x1000, 0x1090, 0x2000, 0x2000, 5, 100),
          "the compact spirit sibling commit is owned by the explicit delta route");
    Check(!ShouldSkipSharedCommitForExplicitDelta(
              true, 0x1000, 0x1000, 0x1090, 0x2001, 0x2000, 5, 100),
          "a different commit context is not suppressed");
    Check(!ShouldSkipSharedCommitForExplicitDelta(
              true, 0x1000, 0x1000, 0x1090, 0x2000, 0x2000, 101, 100),
          "an expired explicit-delta marker cannot suppress a later commit");
    Check(!ShouldSkipSharedCommitForExplicitDelta(
              true, 0x1180, 0x1000, 0x1090, 0x2000, 0x2000, 5, 100),
          "an unrelated resource commit is not suppressed");

    Check(DirectStaminaOwnsObservedPlayerDelta(true, true),
          "an earlier stat-write pulse cannot permanently suppress a later direct stamina cost");
    Check(DirectStaminaOwnsObservedPlayerDelta(true, true),
          "direct stamina owns a tracked callback with no exact-entry authority");
    Check(!DirectStaminaOwnsObservedPlayerDelta(false, true),
          "direct stamina rejects an untracked entry");
    Check(!DirectStaminaOwnsObservedPlayerDelta(true, false),
          "direct stamina requires the callback to have fired");

    Check(ShouldSkipStatWriteRescaleAfterDirectStaminaDelta(
              true, 0x1000, 0x1000, 7, 7, 2, 10),
          "a synchronous write after a direct delta cannot be scaled twice");
    Check(!ShouldSkipStatWriteRescaleAfterDirectStaminaDelta(
              true, 0x1000, 0x1000, 7, 8, 2, 10),
          "a different thread cannot consume the direct-delta marker");
    Check(!ShouldSkipStatWriteRescaleAfterDirectStaminaDelta(
              true, 0x1000, 0x1000, 7, 7, 11, 10),
          "an expired direct-delta marker cannot suppress a later write");

    Check(!DirectStaminaMayPromoteObservedOwner(true),
          "authoritative stat-write blocks AB00 owner promotion");
    Check(DirectStaminaMayPromoteObservedOwner(false),
          "AB00 may recover owner tracking only without stat-write");

    Check(ShouldPromotePlayerFromStatusResourceHandshake(
              true, 5, true, true, true, true, true, false, false),
          "correlated load status burst and stamina-shaped resource identify the active player");
    Check(!ShouldPromotePlayerFromStatusResourceHandshake(
              true, 0, false, false, false, true, true, false, false),
          "AB00 owner alone cannot promote a player while stat-write is installed");
    Check(!ShouldPromotePlayerFromStatusResourceHandshake(
              true, 5, true, true, true, false, true, false, false),
          "status evidence from another owner cannot claim the resource");
    Check(!ShouldPromotePlayerFromStatusResourceHandshake(
              false, 5, true, true, true, true, true, false, false),
          "stale load status evidence cannot claim a later resource");
    Check(!ShouldPromotePlayerFromStatusResourceHandshake(
              true, 2, true, true, true, true, true, false, false),
          "a periodic two-status update is not a player load signature");
    Check(!ShouldPromotePlayerFromStatusResourceHandshake(
              true, 5, true, true, true, true, false, false, false),
          "status evidence without a player stamina-shaped resource is insufficient");
    Check(!ShouldPromotePlayerFromStatusResourceHandshake(
              true, 5, true, true, true, true, true, true, false),
          "a tracked mount resource cannot become the player through the handshake");
    Check(!ShouldPromotePlayerFromStatusResourceHandshake(
              true, 5, true, true, true, true, true, false, true),
          "a later large spirit mirror cannot replace established player stamina");

    Check(ShouldActivateValidatedPlayerOwnerFromResourceUse(
              true, true, true, true, false, false),
          "a validated playable owner may become active from its own stamina cost");
    Check(!ShouldActivateValidatedPlayerOwnerFromResourceUse(
              false, true, true, true, false, false),
          "an unvalidated owner cannot claim the active player from AB00 alone");
    Check(!ShouldActivateValidatedPlayerOwnerFromResourceUse(
              true, false, true, true, false, false),
          "a resource belonging to another owner cannot activate this candidate");
    Check(!ShouldActivateValidatedPlayerOwnerFromResourceUse(
              true, true, false, true, false, false),
          "a positive load fill cannot switch the active player");
    Check(!ShouldActivateValidatedPlayerOwnerFromResourceUse(
              true, true, true, false, false, false),
          "a non-stamina-shaped resource cannot activate a playable owner");
    Check(!ShouldActivateValidatedPlayerOwnerFromResourceUse(
              true, true, true, true, true, false),
          "a tracked mount resource cannot activate a playable owner");
    Check(!ShouldActivateValidatedPlayerOwnerFromResourceUse(
              true, true, true, true, false, true),
          "a paired load-time resource reversal cannot steal the active player");

    Check(ShouldValidateObservedPlayerOwnerFromActiveResourceUse(
              true, true, true, false, false),
          "a real stamina cost from a complete playable resource block validates a switched character");
    Check(!ShouldValidateObservedPlayerOwnerFromActiveResourceUse(
              false, true, true, false, false),
          "a resource that does not belong to the observed owner cannot validate it");
    Check(!ShouldValidateObservedPlayerOwnerFromActiveResourceUse(
              true, false, true, false, false),
          "a positive fill does not identify the actively controlled character");
    Check(!ShouldValidateObservedPlayerOwnerFromActiveResourceUse(
              true, true, false, false, false),
          "an incomplete player resource block cannot replace the active owner");
    Check(!ShouldValidateObservedPlayerOwnerFromActiveResourceUse(
              true, true, true, true, false),
          "a tracked mount resource cannot validate a player owner");
    Check(!ShouldValidateObservedPlayerOwnerFromActiveResourceUse(
              true, true, true, false, true),
          "a paired load-time resource reversal cannot validate a resident inactive character");

    Check(ShouldRejectObservedSpiritCandidate(true),
          "a validated playable stamina entry cannot be rebound as spirit");
    Check(!ShouldRejectObservedSpiritCandidate(false),
          "an unrelated large resource remains eligible for spirit routing");
    Check(!ShouldRejectObservedSpiritCandidate(true, true),
          "a correlated switched-character spirit owner may override a temporary stamina label");

    Check(RequiresDirectStaminaHook(true, true, false),
          "player stamina configuration requires the direct hook");
    Check(RequiresDirectStaminaHook(true, false, true),
          "mount stamina configuration requires the direct hook");
    Check(!RequiresDirectStaminaHook(false, true, true),
          "disabled general configuration installs no direct hook");

    Check(ObservedOwnerChanged(0x1000, 0x1000, 0x2000),
          "new observed owner resets stale character state");
    Check(!ObservedOwnerChanged(0x2000, 0x2000, 0x2000),
          "same observed owner preserves current character state");

    Check(ShouldAttemptDamageTargetPromotion(true, true, 0, 100, false),
          "null-source environment health event attempts player promotion");
    Check(ShouldAttemptDamageTargetPromotion(true, true, 0, -100, true),
          "signed incoming health loss attempts player promotion");
    Check(!ShouldAttemptDamageTargetPromotion(true, true, 0, 100, true),
          "positive passive heal with a valid source cannot replace the active player");
    Check(!ShouldAttemptDamageTargetPromotion(false, true, 0, 100, false),
          "disabled runtime rejects target promotion");
    Check(!ShouldAttemptDamageTargetPromotion(true, false, 0, 100, false),
          "invalid target rejects promotion");
    Check(!ShouldAttemptDamageTargetPromotion(true, true, 0, 0, false),
          "zero delta rejects promotion");

    Check(SelectStatWriteObservedValue(StatWriteValueRegister::Rbx,
                                       440000,
                                       57446,
                                       57446) == 440000,
          "1.0.0.1980 stat-write contract selects authoritative rbx value");

    Check(IsStaminaResourceShape(20, 440000, 19, 20, 400000, 700000),
          "type 20 large resource uses stamina configuration");
    Check(IsStaminaResourceShape(20, 615000, 19, 20, 400000, 700000),
          "type 20 expanded large resource uses stamina configuration");
    Check(!IsStaminaResourceShape(20, 100000, 19, 20, 400000, 700000),
          "type 20 combat-size resource is not stamina");
    Check(IsSpiritResourceShape(20, 100000, 19, 20, 400000, 700000),
          "type 20 combat-size resource uses spirit configuration");
    Check(IsSpiritResourceShape(21, 170000, 19, 20, 400000, 700000, 21),
          "type 21 compact switched-character resource uses spirit configuration");

    Check(ShouldTraceResourceAuthorityCandidate(21, 170000, -9800),
          "current type 21 spirit cost is included in authority tracing");
    Check(ShouldTraceResourceAuthorityCandidate(21, 170000, 1600),
          "current type 21 spirit recovery is included in authority tracing");
    Check(ShouldTraceResourceAuthorityCandidate(22, 170000, -9800),
          "future shifted combat resource remains visible to authority tracing");
    Check(!ShouldTraceResourceAuthorityCandidate(21, 170000, 0),
          "unchanged type 21 resource does not create trace noise");
    Check(!ShouldTraceResourceAuthorityCandidate(16, 170000, -9800),
          "unrelated legacy resource type is excluded from authority tracing");
    Check(!ShouldTraceResourceAuthorityCandidate(21, 0, -9800),
          "resource without a valid maximum is excluded from authority tracing");
    Check(!ShouldTraceResourceAuthorityCandidate(21, 10000001, -9800),
          "implausibly large resource is excluded from authority tracing");

    Check(IsDiscoveredPlayerResourcePair(19, 440000, 20, 170000,
                                         400000, 700000, 100000, 220000),
          "legacy 19/20 adjacent resource pair is discoverable without fixed role IDs");
    Check(IsDiscoveredPlayerResourcePair(20, 440000, 21, 170000,
                                         400000, 700000, 100000, 220000),
          "current 20/21 adjacent resource pair is discoverable without fixed role IDs");
    Check(IsDiscoveredPlayerResourcePair(22, 440000, 23, 170000,
                                         400000, 700000, 100000, 220000),
          "1.0.0.2330 type 22/23 adjacent resource pair is discoverable");
    Check(IsCurrentPatchPlayerResourcePair(22, 360000, 23, 130000),
          "Damiane and Oongka 360000/130000 resource pairs are discoverable");
    Check(IsCurrentPatchPlayerResourcePair(22, 440000, 23, 170000),
          "Kliff 440000/170000 resource pair remains discoverable");
    Check(!IsCurrentPatchPlayerResourcePair(22, 360000, 24, 130000),
          "current-patch discovery still rejects non-adjacent resource types");
    Check(IsDiscoveredPlayerResourcePair(27, 440000, 28, 170000,
                                         400000, 700000, 100000, 220000),
          "future adjacent type shifts remain discoverable by validated resource shape");
    Check(!IsDiscoveredPlayerResourcePair(2, 440000, 18, 170000,
                                          400000, 700000, 100000, 220000),
          "unrelated non-sequential 2/18 resources cannot replace the player pair");
    Check(!IsDiscoveredPlayerResourcePair(22, 440000, 24, 170000,
                                          400000, 700000, 100000, 220000),
          "a gapped resource pair cannot replace the sequential player pair");
    Check(!IsDiscoveredPlayerResourcePair(20, 440000, 20, 170000,
                                          400000, 700000, 100000, 220000),
          "a single type cannot claim both stamina and spirit roles");
    Check(!IsDiscoveredPlayerResourcePair(20, 170000, 21, 440000,
                                          400000, 700000, 100000, 220000),
          "reversed resource shapes are rejected");
    Check(ShouldAdoptExplicitPlayerResourcePair(true, true, true, true, false, true),
          "the explicit game selector may retain the already active sequential pair");
    Check(!ShouldAdoptExplicitPlayerResourcePair(true, true, true, true, false, false),
          "an explicit selector from another loaded character cannot replace the active pair");
    Check(!ShouldAdoptExplicitPlayerResourcePair(true, false, true, true, false, true),
          "a selector outside the discovered pair cannot adopt it");
    Check(!ShouldAdoptExplicitPlayerResourcePair(true, true, false, true, false, true),
          "a zero delta cannot adopt a transient resource pair");
    Check(!ShouldAdoptExplicitPlayerResourcePair(true, true, true, false, false, true),
          "resource shape alone cannot create an active player identity");
    Check(!ShouldAdoptExplicitPlayerResourcePair(true, true, true, true, true, true),
          "a trusted mount resource cannot replace the player pair");
    Check(ShouldPublishExplicitPlayerResourceSelectors(true, true, true, true, false),
          "a valid explicit contract may publish live selector IDs without taking ownership");
    Check(!ShouldPublishExplicitPlayerResourceSelectors(true, false, true, true, false),
          "a mismatched selector cannot calibrate live resource IDs");
    Check(ShouldAcceptObservedResourceOwnerForActivePlayer(false, false),
          "the first owner-shaped resource may establish an unowned player profile");
    Check(ShouldAcceptObservedResourceOwnerForActivePlayer(true, true),
          "an exact authoritative owner may refresh its own resource pair");
    Check(!ShouldAcceptObservedResourceOwnerForActivePlayer(true, false),
          "a foreign loaded character cannot replace an authoritative player pair");
    Check(ShouldCommitObservedPlayerOwnerPair(false, false, true),
          "the first validated owner may establish its adjacent resource pair");
    Check(ShouldCommitObservedPlayerOwnerPair(true, true, true),
          "an exact authoritative owner may refresh its adjacent resource pair");
    Check(!ShouldCommitObservedPlayerOwnerPair(true, false, true),
          "a foreign resource event cannot splice its pair into the active player identity");
    Check(!ShouldCommitObservedPlayerOwnerPair(false, false, false),
          "an invalid adjacent resource pair cannot establish player ownership");
    Check(ShouldBindSplitPlayerResourceOwner(
              true, false, false, true, true, false),
          "an active validated pair may bind a resource owner separate from the status root");
    Check(ShouldBindSplitPlayerResourceOwner(
              true, false, true, true, false, false),
          "an already bound split resource owner may refresh its validated pair");
    Check(!ShouldBindSplitPlayerResourceOwner(
              false, false, false, true, true, false),
          "resource activity cannot invent a player identity before authoritative capture");
    Check(!ShouldBindSplitPlayerResourceOwner(
              true, false, false, false, true, false),
          "an invalid adjacent pair cannot bind a split resource owner");
    Check(!ShouldBindSplitPlayerResourceOwner(
              true, false, false, true, false, false),
          "a foreign resource owner cannot bind without validated active use");
    Check(!ShouldBindSplitPlayerResourceOwner(
              true, false, false, true, true, true),
          "a trusted mount pair cannot bind as a split player resource owner");
    Check(ShouldBootstrapShiftedResourcePairBeforeTypeGate(
              false, true, true, true, false, false),
          "a valid shifted 22/23 pair must bootstrap before the stale startup-ID gate");
    Check(!ShouldBootstrapShiftedResourcePairBeforeTypeGate(
              true, true, true, true, false, false),
          "an entry already recognized by the active selector does not need pre-gate bootstrap");
    Check(!ShouldBootstrapShiftedResourcePairBeforeTypeGate(
              false, true, false, true, false, false),
          "foreign owner residue cannot bootstrap a shifted player resource pair");
    Check(!ShouldBootstrapShiftedResourcePairBeforeTypeGate(
              false, true, true, true, true, false),
          "a shifted pair overlapping a trusted mount must fail closed");
    Check(!ShouldBootstrapShiftedResourcePairBeforeTypeGate(
              false, true, true, false, false, false),
          "an invalid adjacent resource shape cannot bootstrap before the type gate");
    Check(!ShouldBootstrapShiftedResourcePairBeforeTypeGate(
              false, true, true, true, false, true),
          "a shifted pair cannot replace an already active player pair");
    Check(ShouldBootstrapExactOwnerPlayerResourcePair(
              true, true, true, false, false),
          "an exact authoritative owner may bootstrap its 22/23 pair while active pointers are empty");
    Check(!ShouldBootstrapExactOwnerPlayerResourcePair(
              true, false, true, false, false),
          "a foreign loaded character cannot bootstrap over the authoritative owner");
    Check(!ShouldBootstrapExactOwnerPlayerResourcePair(
              true, true, true, true, false),
          "a trusted mount pair cannot bootstrap as the authoritative player");
    Check(!ShouldBootstrapExactOwnerPlayerResourcePair(
              true, true, true, false, true),
          "an exact owner bootstrap does not replace an already valid active pair");
    Check(!ShouldAllowOwnerlessPlayerResourcePromotion(true, false),
          "an ownerless stat write cannot replace an authoritative player pair");
    Check(ShouldAllowOwnerlessPlayerResourcePromotion(false, false),
          "ownerless discovery remains available before authoritative identity exists");
    Check(ShouldAllowOwnerlessPlayerResourcePromotion(true, true),
          "an ownerless write may refresh the already active player pair");
    Check(IsPlayerDamageRoutingReady(true, true, false),
          "authoritative player owner plus identity enables damage routing before a resource action");
    Check(IsPlayerDamageRoutingReady(true, false, true),
          "authoritative player owner plus accepted resource enables damage routing");
    Check(!IsPlayerDamageRoutingReady(false, true, true),
          "player damage routing still requires an authoritative owner");
    Check(!IsPlayerDamageRoutingReady(true, false, false),
          "player owner alone is insufficient without identity or resources");
    Check(SelectDiscoveredPlayerResourceRole(
              0x1000, 0x1000, 0x1090, true) ==
              DiscoveredPlayerResourceRole::Stamina,
          "the first entry in an accepted adjacent pair is stamina");
    Check(SelectDiscoveredPlayerResourceRole(
              0x1090, 0x1000, 0x1090, true) ==
              DiscoveredPlayerResourceRole::Spirit,
          "the second entry in an accepted adjacent pair is spirit");
    Check(SelectDiscoveredPlayerResourceRole(
              0x1000, 0x1000, 0x1090, false) ==
              DiscoveredPlayerResourceRole::None,
          "an unaccepted shape probe cannot assign a player resource role");
    Check(SelectAuthoritativeSharedDeltaRoute(
              0x1000, 0x1000, 0x1090) ==
              DiscoveredPlayerResourceRole::Stamina,
          "the shared delta function owns the exact tracked stamina entry");
    Check(SelectAuthoritativeSharedDeltaRoute(
              0x1090, 0x1000, 0x1090) ==
              DiscoveredPlayerResourceRole::Spirit,
          "the shared delta function owns the exact tracked spirit entry");
    Check(SelectAuthoritativeSharedDeltaRoute(
              0x1120, 0x1000, 0x1090) ==
              DiscoveredPlayerResourceRole::None,
          "an adjacent but untracked resource is rejected by shared delta authority");
    Check(SelectAuthoritativeSharedDeltaRoute(
              0x1000, 0x1000, 0x1000) ==
              DiscoveredPlayerResourceRole::None,
          "an ambiguous tracked resource pair fails closed");
    Check(SelectAuthoritativeSharedDeltaMultiplier(
              DiscoveredPlayerResourceRole::Stamina,
              -100,
              0.25,
              4.0,
              0.75,
              10.0) == 0.25,
          "authoritative stamina costs use only the stamina consumption multiplier");
    Check(SelectAuthoritativeSharedDeltaMultiplier(
              DiscoveredPlayerResourceRole::Stamina,
              100,
              0.25,
              4.0,
              0.75,
              10.0) == 4.0,
          "authoritative stamina recovery uses only the stamina heal multiplier");
    Check(SelectAuthoritativeSharedDeltaMultiplier(
              DiscoveredPlayerResourceRole::Spirit,
              -100,
              0.25,
              4.0,
              0.75,
              10.0) == 0.75,
          "authoritative spirit costs use only the spirit consumption multiplier");
    Check(SelectAuthoritativeSharedDeltaMultiplier(
              DiscoveredPlayerResourceRole::Spirit,
              100,
              0.25,
              4.0,
              0.75,
              10.0) == 10.0,
          "authoritative spirit recovery uses only the spirit heal multiplier");
    Check(SelectAuthoritativeSharedDeltaMultiplier(
              DiscoveredPlayerResourceRole::None,
              -100,
              0.25,
              4.0,
              0.75,
              10.0) == 1.0,
          "an unowned shared delta preserves the game value");
    Check(ShouldSuppressFallbackPlayerResourceScaling(
              true,
              DiscoveredPlayerResourceRole::Stamina,
              0x1000,
              0x1000,
              42,
              42,
              2,
              10),
          "a correlated shared stamina update suppresses downstream fallback scaling");
    Check(ShouldSuppressFallbackPlayerResourceScaling(
              true,
              DiscoveredPlayerResourceRole::Spirit,
              0x1090,
              0x1090,
              42,
              42,
              2,
              10),
          "a correlated shared spirit update suppresses downstream fallback scaling");
    Check(!ShouldSuppressFallbackPlayerResourceScaling(
               true,
               DiscoveredPlayerResourceRole::Stamina,
               0x1000,
               0x1090,
               42,
               42,
               2,
               10),
          "shared stamina authority never suppresses another resource entry");
    Check(!ShouldSuppressFallbackPlayerResourceScaling(
               true,
               DiscoveredPlayerResourceRole::Stamina,
               0x1000,
               0x1000,
               42,
               43,
               2,
               10),
          "shared resource authority never crosses threads");
    Check(!ShouldSuppressFallbackPlayerResourceScaling(
               true,
               DiscoveredPlayerResourceRole::Stamina,
               0x1000,
               0x1000,
               42,
               42,
               11,
               10),
          "an expired shared resource update does not suppress a later direct write");
    Check(!ShouldPublishDiscoveredPlayerResourceSelectors(true, false),
          "shape discovery alone cannot replace the active player selectors");
    Check(ShouldPublishDiscoveredPlayerResourceSelectors(true, true),
          "an accepted active-player pair publishes its live selectors");
    Check(IsPlausibleObservedOwnerPointer(0x000002105A0E03C0ull, 0x10000000ull),
          "a canonical game heap pointer is eligible as an observed owner");
    Check(!IsPlausibleObservedOwnerPointer(0x17097ull, 0x10000000ull),
          "a small selector-like shared-commit value cannot become a player owner");

    Check(ShouldSkipWatcherAdjustmentAfterAuthoritativeWrite(
              true, true, 100, 350),
          "watcher yields to an exact authoritative write observed within its ownership window");
    Check(!ShouldSkipWatcherAdjustmentAfterAuthoritativeWrite(
               false, true, 100, 350),
          "watcher does not yield when the authoritative write belongs to another entry");
    Check(!ShouldSkipWatcherAdjustmentAfterAuthoritativeWrite(
               true, false, 100, 350),
          "watcher does not yield when the observed value differs from the authoritative result");
    Check(!ShouldSkipWatcherAdjustmentAfterAuthoritativeWrite(
               true, true, 351, 350),
          "watcher resumes ownership after the authoritative write window expires");

    Check(ShouldHandleAuthoritativeSpiritDelta(20, 170000, -9800, 19, 20, 400000, 700000),
          "authoritative delta hook accepts a real combat spirit cost");
    Check(ShouldHandleAuthoritativeSpiritDelta(
              21, 170000, -9800, 19, 20, 400000, 700000, 21),
          "authoritative delta hook accepts the switched-character type 21 spirit cost");
    Check(ShouldHandleAuthoritativeSpiritDelta(20, 170000, 1600, 19, 20, 400000, 700000),
          "authoritative delta hook accepts combat spirit recovery");
    Check(!ShouldHandleAuthoritativeSpiritDelta(20, 440000, -9800, 19, 20, 400000, 700000),
          "authoritative spirit delta hook rejects the large stamina mirror");
    Check(!ShouldHandleAuthoritativeSpiritDelta(20, 170000, 0, 19, 20, 400000, 700000),
          "authoritative spirit delta hook rejects zero deltas");

    Check(IsCombatSpiritResourceShape(20, 170000, 20, 100000, 220000),
          "combat spirit shape accepts the current player spirit range");
    Check(IsCombatSpiritResourceShape(21, 170000, 20, 100000, 220000, 21),
          "combat spirit shape accepts the current switched-character type 21 resource");
    Check(!IsCombatSpiritResourceShape(21, 390000, 20, 100000, 220000, 21),
          "type 21 large resources are not accepted as switched-character spirit");
    Check(!IsCombatSpiritResourceShape(20, 390000, 20, 100000, 220000),
          "combat spirit shape rejects Damiane horse stamina observed in the playtest");
    Check(ShouldRouteResourceValueWriteToPlayerSpirit(
              20, 170000, 20, 100000, 220000, true, false, false),
          "adjacent player stamina profile authorizes a player spirit value write");
    Check(ShouldRouteResourceValueWriteToPlayerSpirit(
              21, 170000, 20, 100000, 220000, true, false, false, 21),
          "adjacent player stamina profile authorizes a type 21 spirit value write");
    Check(ShouldRouteResourceValueWriteToPlayerSpirit(
              20, 170000, 20, 100000, 220000, false, true, false),
          "tracked player owner authorizes a player spirit value write");
    Check(!ShouldRouteResourceValueWriteToPlayerSpirit(
              20, 170000, 20, 100000, 220000, false, false, false),
          "unowned combat-size resource is not promoted as player spirit");
    Check(AdjustedRequestedValueFromDelta(120000, -980, 170000) == 119020,
          "adjusted spirit cost is converted back to an absolute requested value");
    Check(AdjustedRequestedValueFromDelta(169000, 20000, 170000) == 170000,
          "adjusted spirit recovery clamps to the resource maximum");
    Check(ResolveExplicitDeltaCommitValue(
              true, 169490, -510, 169490, 170000) == 169490,
          "a matching explicit Spirit commit preserves the value produced by the already-scaled call site");
    Check(ResolveExplicitDeltaCommitValue(
              true, 200, -510, 200, 170000) == 200,
          "a matching explicit Spirit commit is not recomputed from a later mutable current value");
    Check(ResolveExplicitDeltaCommitValue(
              false, 169490, -510, 170000, 170000) == 170000,
          "an unrelated recovery commit keeps the game's requested value");
    Check(IsAuthoritativePlayerResourceConsumption(170000, 169798),
          "an explicit spirit commit below its prior value records consumption ownership");
    Check(!IsAuthoritativePlayerResourceConsumption(169798, 170000),
          "an explicit spirit recovery commit is not mislabeled as consumption");

    Check(!ShouldReplaceAcceptedPlayerResourcePair(
              true, 0x1000, 0x1090,
              false, 0x3000, 0x2EF0),
          "a legacy non-adjacent resource snapshot cannot replace the accepted active pair");
    Check(ShouldReplaceAcceptedPlayerResourcePair(
              true, 0x1000, 0x1090,
              true, 0x3000, 0x3090),
          "a newly validated adjacent resource pair may replace the prior character pair");
    Check(ShouldReplaceAcceptedPlayerResourcePair(
              false, 0, 0,
              false, 0x3000, 0x2EF0),
          "legacy discovery remains available before an adjacent player pair is accepted");

    Check(ShouldTreatType20ResourceAsLooseMountStamina(
              20, 390000, 20, 220000, false, false, false, 180000, 10000000, 400000),
          "Damiane horse type 20 resource is eligible for loose mount tracking");
    Check(!ShouldTreatType20ResourceAsLooseMountStamina(
              20, 170000, 20, 220000, false, false, false, 180000, 10000000, 400000),
          "player combat spirit is never treated as loose mount stamina");
    Check(!ShouldTreatType20ResourceAsLooseMountStamina(
              20, 440000, 20, 220000, true, true, true, 180000, 10000000, 400000),
          "tracked player stamina mirror is never treated as loose mount stamina");
    Check(!ShouldTreatType20ResourceAsLooseMountStamina(
              20, 440000, 20, 220000, false, false, false, 180000, 10000000, 400000),
          "unowned type 20 player stamina is never inferred to be a mount from maximum alone");

    Check(ShouldTrustPartialMountStamina(3, true),
          "a three-hit loose mount stamina entry is trusted without a complete actor snapshot");
    Check(!ShouldTrustPartialMountStamina(2, true),
          "a loose mount stamina entry below the confidence threshold is rejected");
    Check(!ShouldTrustPartialMountStamina(3, false),
          "mount confidence cannot authorize a missing stamina entry");
    Check(ShouldRelockPartialMountStamina(3, true, true),
          "a trusted partial mount profile is periodically relocked when its lock is enabled");
    Check(!ShouldRelockPartialMountStamina(3, true, false),
          "a trusted partial mount profile obeys the disabled stamina-lock setting");
    Check(!ShouldRelockPartialMountStamina(2, true, true),
          "periodic relock rejects a low-confidence partial mount profile");

    Check(ShouldBindObservedSpiritToConfirmedPlayer(true, true, true, true, true),
          "an active combat spirit cost binds to the recently confirmed player profile");
    Check(!ShouldBindObservedSpiritToConfirmedPlayer(true, true, false, true, true),
          "a stale active-player confirmation cannot claim an unowned spirit resource");
    Check(!ShouldBindObservedSpiritToConfirmedPlayer(true, false, true, true, true),
          "spirit binding requires an active player stamina profile");
    Check(!ShouldBindObservedSpiritToConfirmedPlayer(true, true, true, false, true),
          "a non-spirit-shaped resource cannot bind as player spirit");
    Check(!ShouldBindObservedSpiritToConfirmedPlayer(true, true, true, true, false),
          "an inactive recovery write cannot replace the active player spirit resource");

    Check(ObservedSpiritOwnerTokenMatchesPlayer(
              0x000002F4B91EA280ull,
              0x000002F4B8C8A280ull),
          "the switched player and its spirit component share the allocation token");
    Check(!ObservedSpiritOwnerTokenMatchesPlayer(
              0x000002F4B91EA280ull,
              0x000002F4B8C83AC0ull),
          "a different character resource owner cannot claim the switched player");
    Check(ShouldAcceptObservedSpiritOwner(true, true, true, false),
          "a correlated spirit owner is accepted during the character-switch grace window");
    Check(!ShouldAcceptObservedSpiritOwner(true, false, true, false),
          "an unrelated spirit mirror is rejected during the character-switch grace window");
    Check(!ShouldAcceptObservedSpiritOwner(true, false, false, true),
          "an unrelated spirit mirror cannot replace an established correlated binding");
    Check(!ShouldAcceptObservedSpiritOwner(true, false, false, false),
          "an unrelated owner is rejected even after the grace window");
    Check(!ShouldAcceptObservedSpiritOwner(false, false, false, false),
          "an ownerless observation cannot establish a new spirit binding");

    Check(ShouldRouteLargeType20CostToPlayerSpirit(
              20, 440000, -10000, 20, 400000, 700000,
              true, true, false, false, false),
          "a negative 440000 type 20 cost may select the active character spirit resource");
    Check(ShouldRouteLargeType20CostToPlayerSpirit(
              20, 615000, -10000, 20, 400000, 700000,
              true, true, false, false, false),
          "a negative 615000 type 20 cost may select the active character spirit resource");
    Check(!ShouldRouteLargeType20CostToPlayerSpirit(
              20, 440000, 10000, 20, 400000, 700000,
              true, true, false, false, false),
          "a positive recovery write cannot steal the active character spirit binding");
    Check(!ShouldRouteLargeType20CostToPlayerSpirit(
              20, 440000, -10000, 20, 400000, 700000,
              true, true, true, false, false),
          "the tracked player stamina entry cannot be rebound as spirit");
    Check(!ShouldRouteLargeType20CostToPlayerSpirit(
              20, 440000, -10000, 20, 400000, 700000,
              true, true, false, true, false),
          "tracked mount stamina cannot be rebound as player spirit");
    Check(!ShouldRouteLargeType20CostToPlayerSpirit(
              20, 440000, -10000, 20, 400000, 700000,
              true, true, false, false, true),
          "tracked mount spirit cannot be rebound as player spirit");
    Check(!ShouldRouteLargeType20CostToPlayerSpirit(
              20, 440000, -10000, 20, 400000, 700000,
              false, true, false, false, false),
          "a large type 20 cost requires a confirmed player owner");
    Check(!ShouldRouteLargeType20CostToPlayerSpirit(
              20, 440000, -10000, 20, 400000, 700000,
              true, false, false, false, false),
          "a large type 20 cost requires a readable player stamina profile");

    Check(ShouldDeferSpiritDeltaToRecentValueWrite(true, true, 40, 250),
          "a recent value-write callback for the same spirit entry owns the update");
    Check(!ShouldDeferSpiritDeltaToRecentValueWrite(true, true, 251, 250),
          "an installed but dormant value-write hook cannot suppress the real spirit delta path");
    Check(!ShouldDeferSpiritDeltaToRecentValueWrite(true, false, 40, 250),
          "a value-write callback for another resource cannot suppress this spirit delta");
    Check(!ShouldDeferSpiritDeltaToRecentValueWrite(false, true, 40, 250),
          "an absent value-write hook never owns spirit delta processing");

    Check(SharedResourceCommitOwnsCombatSpirit(true, true),
          "the shared commit hook owns combat spirit mutations");
    Check(!SharedResourceCommitOwnsCombatSpirit(false, true),
          "an unavailable shared commit hook cannot own spirit mutations");
    Check(!SharedResourceCommitOwnsCombatSpirit(true, false),
          "the shared commit hook does not claim non-spirit resources");

    Check(AuthoritativeResourceCommitOwnsTrackedRole(
              true, DiscoveredPlayerResourceRole::Stamina),
          "the final resource commit owns tracked player stamina");
    Check(AuthoritativeResourceCommitOwnsTrackedRole(
              true, DiscoveredPlayerResourceRole::Spirit),
          "the final resource commit owns tracked player spirit");
    Check(!AuthoritativeResourceCommitOwnsTrackedRole(
               false, DiscoveredPlayerResourceRole::Stamina),
          "an unavailable final commit hook cannot claim player stamina");
    Check(!AuthoritativeResourceCommitOwnsTrackedRole(
               true, DiscoveredPlayerResourceRole::None),
          "the final commit hook does not claim an untracked resource");

    Check(ShouldDeferUpstreamResourceMutationToCommit(
              true, DiscoveredPlayerResourceRole::Stamina),
          "the upstream stamina delta yields to the final commit");
    Check(ShouldDeferUpstreamResourceMutationToCommit(
              true, DiscoveredPlayerResourceRole::Spirit),
          "the upstream spirit delta yields to the final commit");
    Check(!ShouldDeferUpstreamResourceMutationToCommit(
               false, DiscoveredPlayerResourceRole::Stamina),
          "the upstream fallback remains available without a commit hook");

    Check(ShouldSkipConditionalSpiritWriteAfterSharedCommit(
              true, true, true, 40, 250),
          "a recent shared commit prevents duplicate conditional spirit scaling");
    Check(!ShouldSkipConditionalSpiritWriteAfterSharedCommit(
              true, false, true, 40, 250),
          "a non-spirit stat write remains owned by the conditional hook");
    Check(!ShouldSkipConditionalSpiritWriteAfterSharedCommit(
              true, true, false, 40, 250),
          "a different spirit entry remains eligible for conditional scaling");
    Check(!ShouldSkipConditionalSpiritWriteAfterSharedCommit(
              true, true, true, 251, 250),
          "an expired shared commit does not suppress a later spirit write");

    Check(ShouldAcceptLearnedOutgoingSource(false, true, 0x4000, 0x4000),
          "a learned active-character source context authorizes outgoing damage");
    Check(!ShouldAcceptLearnedOutgoingSource(true, true, 0x4000, 0x4000),
          "the learned source context never reclassifies damage targeting the player as outgoing");
    Check(!ShouldAcceptLearnedOutgoingSource(false, false, 0x4000, 0x4000),
          "a source context learned for a previous player owner is rejected");
    Check(!ShouldAcceptLearnedOutgoingSource(false, true, 0x5000, 0x4000),
          "a different source context is not authorized by the learned player source");

    Check(ShouldIgnorePointerFallbackForResolvedOwner(true, true),
          "a confirmed owner and stamina profile outrank an unrelated pointer fallback");
    Check(!ShouldIgnorePointerFallbackForResolvedOwner(true, false),
          "an owner without a resource profile can still use pointer fallback recovery");

    Check(ShouldReplaceObservedOwnerFromAuthoritativePlayerCapture(true, true),
          "a changed authoritative player capture replaces a stale observed owner");
    Check(!ShouldReplaceObservedOwnerFromAuthoritativePlayerCapture(false, true),
          "a legacy pointer fallback cannot replace a resolved owner");
    Check(!ShouldReplaceObservedOwnerFromAuthoritativePlayerCapture(true, false),
          "an unchanged authoritative capture does not reset tracked resources");

    Check(ShouldUseObservedOwnerPairForAuthoritativeCapture(
              true, true, true, true, false),
          "a changed authoritative player owner adopts its cached adjacent resource pair instead of a legacy resolver pair");
    Check(!ShouldUseObservedOwnerPairForAuthoritativeCapture(
              true, true, true, true, true),
          "a valid authoritative resolver pair outranks the observed-owner fallback");
    Check(!ShouldUseObservedOwnerPairForAuthoritativeCapture(
              true, true, false, true, false),
          "an observed pair belonging to another owner cannot cross a character switch");
    Check(!ShouldUseObservedOwnerPairForAuthoritativeCapture(
              false, true, true, true, false),
          "a non-authoritative pointer fallback cannot adopt a switched owner pair");

    Check(IsAuthoritativePlayerPointerCapture(
              PlayerPointerCaptureKind::CurrentPlayerCaptureBaseRax),
          "the current-player capture-base path is authoritative");
    Check(!IsAuthoritativePlayerPointerCapture(
              PlayerPointerCaptureKind::LegacyMarkerRdx),
          "the legacy marker path remains a non-authoritative fallback");

    Check(ShouldAcceptRelatedOutgoingSource(true, false),
          "player-related outgoing source does not require a mount marker");
    Check(ShouldAcceptRelatedOutgoingSource(true, true),
          "player-related outgoing source remains valid with a mount marker");
    Check(!ShouldAcceptRelatedOutgoingSource(false, false),
          "unrelated outgoing source remains rejected");

    Check(MatchesPlayerDamageIdentity(0x3000, 0, 0, 0x3000, 0, 0),
          "partial player root is a tracked outgoing participant");
    Check(MatchesPlayerDamageIdentity(0x4000, 0, 0, 0, 0x4000, 0),
          "partial player damage target is a tracked outgoing participant");
    Check(MatchesPlayerDamageIdentity(0x5000, 0, 0, 0, 0, 0x5000),
          "partial player damage source is a tracked outgoing participant");
    Check(!MatchesPlayerDamageIdentity(0x6000, 0x1000, 0x2000, 0x3000, 0x4000, 0x5000),
          "unrelated damage participant is rejected");

    if (g_failures != 0) {
        std::fprintf(stderr, "%d routing policy test(s) failed\n", g_failures);
        return 1;
    }

    std::puts("routing policy tests passed");
    return 0;
}
