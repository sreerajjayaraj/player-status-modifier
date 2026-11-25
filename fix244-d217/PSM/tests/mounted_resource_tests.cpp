#include "runtime/mounted_resource.h"
#include "runtime/runtime_state.h"
#include "runtime/stat_logic.h"
#include "config.h"
#include <safetyhook.hpp>
#include <array>
#include <cstring>
#include <cstdlib>
#include <iostream>

void TestNativeResourceRateCallback(SafetyHookContext& ctx);
void TestNativeResourceCostCallback(SafetyHookContext& ctx);

namespace {
int checks = 0;
void Check(bool ok, const char* name) {
    ++checks;
    if (!ok) { std::cerr << "FAIL: " << name << '\n'; std::exit(1); }
}
template<class T, size_t N> void Put(std::array<std::byte, N>& b, size_t at, T value) {
    std::memcpy(b.data() + at, &value, sizeof value);
}
template<class T> uintptr_t Ptr(T& b) { return reinterpret_cast<uintptr_t>(b.data()); }
struct Actor {
    std::array<std::byte, 0x300> actor{}, status{};
    std::array<std::byte, 0x100> table{}, vehicle{}, ride{}, owner{};
    std::array<std::byte, 0x1B0> entries{};
    uint32_t id;
    explicit Actor(uint32_t n) : id(n) {
        Put(actor, 0x60, id); Put(actor, 0x68, Ptr(table));
        Put(table, 0x20, Ptr(status)); Put(status, 0x18, Ptr(owner));
        Put(table, 0x48, Ptr(vehicle)); Put(vehicle, 0x18, Ptr(ride));
        Put(owner, 0x58, Ptr(entries));
        Put(entries, 0, int32_t{0}); Put(entries, 8, int64_t{250000});
        Put(entries, 0x18, int64_t{250000});
        Put(entries, 0x90, int32_t{22}); Put(entries, 0x98, int64_t{390000});
        Put(entries, 0xA8, int64_t{390000});
        Put(entries, 0x120, int32_t{23}); Put(entries, 0x128, int64_t{130000});
        Put(entries, 0x138, int64_t{130000});
    }
    uintptr_t Stamina() { return Ptr(entries) + 0x90; }
    uintptr_t Spirit() { return Ptr(entries) + 0x120; }
    void Bind() {
        std::lock_guard lock(g_state_mutex);
        g_player_resolve = {};
        g_player_resolve.actor = Ptr(actor); g_player_resolve.marker = Ptr(status);
        g_player_resolve.root = g_player_resolve.resource_owner = Ptr(owner);
        g_player_resolve.health_entry = Ptr(entries);
        g_player_resolve.stamina_entry = Stamina(); g_player_resolve.spirit_entry = Spirit();
        g_player_resolve.stamina_type = 22; g_player_resolve.spirit_type = 23;
    }
};
}

