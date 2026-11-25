#pragma once
#include "runtime/ship_trace.h"
#include <array>
#include <cstring>
namespace shiptest {
struct NativeCatalogFixture {
 std::array<unsigned char,0x400> frame{},character_manager{},gimmick_manager{},vehicle_manager{},character_row{},gimmick_row{};
 std::array<uintptr_t,512 * 3> character_cache{},gimmick_cache{},vehicle_cache{};
 uintptr_t character_global=0,gimmick_global=0,vehicle_global=0;
 template<class B>static uintptr_t Ptr(B&x){return reinterpret_cast<uintptr_t>(x.data());}
 template<class T,size_t N>static void Set(std::array<unsigned char,N>&x,size_t o,T v){std::memcpy(x.data()+o,&v,sizeof v);}
 explicit NativeCatalogFixture(uintptr_t record){
  Set(character_manager,0x28,uint32_t{512});Set(character_manager,0x20,Ptr(character_cache));Set(gimmick_manager,0x28,uint32_t{512});Set(gimmick_manager,0x20,Ptr(gimmick_cache));Set(vehicle_manager,0x28,uint32_t{512});Set(vehicle_manager,0x20,Ptr(vehicle_cache));character_cache[77 * 3 + 1]=Ptr(character_row);gimmick_cache[123 * 3 + 1]=Ptr(gimmick_row);vehicle_cache[3 * 3 + 1]=record;Set(character_row,0xA0,uint16_t{3});Set(gimmick_row,0x2E8,uint16_t{3});Set(frame,0x1A0,uint16_t{3});character_global=Ptr(character_manager);gimmick_global=Ptr(gimmick_manager);vehicle_global=Ptr(vehicle_manager);
  psm::shiptrace::ConfigureNativeMetadataCatalogs(reinterpret_cast<uintptr_t>(&character_global),reinterpret_cast<uintptr_t>(&gimmick_global),reinterpret_cast<uintptr_t>(&vehicle_global));
 }
 uintptr_t Frame(){return Ptr(frame);}
};
}
