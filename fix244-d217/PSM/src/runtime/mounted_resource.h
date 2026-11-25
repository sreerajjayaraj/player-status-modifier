#pragma once
#include <cstdint>

void SetNativeMountedResourceEnabled(bool enabled, bool current_3446 = false);
void ResetNativeMountedResourceLocked();
void ObserveNativeMountedActor(uintptr_t controller, uintptr_t player, uintptr_t mount);
void ObserveNativeMountedResource(uintptr_t owner, uint16_t selector, uintptr_t entry);
bool TryLockNativeMountedResource(uintptr_t entry, int64_t value, int64_t* result, bool rate);
