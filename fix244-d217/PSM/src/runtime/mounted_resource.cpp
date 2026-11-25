#include "runtime/mounted_resource.h"
#include "runtime/runtime_state.h"
#include "config.h"
#include "logger.h"
#include <array>
#include <limits>

namespace {
struct MountedBinding {
    uintptr_t controller = 0, player = 0, mount = 0, ride = 0;
    uintptr_t owner = 0, entries = 0, stamina = 0;
    uint32_t id = 0;
    uint16_t selector = 0;
    std::array<uintptr_t, 8> player_binding{};
};
MountedBinding g_binding;
std::atomic<uintptr_t> g_observed_owner{0}, g_observed_stamina{0};
bool g_enabled = false;
bool g_current_3446 = false;
unsigned g_bind_logs = 0, g_cost_logs = 0, g_rate_logs = 0;

template<class T> bool Read(uintptr_t base, size_t offset, T* value) {
    if (!value || base < kMinimumPointerAddress || offset > UINTPTR_MAX - base ||
        sizeof(T) > UINTPTR_MAX - base - offset) return false;
    __try { *value = *reinterpret_cast<const T*>(base + offset); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

std::array<uintptr_t, 8> PlayerBinding() {
    const auto& p = g_player_resolve;
    return {p.actor, p.marker, p.root, p.resource_owner, p.health_entry,
            p.stamina_entry, p.spirit_entry, static_cast<uintptr_t>(p.stamina_type)};
}

bool ActorOwner(uintptr_t actor, uintptr_t* owner) {
    uintptr_t table = 0, status = 0;
    uint8_t dead = 1;
    return Read(actor, 0x68, &table) && Read(table, 0x20, &status) &&
        Read(status, g_current_3446 ? 0x293 : 0x273, &dead) && !dead && Read(status, 0x18, owner) &&
        *owner >= kMinimumPointerAddress;
}

bool RideIdentity(uintptr_t player, uintptr_t* ride, uint32_t* id) {
    uintptr_t table = 0, vehicle = 0;
    return Read(player, 0x68, &table) && Read(table, 0x48, &vehicle) &&
        Read(vehicle, 0x18, ride) && Read(*ride, 0x10, id) && *id;
}

bool PlayerReady() {
    return g_enabled && g_runtime_enabled.load(std::memory_order_acquire) &&
        g_player_resolve.valid() && g_player_resolve.resource_owner >= kMinimumPointerAddress &&
        g_player_resolve.stamina_type > 0 && g_player_resolve.stamina_type <= UINT16_MAX;
}

bool Live(const MountedBinding& b) {
    uintptr_t player_owner = 0, mount_owner = 0, ride = 0, entries = 0;
    uint32_t live_id = 0, actor_id = 0, hud_id = 0;
    return PlayerReady() && b.player_binding == PlayerBinding() && b.id &&
        b.mount != b.player && ActorOwner(b.player, &player_owner) &&
        player_owner == g_player_resolve.resource_owner &&
        RideIdentity(b.player, &ride, &live_id) && ride == b.ride && live_id == b.id &&
        Read(b.controller, g_current_3446 ? 0x370 : 0x360, &hud_id) && hud_id == live_id &&
        Read(b.mount, 0x60, &actor_id) && actor_id == live_id &&
        ActorOwner(b.mount, &mount_owner) && mount_owner == b.owner &&
        mount_owner != player_owner && Read(mount_owner, 0x58, &entries) &&
        entries == b.entries && entries >= kMinimumPointerAddress;
}

bool EntryMatches(const MountedBinding& b, uintptr_t entry, uint16_t selector) {
    uint16_t actual = 0;
    const auto& p = g_player_resolve;
    // The native lookup supplies the entry. This is a bounded membership check,
    // not a nearby-entry scan or a health/stamina maximum heuristic.
    return selector == p.stamina_type && entry >= b.entries &&
        entry - b.entries < 512 * 0x90ULL && (entry - b.entries) % 0x90 == 0 &&
        entry != p.health_entry && entry != p.stamina_entry && entry != p.spirit_entry &&
        Read(entry, 0, &actual) && actual == selector;
}
}

void ResetNativeMountedResourceLocked() {
    g_observed_stamina.store(0, std::memory_order_release);
    g_observed_owner.store(0, std::memory_order_release);
    g_binding = {};
}

void SetNativeMountedResourceEnabled(bool enabled, bool current_3446) {
    std::lock_guard lock(g_state_mutex);
    g_enabled = enabled;
    g_current_3446 = current_3446;
    ResetNativeMountedResourceLocked();
}

void ObserveNativeMountedActor(uintptr_t controller, uintptr_t player, uintptr_t mount) {
    std::lock_guard lock(g_state_mutex);
    MountedBinding next{};
    next.controller = controller; next.player = player; next.mount = mount;
    next.player_binding = PlayerBinding();
    if (!PlayerReady() || !RideIdentity(player, &next.ride, &next.id) ||
        !ActorOwner(mount, &next.owner) || !Read(next.owner, 0x58, &next.entries) || !Live(next)) {
        ResetNativeMountedResourceLocked();
        return;
    }
    if (g_binding.controller == controller && g_binding.player == player &&
        g_binding.mount == mount && g_binding.owner == next.owner &&
        g_binding.entries == next.entries && g_binding.ride == next.ride &&
        g_binding.id == next.id && g_binding.player_binding == next.player_binding) return;
    g_observed_stamina.store(0, std::memory_order_release);
    g_binding = next;
    g_observed_owner.store(next.owner, std::memory_order_release);
}

void ObserveNativeMountedResource(uintptr_t owner, uint16_t selector, uintptr_t entry) {
    if (!owner || owner != g_observed_owner.load(std::memory_order_acquire)) return;
    std::lock_guard lock(g_state_mutex);
    if (!g_enabled || owner != g_binding.owner || selector != g_player_resolve.stamina_type) return;
    if (!Live(g_binding)) { ResetNativeMountedResourceLocked(); return; }
    if (!EntryMatches(g_binding, entry, selector)) return;
    if (g_binding.stamina == entry && g_binding.selector == selector) return;
    g_binding.stamina = entry; g_binding.selector = selector;
    g_observed_stamina.store(entry, std::memory_order_release);
    if (g_bind_logs++ < 64)
        Log("mount-native: bound ride-id=%u player=0x%p mount=0x%p owner=0x%p stamina=0x%p selector=%u source=native-lookup",
            g_binding.id, reinterpret_cast<void*>(g_binding.player), reinterpret_cast<void*>(g_binding.mount),
            reinterpret_cast<void*>(owner), reinterpret_cast<void*>(entry), selector);
}

bool TryLockNativeMountedResource(uintptr_t entry, int64_t value, int64_t* result, bool rate) {
    if (!result || value >= 0 || !entry ||
        entry != g_observed_stamina.load(std::memory_order_acquire)) return false;
    const auto config = GetConfig();
    if (!config.general.enabled || !config.mount.enabled || !config.mount.lock_stamina ||
        !config.mount.special_enabled || !config.mount.lock_special_stamina) return false;
    std::lock_guard lock(g_state_mutex);
    if (!g_enabled || !entry || entry != g_binding.stamina) return false;
    if (!Live(g_binding) || !EntryMatches(g_binding, entry, g_binding.selector)) {
        ResetNativeMountedResourceLocked();
        return false;
    }
    *result = 0;
    auto& samples = rate ? g_rate_logs : g_cost_logs;
    if (samples++ < 24)
        Log("mount-native: locked %s ride-id=%u entry=0x%p original=%lld adjusted=0 stored-rate-unchanged=1",
            rate ? "rate" : "cost", g_binding.id, reinterpret_cast<void*>(entry), static_cast<long long>(value));
    return true;
}
