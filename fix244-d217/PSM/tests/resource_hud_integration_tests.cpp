#include "runtime/resource_hud.h"
#include "runtime/runtime_state.h"
#include "config.h"
#include <array>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace {
void Check(bool ok, const char* name) {
    if (!ok) { std::cerr << "FAIL: " << name << '\n'; std::exit(1); }
}
template<class T, size_t N> void Put(std::array<std::byte, N>& b, size_t at, T value) {
    std::memcpy(b.data() + at, &value, sizeof value);
}
template<class T, size_t N> T Get(const std::array<std::byte, N>& b, size_t at) {
    T value{}; std::memcpy(&value, b.data() + at, sizeof value); return value;
}
template<class T> uintptr_t Ptr(T& value) { return reinterpret_cast<uintptr_t>(value.data()); }
struct Actor {
    std::array<std::byte, 0x100> actor{}, table{}, status{}, equip{};
    std::array<std::byte, 0x110> owner{};
    std::array<std::byte, 0x90> stamina{}, spirit{}, modifiers{};
    std::array<std::byte, 0x300> gauge{};
    Actor() {
        Put(actor, 0x68, Ptr(table)); Put(table, 0x20, Ptr(status));
        Put(table, 0x38, Ptr(equip)); Put(status, 0x18, Ptr(owner)); Put(equip, 8, Ptr(actor));
        Put(stamina, 0, int32_t{22}); Put(stamina, 8, int64_t{354000});
        Put(stamina, 0x18, int64_t{360000});
        Put(spirit, 0, int32_t{23}); Put(spirit, 8, int64_t{122500});
        Put(spirit, 0x18, int64_t{130000});
        Put(owner, 0xD8, Ptr(modifiers)); Put(owner, 0xE0, uint32_t{2});
        Put(owner, 0xE4, uint32_t{3});
        Put(modifiers, 0, uint16_t{22}); Put(modifiers, 8, int64_t{-100});
        Put(modifiers, 0x30, uint16_t{22}); Put(modifiers, 0x38, int64_t{100000});
        Gauge(1, 360000);
    }
    void Gauge(uint8_t role, int64_t maximum) {
        Put(gauge, 0x255, role); Put(gauge, 0x230, maximum); Put(gauge, 0x238, maximum);
    }
    void Bind() {
        std::lock_guard lock(g_state_mutex);
        g_player_resolve = {};
        g_player_resolve.actor = Ptr(actor); g_player_resolve.marker = Ptr(status);
        g_player_resolve.root = g_player_resolve.resource_owner = Ptr(owner);
        g_player_resolve.health_entry = Ptr(owner);
        g_player_resolve.stamina_entry = Ptr(stamina); g_player_resolve.spirit_entry = Ptr(spirit);
        g_player_resolve.stamina_type = 22; g_player_resolve.spirit_type = 23;
        NotifyResourceHudBindingLocked();
    }
};
}

int main() {
    ModConfig config{};
    config.general.enabled = config.stamina.enabled = config.spirit.enabled = true;
    config.stamina.heal_multiplier = 100;
    config.spirit.heal_multiplier = 20;
    SetConfigSnapshot(L"private-hud-test.ini", config);
    g_runtime_enabled.store(true);
    SetResourceHudEnabled(true, true);
    Actor kliff, damiane, oongka, mount;
    for (auto* a : {&kliff, &damiane, &oongka}) {
        a->Bind();
        Check(!ResourceHudStaminaActive(Ptr(a->equip)), "new character has no old HUD activity");
        const auto original_stamina = a->stamina;
        const auto original_spirit = a->spirit;
        const auto original_owner = a->owner;
        ObserveResourceHudCommittedCost(Ptr(a->stamina), 360000);
        Check(ResourceHudStaminaActive(Ptr(a->equip)), "each actor expense shows stamina");
        a->Gauge(1, 360000);
        ApplyResourceHudPresentation(Ptr(a->gauge), Ptr(a->actor));
        Check(Get<int64_t>(a->gauge, 0x230) == 354000, "stamina UI shows real expense despite refill");
        a->Gauge(2, 130000);
        ObserveResourceHudCommittedCost(Ptr(a->spirit), 130000);
        ApplyResourceHudPresentation(Ptr(a->gauge), Ptr(a->actor));
        Check(Get<int64_t>(a->gauge, 0x230) == 122000, "Spirit UI shows real expense despite refill");
        Check(a->stamina == original_stamina && a->spirit == original_spirit && a->owner == original_owner,
              "HUD operations never write gameplay state");
        a->Gauge(0, 360000);
        ApplyResourceHudPresentation(Ptr(a->gauge), Ptr(a->actor));
        Check(Get<int64_t>(a->gauge, 0x230) == 360000, "health HUD unchanged");
    }
    damiane.Bind();
    ObserveResourceHudCommittedCost(Ptr(oongka.stamina), 360000);
    ObserveResourceHudCommittedCost(Ptr(mount.stamina), 360000);
    Check(!ResourceHudStaminaActive(Ptr(damiane.equip)), "other actor and mount cannot publish expense");
    ObserveResourceHudModifiers(Ptr(damiane.owner));
    Check(ResourceHudStaminaActive(Ptr(damiane.equip)), "negative sustained component survives large regen");
    Check(!ResourceHudStaminaActive(Ptr(mount.equip)), "mount actor cannot consume player HUD activity");
    Put(damiane.modifiers, 8, int64_t{0});
    ObserveResourceHudModifiers(Ptr(damiane.owner));
    Check(!ResourceHudStaminaActive(Ptr(damiane.equip)), "removing sustained cost clears activity");
    Put(damiane.modifiers, 8, int64_t{-100});
    Put(damiane.owner, 0xE0, uint32_t{513});
    Put(damiane.owner, 0xE4, uint32_t{513});
    ObserveResourceHudModifiers(Ptr(damiane.owner));
    Check(!ResourceHudStaminaActive(Ptr(damiane.equip)), "oversize native list rejected without walking");
    Put(damiane.owner, 0xE0, uint32_t{2}); Put(damiane.owner, 0xE4, uint32_t{3});
    ObserveResourceHudModifiers(Ptr(damiane.owner));
    { std::lock_guard lock(g_state_mutex); ResetResourceHudLocked(); }
    Check(!ResourceHudStaminaActive(Ptr(damiane.equip)), "same-address reset clears sustained activity");
    config.general.enabled = false; SetConfigSnapshot(L"private-hud-test.ini", config);
    ObserveResourceHudCommittedCost(Ptr(damiane.stamina), 360000);
    config.general.enabled = true; SetConfigSnapshot(L"private-hud-test.ini", config);
    Check(!ResourceHudStaminaActive(Ptr(damiane.equip)), "disabled config cannot leave delayed expense");
    std::thread producer([&] { for (int i = 0; i < 20000; ++i)
        ObserveResourceHudCommittedCost(Ptr(kliff.stamina), 360000); });
    for (int i = 0; i < 20000; ++i) {
        (i % 2 ? kliff : damiane).Bind();
        if (i % 2 == 0)
            Check(!ResourceHudStaminaActive(Ptr(damiane.equip)), "concurrent stale publisher rejected");
    }
    producer.join();
    SetResourceHudEnabled(false, false);
    std::cout << "resource HUD integration tests passed: all actors, ownership, reset, disabled, bounded, concurrent\n";
}
