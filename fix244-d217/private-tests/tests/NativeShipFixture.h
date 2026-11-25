#pragma once
#include "runtime/ship_trace.h"
#include "runtime/runtime_state.h"
#include <array>
#include <cstring>
namespace shiptest {
template<class T>uintptr_t P(T&a){return reinterpret_cast<uintptr_t>(a.data());}
template<size_t N,class T>void Put(std::array<unsigned char,N>&a,size_t o,T v){std::memcpy(a.data()+o,&v,sizeof v);}
struct Actor {
 std::array<unsigned char,0x400>a{},table{},vehicle{},ride{},dock{},status{},owner{},entries{},meta{},gimmick{};
 Actor(uint32_t uid,uint8_t category){Put(a,0x60,uid);Put(a,0x68,P(table));Put(a,0x88,P(meta));meta[1]=category;Put(table,0x48,P(vehicle));Put(vehicle,0x18,P(ride));Put(table,0x170,P(dock));Put(table,0x20,P(status));Put(table,0x30,P(gimmick));Put(status,8,P(a));Put(status,0x18,P(owner));Put(owner,0,P(status));Put(status,0x30,uint16_t{77});Put(gimmick,0x48,uint16_t{123});Put(owner,0x58,P(entries));Put(entries,0,uint16_t{0});Put(entries,8,int64_t{100});Put(entries,0x18,int64_t{1000});}
};
struct Fixture {
 Actor player{10,1},hudPlayer{99,1},ship{200,7},enemy{201,7};
 std::array<unsigned char,0x400>hud{},info{},otherInfo{},frame{},otherFrame{},characterManager{},gimmickManager{},vehicleManager{},characterRow{},gimmickRow{},otherRow{};
 std::array<uintptr_t,512 * 3>characterCache{},gimmickCache{},vehicleCache{};
 uintptr_t characterGlobal=0,gimmickGlobal=0,vehicleGlobal=0;
 Fixture(){Put(player.ride,0x10,uint32_t{200});Put(hud,0x370,uint32_t{200});Put(info,8,uint16_t{16970});Put(info,0x0D,uint8_t{1});Put(info,0x40,uint32_t{0x4798223F});Put(frame,0x1A0,uint16_t{3});
  Put(characterManager,0x28,uint32_t{512});Put(characterManager,0x20,P(characterCache));Put(gimmickManager,0x28,uint32_t{512});Put(gimmickManager,0x20,P(gimmickCache));Put(vehicleManager,0x28,uint32_t{512});Put(vehicleManager,0x20,P(vehicleCache));characterCache[77 * 3 + 1]=P(characterRow);gimmickCache[123 * 3 + 1]=P(gimmickRow);vehicleCache[3 * 3 + 1]=P(info);Put(characterRow,0xA0,uint16_t{3});Put(gimmickRow,0x2E8,uint16_t{3});characterGlobal=P(characterManager);gimmickGlobal=P(gimmickManager);vehicleGlobal=P(vehicleManager);
  ActorResolveSnapshot a{};a.actor=P(player.table);a.marker=P(player.status);a.root=a.resource_owner=a.damage_source=a.damage_target=P(player.owner);a.health_entry=P(player.entries);a.stamina_entry=P(player.entries)+0x90;a.spirit_entry=P(player.entries)+0x120;a.stamina_type=19;a.spirit_type=20;{std::lock_guard lock(g_state_mutex);AssignTrackedPlayerResolveLocked(a);}
#ifdef PSM_SHIP_PROOF_V2
 psm::shiptrace::ConfigureNativeMetadataCatalogs(reinterpret_cast<uintptr_t>(&characterGlobal),reinterpret_cast<uintptr_t>(&gimmickGlobal),reinterpret_cast<uintptr_t>(&vehicleGlobal));
#endif
 }
 void ActorCall(uintptr_t whichFrame=0){
#ifdef PSM_SHIP_PROOF_V2
 psm::shiptrace::ObserveActor(P(hud),P(hudPlayer.a),P(ship.a),psm::shiptrace::ActorOrigin::Mounted,whichFrame?whichFrame:P(frame));
#else
 (void)whichFrame;psm::shiptrace::ObserveActor(P(hud),P(hudPlayer.a),P(ship.a));
#endif
 }
 void InfoCall(uintptr_t whichFrame=0){
#ifdef PSM_SHIP_PROOF_V2
 psm::shiptrace::ObserveVehicleInfo(P(hud),P(hudPlayer.a),P(ship.a),P(info),3,whichFrame?whichFrame:P(frame));
#else
 (void)whichFrame;psm::shiptrace::ObserveVehicleInfo(P(hud),P(hudPlayer.a),P(ship.a),P(info));
#endif
 }
 void Query(){psm::shiptrace::ObserveQuery(P(ship.owner),0,P(ship.entries));}
 void Bind(){ActorCall();InfoCall();Query();}
 bool Write(int64_t native,int64_t*out){*out=native;return psm::shiptrace::TryAdjustOccupiedShipHealthWrite(P(ship.entries),native,100,out)||psm::shiptrace::IsObservedOccupiedShipHealthEntry(P(ship.entries));}
};
}
