#pragma once
#include "hud_scanner.h"

psm::hud::MountedResourceScanResult PrepareNativeMountedResourceHooks();
bool InstallNativeMountedResourceHooks(const psm::hud::MountedResourceScanResult& sites);
void RemoveNativeMountedResourceHooks();
