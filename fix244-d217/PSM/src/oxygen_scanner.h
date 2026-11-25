#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace psm::oxygen {
struct Catalog {
 uintptr_t actors=0, groups=0, statuses=0;
 explicit operator bool() const { return actors&&groups&&statuses; }
};
inline constexpr std::array<std::uint8_t,28> kGaugeSampleWitness{
 0x48,0x8B,0x86,0xB0,0x01,0x00,0x00,0xC5,0xF8,0x57,0xC0,0xC4,0xE1,0xFA,
 0x2A,0x80,0xE8,0x00,0x00,0x00,0xC5,0xFA,0x5E,0xD6,0xC5,0xF0,0x57,0xC9
};
struct Sites {
 uintptr_t breath_before_call=0, stats_after_lookup=0;
 Catalog catalog{};
 uintptr_t server_actor_vtable=0, gauge_sample=0, client_control_slot=0;
 uintptr_t server_status_vtable=0;
 explicit operator bool() const { return bool(catalog) || (breath_before_call && stats_after_lookup); }
};
Sites Scan(const void* module, std::size_t size);
}
