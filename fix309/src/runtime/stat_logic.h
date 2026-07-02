#pragma once

#include <cstdint>

struct ModConfig;

void ObserveStatEntry(uintptr_t entry, uintptr_t component);
bool TryAdjustSpiritDelta(uintptr_t entry, int64_t* delta);
bool TryAdjustStaminaDelta(uintptr_t entry, int64_t* delta);
bool TryAdjustStatWrite(uintptr_t entry, int64_t* value, uintptr_t owner_root = 0);
void RecordPlayerStaminaPreNotifyWrite(uintptr_t entry, int64_t final_value);
bool TryConsumePlayerStaminaPreNotifyWrite(uintptr_t entry, int64_t observed_value, int64_t* final_value);
bool WasRecentPlayerStaminaPreNotifyWrite(uintptr_t entry, uint32_t window_ms);
bool TryGetPlayerStaminaAb00VirtualBase(uintptr_t entry, int64_t current_value, int64_t max_value, const ModConfig& config, int64_t* base_value);
void RecordPlayerStaminaAb00VirtualCost(uintptr_t entry, int64_t final_value, int64_t max_value);
bool TryApplyPlayerStaminaAb00VirtualRecovery(uintptr_t entry, int64_t observed_value, int64_t max_value, const ModConfig& config, int64_t* final_value);
bool TryConsumeSpiritMirrorWrite(uintptr_t entry, int64_t observed_value);
void RecordReroutedSpiritDelta(uintptr_t entry, int64_t requested_value);
bool TryConsumeReroutedSpiritStatWrite(uintptr_t entry, int64_t requested_value);
void RecordFocusRecoveryWindow();
bool IsFocusRecoveryWindowActive();
bool IsFocusModeActive();
void ClearFocusRecoveryWindow(const char* reason);
bool WasRecentPlayerSpiritConsumption(uintptr_t entry, uint32_t window_ms);
bool WasRecentPlayerStaminaConsumption(uintptr_t entry, uint32_t window_ms);
uintptr_t GetRecentPlayerSpiritConsumptionEntry(uint32_t window_ms);
bool IsPostRematchDetachedPlayerStaminaMirror(uintptr_t entry);
bool TryRedirectCombatStaminaCostToAdjacentSpirit(uintptr_t stamina_entry, int64_t* delta);
bool SyncPlayerSpiritVisualMirror(uintptr_t primary_entry, int64_t primary_value, int64_t primary_max, const char* reason);
