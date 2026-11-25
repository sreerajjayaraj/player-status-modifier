#pragma once
#include "oxygen_scanner.h"
psm::oxygen::Sites PreparePlayerOxygenHooks();
bool InstallPlayerOxygenHooks(const psm::oxygen::Sites& sites, bool stats_observer_ready);
void RemovePlayerOxygenHooks();
