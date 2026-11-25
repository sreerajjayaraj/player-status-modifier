#pragma once
#include "hud_scanner.h"
#include <array>

struct PreparedResourceHud {
    psm::hud::ScanResult sites;
    bool modifiers = false;
    std::array<std::array<unsigned char, 16>, 3> original{};
};
PreparedResourceHud PrepareResourceHudHooks(uintptr_t native_cost_site);
bool InstallResourceHudHooks(const PreparedResourceHud& prepared);
void RemoveResourceHudHooks();
