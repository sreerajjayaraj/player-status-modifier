#pragma once
#include "oxygen_scanner.h"
#include <cstdint>
void SetPlayerOxygenEnabled(bool enabled, psm::oxygen::Catalog catalog = {},
                            uintptr_t server_actor_vtable = 0, uintptr_t client_control_slot = 0,
                            uintptr_t server_status_vtable = 0);
void ObservePlayerOxygenGauge(uintptr_t gauge, uintptr_t actor);
struct OxygenHudSample {
 int64_t shown=0, shown_max=0, stored=0, rate=0, maximum=0, minimum=0;
 uint16_t selector=0;
 bool raw_available=false, same_player=false, entry_proved=false, control_bound=false;
};
bool ReadPlayerOxygenHud(uintptr_t widget, uintptr_t actor, OxygenHudSample* sample);
void ObservePlayerOxygenEntry(uintptr_t owner, uintptr_t entry);
bool TryLockPlayerOxygen(uintptr_t entry, int64_t value, int64_t* result, bool rate);
bool TryPreservePlayerOxygenWrite(uintptr_t entry, int64_t requested, int64_t* result);
