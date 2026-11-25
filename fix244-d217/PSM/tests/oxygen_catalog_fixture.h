#pragma once
#include "runtime/oxygen_resource.h"
#include "runtime/player_identity_publication.h"
#include <array>
#include <cstring>

struct OxygenCatalogFixture {
    std::array<unsigned char, 0x70> actor_registry{}, group_registry{}, status_registry{}, group{}, status{};
    std::array<unsigned char, 0x400> actor_info{};
    std::array<uintptr_t, 3> actor_rows{}, group_rows{};
    std::array<uintptr_t, 64 * 3> status_rows{};
    std::array<int32_t, 64> slots{};
    std::array<uint16_t, 16> selectors{};
    uintptr_t actor_global=0, group_global=0, status_global=0;

    template<class T> static void Put(uintptr_t address,T value) {
        std::memcpy(reinterpret_cast<void*>(address),&value,sizeof(value));
    }
    template<class T> static uintptr_t Address(T& value) {
        return reinterpret_cast<uintptr_t>(value.data());
    }
    void SetCatalog(uintptr_t owner, uintptr_t marker, uint16_t selector=24,
                    uint32_t native_index=24, std::size_t slot=14,
                    uintptr_t server_vtable=0, uintptr_t control_slot=0, uintptr_t status_vtable=0) {
        actor_global=Address(actor_registry);group_global=Address(group_registry);status_global=Address(status_registry);
        actor_rows[1]=Address(actor_info);group_rows[1]=Address(group);status_rows[selector * 3 + 1]=Address(status);
        Put(actor_global+0x28,uint32_t{1});Put(group_global+0x28,uint32_t{1});Put(status_global+0x28,uint32_t{selector+1U});
        Put(actor_global+0x20,Address(actor_rows));Put(group_global+0x20,Address(group_rows));Put(status_global+0x20,Address(status_rows));
        Put(owner,marker);Put(marker+0x30,uint16_t{0});Put(Address(actor_info)+0x3a6,uint16_t{0});
        Put(Address(group)+8,uint32_t{1000001});Put(Address(status)+8,uint32_t{1000078});
        Put(Address(status)+0x1a,uint8_t{2});Put(Address(status)+0x1b,uint8_t{0x7f});
        Put(Address(status)+0x2c,native_index);
        slots.fill(-1);slots[native_index]=static_cast<int32_t>(slot);selectors[slot]=selector;
        Put(Address(group)+0x50,Address(slots));Put(Address(group)+0x58,native_index+1U);
        Put(Address(group)+0x10,Address(selectors));Put(Address(group)+0x18,static_cast<uint32_t>(slot+1U));
        {
            std::lock_guard lock(g_state_mutex);
            psm::authority::SynchronizeLocked();
        }
        SetPlayerOxygenEnabled(true,{reinterpret_cast<uintptr_t>(&actor_global),
            reinterpret_cast<uintptr_t>(&group_global),reinterpret_cast<uintptr_t>(&status_global)},server_vtable,control_slot,status_vtable);
    }
};
