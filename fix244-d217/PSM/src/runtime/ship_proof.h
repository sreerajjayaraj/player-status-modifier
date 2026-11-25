#pragma once
#include <array>
#include <cstdint>
namespace psm::shipproof {
void Configure(uintptr_t character_global,uintptr_t gimmick_global,uintptr_t vehicle_global,
               std::array<uintptr_t,2> server_vtables={});
void Enable(bool);
void Actor(uintptr_t controller,uintptr_t ride,uintptr_t native_frame);
void Info(uintptr_t controller,uintptr_t ride,uintptr_t record,uint16_t internal_vehicle_id,uintptr_t native_frame);
void Query(uintptr_t owner,uint16_t selector,uintptr_t entry);
bool Adjust(uintptr_t entry,int64_t native,double multiplier,int64_t*out);
bool PreserveNative(uintptr_t entry);
void Heartbeat(uint64_t now);
}
