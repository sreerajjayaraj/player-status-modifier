#include "runtime/resource_hud.h"
#include "runtime/resource_hud_policy.h"
#include "runtime/runtime_state.h"
#include "config.h"
#include "logger.h"
#include <array>
#include <limits>

namespace {
resource_hud::State g_hud;
std::array<uintptr_t, 8> g_binding{};
bool g_enabled = false, g_modifier_contract = false;
unsigned g_binding_logs = 0, g_cost_logs = 0, g_modifier_logs = 0, g_display_logs = 0;

template<class T> bool Read(uintptr_t address, T* value) {
    if (address < kMinimumPointerAddress || !value) return false;
    __try { *value = *reinterpret_cast<const T*>(address); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool WriteGauge(uintptr_t gauge, int64_t value) {
    __try { *reinterpret_cast<int64_t*>(gauge + 0x230) = value; return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool ReadyLocked(const ModConfig& config) {
    NotifyResourceHudBindingLocked();
    if (!g_enabled || !config.general.enabled || !g_runtime_enabled.load()) {
        g_hud.Reset();
        return false;
    }
    return g_player_resolve.valid() &&
        g_player_resolve.resource_owner >= kMinimumPointerAddress &&
        g_player_resolve.stamina_entry != g_player_resolve.spirit_entry;
}

bool RoleEnabled(const ModConfig& config, unsigned role) {
    const bool stamina = (role == 0) != config.advanced.swap_player_stamina_spirit;
    return (stamina ? config.stamina : config.spirit).enabled;
}

bool ActorMatchesLocked(uintptr_t actor) {
    if (actor < kMinimumPointerAddress) return false;
    uintptr_t table = 0, component = 0, owner = 0;
    // This is the same actor -> status owner chain used by the native HUD
    // refresh. Recheck it for every call; no native object is retained here.
    return Read(actor + 0x68, &table) && Read(table + 0x20, &component) &&
        Read(component + 0x18, &owner) && owner == g_player_resolve.resource_owner;
}
}

void ResetResourceHudLocked() {
    g_hud.Reset();
    g_binding = {};
}

void NotifyResourceHudBindingLocked() {
    const auto& p = g_player_resolve;
    const std::array<uintptr_t, 8> next{p.actor, p.marker, p.root, p.resource_owner,
        p.stamina_entry, p.spirit_entry, static_cast<uintptr_t>(p.stamina_type),
        static_cast<uintptr_t>(p.spirit_type)};
    if (g_binding != next) {
        g_hud.Reset();
        g_binding = next;
        if (g_enabled && p.valid() && g_binding_logs++ < 16)
            Log("hud: binding reset actor=0x%p owner=0x%p stamina=0x%p spirit=0x%p",
                reinterpret_cast<void*>(p.actor), reinterpret_cast<void*>(p.resource_owner),
                reinterpret_cast<void*>(p.stamina_entry), reinterpret_cast<void*>(p.spirit_entry));
    }
}

void SetResourceHudEnabled(bool enabled, bool modifier_contract) {
    std::lock_guard lock(g_state_mutex);
    g_enabled = enabled;
    g_modifier_contract = modifier_contract;
    ResetResourceHudLocked();
}

void ObserveResourceHudModifiers(uintptr_t owner) {
    const auto config = GetConfig();
    std::lock_guard lock(g_state_mutex);
    if (!ReadyLocked(config) || !g_modifier_contract ||
        owner != g_player_resolve.resource_owner) return;
    uintptr_t list = 0;
    uint32_t count = 0, capacity = 0;
    unsigned mask = 0;
    // Called only inside the native modifier transaction, after its array
    // mutation and before AB00 aggregates it. Never walk this array on a worker.
    if (!Read(owner + 0xD8, &list) || !Read(owner + 0xE0, &count) ||
        !Read(owner + 0xE4, &capacity) || count > capacity || count > 512 ||
        (count && (list < kMinimumPointerAddress || list > UINTPTR_MAX - count * 0x30ull))) {
        g_hud.SetContinuous(0);
        return;
    }
    for (uint32_t i = 0; i < count; ++i) {
        const auto node = list + i * 0x30ull;
        uint16_t type = 0;
        int64_t base = 0, discount = 0, bonus = 0;
        if (!Read(node, &type) || !Read(node + 8, &base) ||
            !Read(node + 0x18, &discount) || !Read(node + 0x28, &bonus)) {
            g_hud.SetContinuous(0);
            return;
        }
        if (!resource_hud::IsNegativeContribution(base, discount, bonus)) continue;
        if (type == g_player_resolve.stamina_type) mask |= 1;
        if (type == g_player_resolve.spirit_type) mask |= 2;
    }
    g_hud.SetContinuous(mask);
    if (mask && g_modifier_logs++ < 16)
        Log("hud: native sustained cost owner=0x%p roles=%u modifiers=%u; values unchanged",
            reinterpret_cast<void*>(owner), mask, count);
}

void ObserveResourceHudCommittedCost(uintptr_t entry, int64_t before) {
    const auto config = GetConfig();
    std::lock_guard lock(g_state_mutex);
    if (!ReadyLocked(config)) return;
    const unsigned role = entry == g_player_resolve.stamina_entry ? 0 :
        (entry == g_player_resolve.spirit_entry ? 1 : 2);
    if (role == 2 || !RoleEnabled(config, role)) return;
    int64_t after = 0, maximum = 0, bonus = 0;
    if (!Read(entry + 8, &after) || !Read(entry + 0x18, &maximum) ||
        !Read(entry + 0x20, &bonus) || maximum <= 0 || bonus < 0 ||
        bonus > INT64_MAX - maximum) return;
    maximum += bonus;
    g_hud.RecordCost(role, before, after, maximum, GetTickCount64());
    if (after >= 0 && after < before && before <= maximum && g_cost_logs++ < 24)
        Log("hud: committed expense role=%u before=%lld after=%lld max=%lld; presentation-only",
            role, static_cast<long long>(before), static_cast<long long>(after),
            static_cast<long long>(maximum));
}

bool ResourceHudStaminaActive(uintptr_t equip_component) {
    const auto config = GetConfig();
    std::lock_guard lock(g_state_mutex);
    uintptr_t actor = 0, table = 0, round_trip = 0;
    if (!Read(equip_component + 8, &actor) || !Read(actor + 0x68, &table) ||
        !Read(table + 0x38, &round_trip) || round_trip != equip_component) return false;
    return ReadyLocked(config) && RoleEnabled(config, 0) &&
        ActorMatchesLocked(actor) && g_hud.Active(0, GetTickCount64());
}

void ApplyResourceHudPresentation(uintptr_t gauge, uintptr_t actor) {
    const auto config = GetConfig();
    std::lock_guard lock(g_state_mutex);
    if (!ReadyLocked(config) || !ActorMatchesLocked(actor)) return;
    uint8_t native_role = 0;
    int64_t current = 0, maximum = 0;
    if (!Read(gauge + 0x255, &native_role) || native_role < 1 || native_role > 2 ||
        !RoleEnabled(config, native_role - 1) || !Read(gauge + 0x230, &current) ||
        !Read(gauge + 0x238, &maximum) || current < 0 || maximum <= 0 || current > maximum) return;
    const auto display = g_hud.Display(native_role - 1, current, maximum, GetTickCount64());
    if (display == current) return;
    if (!WriteGauge(gauge, display)) {
        g_enabled = false;
        g_hud.Reset();
        Log("hud: gauge presentation write rejected; HUD extension disabled");
        return;
    }
    if (g_display_logs++ < 24)
        Log("hud: expense feedback role=%u native=%lld displayed=%lld max=%lld; gameplay unchanged",
            native_role - 1, static_cast<long long>(current), static_cast<long long>(display),
            static_cast<long long>(maximum));
}
