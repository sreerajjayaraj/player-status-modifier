#pragma once

#include <cstdint>

enum class AuthoritativeResourceKind : std::uint8_t {
    Stamina = 0,
    Spirit,
};

void ObserveStatEntry(uintptr_t entry, uintptr_t component);
bool TryAdjustSpiritDelta(uintptr_t entry, int64_t* delta);
bool TryAdjustStaminaDelta(uintptr_t entry, int64_t* delta);
bool TryAdjustAuthoritativePlayerResourceDelta(AuthoritativeResourceKind kind,
                                               uintptr_t entry,
                                               int64_t* delta);
void SetAuthoritativePlayerResourceDeltaActive(bool active);
bool IsAuthoritativePlayerResourceDeltaActive();
bool TryAdjustStatWrite(uintptr_t entry, int64_t* value, uintptr_t owner_root = 0,
                        bool preserve_native_health_floor = true);
void RecordAuthoritativeResourceAdjustment(AuthoritativeResourceKind kind,
                                           uintptr_t entry,
                                           int64_t final_value);
void RecordAuthoritativePlayerSpiritCommit(uintptr_t entry,
                                           int64_t previous_value,
                                           int64_t final_value);
bool WasRecentlyAdjustedByAuthoritativeResourceWrite(uintptr_t entry,
                                                      int64_t observed_value,
                                                      uint32_t window_ms);
bool TryConsumeSpiritMirrorWrite(uintptr_t entry, int64_t observed_value);
void RecordReroutedSpiritDelta(uintptr_t entry, int64_t requested_value);
bool TryConsumeReroutedSpiritStatWrite(uintptr_t entry, int64_t requested_value);
void RecordFocusRecoveryWindow();
bool IsFocusRecoveryWindowActive();
bool WasRecentPlayerSpiritConsumption(uintptr_t entry, uint32_t window_ms);
bool WasRecentPlayerStaminaConsumption(uintptr_t entry, uint32_t window_ms);
uintptr_t GetRecentPlayerSpiritConsumptionEntry(uint32_t window_ms);
bool IsPostRematchDetachedPlayerStaminaMirror(uintptr_t entry);
bool TryRedirectCombatStaminaCostToAdjacentSpirit(uintptr_t stamina_entry, int64_t* delta);
bool SyncPlayerSpiritVisualMirror(uintptr_t primary_entry, int64_t primary_value, int64_t primary_max, const char* reason);
