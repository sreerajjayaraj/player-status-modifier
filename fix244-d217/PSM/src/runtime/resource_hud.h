#pragma once
#include <cstdint>

void ResetResourceHudLocked();
void NotifyResourceHudBindingLocked();
void ObserveResourceHudModifiers(uintptr_t owner);
void ObserveResourceHudCommittedCost(uintptr_t entry, int64_t before);
bool ResourceHudStaminaActive(uintptr_t equip_component);
void ApplyResourceHudPresentation(uintptr_t gauge, uintptr_t actor);
void SetResourceHudEnabled(bool enabled, bool modifier_contract);