int main(int argc, char** argv) {
    const bool current_3446 = argc == 2 && std::strcmp(argv[1], "--3446") == 0;
    const size_t dead_offset = current_3446 ? 0x293 : 0x273;
    const size_t ride_offset = current_3446 ? 0x370 : 0x360;
    ModConfig config{};
    config.general.enabled = config.mount.enabled = config.mount.lock_stamina = true;
    config.mount.special_enabled = config.mount.lock_special_stamina = true;
    config.stamina.enabled = config.spirit.enabled = true;
    config.stamina.heal_multiplier = 100;
    config.stamina.consumption_multiplier = 0.25;
    config.spirit.consumption_multiplier = 0.5;
    config.spirit.heal_multiplier = 20;
    SetConfigSnapshot(L"private-mount-test.ini", config);
    g_runtime_enabled.store(true);
    SetNativeMountedResourceEnabled(true, current_3446);
    SetAuthoritativePlayerResourceDeltaActive(true);
    Actor kliff(10), damiane(20), oongka(30), horseK(100), horseD(200), horseO(300), npc(400);
    std::array<std::byte, 0x400> root{};
    const auto capture = [&](Actor& player, Actor& mount) {
        Put(player.ride, 0x10, mount.id); Put(root, ride_offset, mount.id);
        ObserveNativeMountedActor(Ptr(root), Ptr(player.actor), Ptr(mount.actor));
        ObserveNativeMountedResource(Ptr(mount.owner), 22, mount.Stamina());
    };
    const auto locked = [&](uintptr_t entry, int64_t value, bool rate) {
        int64_t result = value;
        return TryLockNativeMountedResource(entry, value, &result, rate) && result == 0;
    };
    const auto untouched = [&](uintptr_t entry, int64_t value, bool rate) {
        int64_t result = value;
        return !TryLockNativeMountedResource(entry, value, &result, rate) && result == value;
    };
    const std::array<Actor*, 3> players{&kliff, &damiane, &oongka};
    const std::array<Actor*, 3> mounts{&horseK, &horseD, &horseO};
    for (size_t i = 0; i < players.size(); ++i) {
        auto& player = *players[i]; auto& mount = *mounts[i];
        player.Bind(); capture(player, mount);
        const auto original = mount.entries;
        Check(locked(mount.Stamina(), -40000, false), "each rider's own horse charge cost is locked");
        Check(locked(mount.Stamina(), -3000, true), "each rider's own horse sprint rate is locked");
        Check(untouched(mount.Stamina(), 11200, true), "mount natural regeneration is not multiplied or blocked");
        Check(untouched(player.Stamina(), -3000, true), "player stamina never enters mount route");
        Check(untouched(player.Spirit(), -10000, false), "player Spirit never enters mount route");
        Check(untouched(mount.Spirit(), -10000, false), "mount Spirit is not stamina");
        Check(untouched(Ptr(mount.entries), -10000, false), "mount health is unchanged");
        Check(untouched(npc.Stamina(), -10000, false), "unrelated actors are not locked");
        Check(mount.entries == original, "observer and lock never write native resource memory");
        SafetyHookContext rate{};
        rate.rbx = mount.Stamina(); rate.r11 = static_cast<uintptr_t>(-40000LL); rate.rflags = 0x202;
        auto expected = rate; expected.r11 = 0;
        TestNativeResourceRateCallback(rate);
        Check(std::memcmp(&rate, &expected, sizeof rate) == 0, "real rate callback only changes verified mount R11");
        SafetyHookContext cost{};
        cost.rsi = mount.Stamina(); cost.rbx = static_cast<uintptr_t>(-40000LL); cost.rflags = 0x202;
        expected = cost; expected.rbx = 0;
        TestNativeResourceCostCallback(cost);
        Check(std::memcmp(&cost, &expected, sizeof cost) == 0, "real cost callback only changes verified mount RBX");
        rate.rbx = player.Stamina(); rate.r11 = static_cast<uintptr_t>(-40000LL);
        expected = rate; expected.r11 = static_cast<uintptr_t>(-10000LL);
        TestNativeResourceRateCallback(rate);
        Check(std::memcmp(&rate, &expected, sizeof rate) == 0, "native player stamina multiplier unchanged while riding");
        cost.rsi = player.Spirit(); cost.rbx = static_cast<uintptr_t>(-10000LL);
        expected = cost; expected.rbx = static_cast<uintptr_t>(-5000LL);
        TestNativeResourceCostCallback(cost);
        Check(std::memcmp(&cost, &expected, sizeof cost) == 0, "native player Spirit multiplier unchanged while riding");
        rate.rbx = mount.Stamina(); rate.r11 = 11200;
        expected = rate;
        TestNativeResourceRateCallback(rate);
        Check(std::memcmp(&rate, &expected, sizeof rate) == 0, "real mount regen callback retains vanilla value");
        cost.rsi = npc.Stamina(); cost.rbx = static_cast<uintptr_t>(-10000LL);
        expected = cost;
        TestNativeResourceCostCallback(cost);
        Check(std::memcmp(&cost, &expected, sizeof cost) == 0, "real callback ignores NPC stamina");
        Put(player.ride, 0x10, uint32_t{0});
        Check(untouched(mount.Stamina(), -1000, true), "dismount rejects stale HUD cache immediately");
    }
    damiane.Bind(); capture(damiane, horseD);
    oongka.Bind();
    Check(untouched(horseD.Stamina(), -1000, true), "character switch rejects old mount");
    damiane.Bind(); capture(damiane, horseD);
    Put(damiane.ride, 0x10, horseK.id);
    Check(untouched(horseD.Stamina(), -1000, true), "changing registered or ridden horse clears old identity");
    capture(damiane, horseK);
    Check(locked(horseK.Stamina(), -1000, true), "same horse works under another rider");
    Check(untouched(horseD.Stamina(), -1000, true), "previous horse is no longer locked by new route");
    capture(damiane, horseD);
    config.mount.lock_stamina = false; SetConfigSnapshot(L"private-mount-test.ini", config);
    Check(untouched(horseD.Stamina(), -1000, true), "live regular lock disable is respected");
    config.mount.lock_stamina = true; SetConfigSnapshot(L"private-mount-test.ini", config);
    Check(locked(horseD.Stamina(), -1000, true), "live reenable needs no restart");
    Put(root, ride_offset, horseO.id);
    Check(untouched(horseD.Stamina(), -1000, true), "stale mismatched HUD identity is rejected");
    capture(damiane, horseD);
    Put(horseD.owner, 0x58, Ptr(npc.entries));
    Check(untouched(horseD.Stamina(), -1000, true), "reallocated owner array invalidates native entry");
    Put(horseD.owner, 0x58, Ptr(horseD.entries)); capture(damiane, horseD);
    ObserveNativeMountedResource(Ptr(horseD.owner), 23, horseD.Spirit());
    Check(locked(horseD.Stamina(), -1000, true), "other native stat query cannot replace stamina");
    ObserveNativeMountedResource(Ptr(horseD.owner), 22, damiane.Stamina());
    Check(untouched(damiane.Stamina(), -1000, true), "foreign lookup cannot publish player entry");
    capture(damiane, horseD);
    const auto good_config = config;
    for (unsigned gate = 0; gate != 5; ++gate) {
        config = good_config;
        if (gate == 0) config.general.enabled = false;
        if (gate == 1) config.mount.enabled = false;
        if (gate == 2) config.mount.lock_stamina = false;
        if (gate == 3) config.mount.special_enabled = false;
        if (gate == 4) config.mount.lock_special_stamina = false;
        SetConfigSnapshot(L"private-mount-test.ini", config);
        Check(untouched(horseD.Stamina(), -1000, true), "each live config gate disables conservative fallback");
    }
    config = good_config; SetConfigSnapshot(L"private-mount-test.ini", config);
    g_runtime_enabled.store(false);
    Check(untouched(horseD.Stamina(), -1000, true), "runtime disable clears stale lock");
    g_runtime_enabled.store(true); capture(damiane, horseD);
    Put(horseD.status, dead_offset, uint8_t{1});
    Check(untouched(horseD.Stamina(), -1000, true), "dead mount is rejected");
    Put(horseD.status, dead_offset, uint8_t{0}); capture(damiane, horseD);
    Put(damiane.status, dead_offset, uint8_t{1});
    Check(untouched(horseD.Stamina(), -1000, true), "dead rider is rejected");
    Put(damiane.status, dead_offset, uint8_t{0}); capture(damiane, horseD);
    Put(horseD.actor, 0x68, uintptr_t{0x10000001});
    Check(untouched(horseD.Stamina(), -1000, true), "unreadable actor chain fails closed without native call");
    Put(horseD.actor, 0x68, Ptr(horseD.table)); capture(damiane, horseD);
    Put(horseD.entries, 0x90, int32_t{23});
    Check(untouched(horseD.Stamina(), -1000, true), "changed native selector invalidates retained entry");
    Put(horseD.entries, 0x90, int32_t{22}); capture(damiane, horseD);
    { std::lock_guard lock(g_state_mutex); ResetTrackedEntriesLocked(); }
    damiane.Bind();
    Check(untouched(horseD.Stamina(), -1000, true), "save reset clears even same-address mount cache");
    capture(damiane, horseD); SetNativeMountedResourceEnabled(false);
    Check(untouched(horseD.Stamina(), -1000, true), "optional scanner or hook failure disables route");
    std::cout << "mounted-resource tests passed: " << checks << " checks\n";
}
