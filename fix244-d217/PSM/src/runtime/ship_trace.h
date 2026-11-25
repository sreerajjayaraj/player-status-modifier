#pragma once
#define PSM_SHIP_OBSERVER_DIAGNOSTICS 1
#define PSM_OCCUPIED_SHIP_HEALING 1
#define PSM_SHIP_PROOF_V2 1
#include <array>
#include <atomic>
#include <cstdint>
namespace psm::shiptrace {
struct Identity {uintptr_t controller=0,player=0,ride_actor=0,ride_record=0,player_owner=0,ride_owner=0;uint32_t player_uid=0,ride_uid=0,hud_uid=0,player_dock_uid=0,ride_dock_uid=0;uint8_t player_category=0,ride_category=0;bool operator==(const Identity&)const=default;};
struct Health {uintptr_t owner=0,entry=0;int64_t value=0,maximum=0;};
struct VehicleInfo {uint16_t external_key=0;uint32_t type_hash=0;uint8_t platform_vehicle=0,blocked=0;};
// Fixed diagnostic payload from the same strict reads; never alternate authority.
struct IdentityReadDetail {const char*field="none";uintptr_t base=0;uint32_t offset=0;uint64_t read_mask=0;Identity observed{};uintptr_t player_status=0,ride_status=0;uint8_t player_dead=1,ride_dead=1,status_mask=0;};
struct HealthReadDetail {const char*field="none";uintptr_t base=0,array=0;uint32_t offset=0,read_mask=0;uint16_t tag=0;};
bool ReadIdentityDetailed(uintptr_t,uintptr_t,uintptr_t,uintptr_t,Identity*,IdentityReadDetail*);
bool ReadHealthDetailed(const Identity&,uintptr_t,uint16_t,uintptr_t,Health*,HealthReadDetail*);
bool ReadIdentity(uintptr_t,uintptr_t,uintptr_t,uintptr_t,Identity*);
bool ReadHealth(const Identity&,uintptr_t,uint16_t,uintptr_t,Health*);
bool ReadVehicleInfo(const Identity&,uintptr_t,VehicleInfo*);
class CaptureWindow {
 bool armed_=false,expired_=false;uint64_t started_=0;
public:
 static constexpr uint64_t duration=900000;
 bool Armed()const{return armed_;}
 bool Arm(uint64_t now){if(armed_||expired_)return false;armed_=true;started_=now;return true;}
 bool Active(uint64_t now){if(!armed_||expired_)return false;if(now<started_||now-started_>duration){expired_=true;return false;}return true;}
};
class Budget {unsigned count_=0;uint64_t last_=0,interval_=1000;public:static constexpr unsigned limit=128;explicit Budget(uint64_t interval=1000):interval_(interval){}bool Take(uint64_t,bool=false);};
using Clock=uint64_t(*)();
void Enable(bool,Clock=nullptr);
enum class ActorOrigin {Mounted,Unridden};
void ObserveActor(uintptr_t,uintptr_t,uintptr_t,ActorOrigin=ActorOrigin::Mounted,uintptr_t native_frame=0);
void NoteAuthoritativePlayerCapture();
void ObserveAuthoritativePlayer(uintptr_t actor,uintptr_t marker);
void Heartbeat();
void ObserveVehicleInfo(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uint16_t native_vehicle_id=0xFFFF,uintptr_t native_frame=0);
void ConfigureNativeMetadataCatalogs(uintptr_t,uintptr_t,uintptr_t,
                                     std::array<uintptr_t,2> server_vtables={});
void ObserveQuery(uintptr_t,uint16_t,uintptr_t);
void ObserveDamage(uintptr_t,uint16_t,int64_t,uintptr_t,uintptr_t);
void ObserveWriter(uintptr_t,int64_t,uintptr_t);
// True owns this exact current-ship entry and excludes legacy outgoing fallback.
// Only positive native requests scale; equal/decreasing requests remain native.
bool TryAdjustOccupiedShipHealthWrite(uintptr_t,int64_t,double,int64_t*);
// Native preservation requires a fresh coherent current proof. Definitive
// identity/metadata/array changes retire the observed epoch and use the ordinary route.
bool IsObservedOccupiedShipHealthEntry(uintptr_t);
}
