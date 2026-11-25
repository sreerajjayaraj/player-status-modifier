#include "runtime/ship_trace.h"
#include "runtime/ship_proof.h"
#include "runtime/runtime_state.h"
#include "logger.h"
#include <Windows.h>
#include <mutex>
#include <limits>
#include <array>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <immintrin.h>

namespace psm::shiptrace {
namespace {
template<class T>bool Read(uintptr_t p,size_t o,T*out){if(!out||p<kMinimumPointerAddress||o>UINTPTR_MAX-p||sizeof(T)>UINTPTR_MAX-p-o)return false;__try{*out=*reinterpret_cast<const T*>(p+o);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
bool Dock(uintptr_t table,uint32_t*out){uintptr_t c=0;return Read(table,0x170,&c)&&(!c?(*out=0,true):Read(c,0x18,out));}
bool Owner(uintptr_t table,uintptr_t*out){uintptr_t c=0;return Read(table,0x20,&c)&&(!c?(*out=0,true):Read(c,0x18,out));}
enum class Failure:unsigned {None,ExpectedPlayer,PlayerChain,HudRideMismatch,MissingRide,SelfRide,RideUid,RideChain,Torn,NoVehicle,NoIdentity,NotHealth,RawAuthority,RawRead,RawTorn,RawBusy,Count};
enum class Callback:unsigned {Actor,Unridden,Info,Query,Damage,Writer,PlayerCapture,Count};
constexpr const char* failureNames[]={"none","expected-player-mismatch","unreadable-player-chain","hud-ride-uid-mismatch","missing-ride-actor","self-ride-actor","ride-actor-uid-mismatch","unreadable-ride-chain","torn-identity","no-vehicle-or-dock-context","identity-not-seeded","nonhealth-selector","raw-authority-unavailable-or-mismatch","raw-chain-unreadable","raw-chain-torn","raw-sample-busy"};
constexpr auto failureCount=static_cast<unsigned>(Failure::Count),callbackCount=static_cast<unsigned>(Callback::Count);
static_assert(std::size(failureNames)==failureCount);
std::array<std::atomic<uint32_t>,failureCount> failures{};std::array<std::atomic<uint32_t>,callbackCount> callbacks{};
void Increment(std::atomic<uint32_t>&x){auto n=x.load(std::memory_order_relaxed);for(unsigned tries=0;tries<4&&n!=UINT32_MAX;++tries)if(x.compare_exchange_weak(n,n+1,std::memory_order_relaxed))return;}
void Reject(Failure f){Increment(failures[static_cast<unsigned>(f)]);}void Arrive(Callback c){Increment(callbacks[static_cast<unsigned>(c)]);}
bool Fail(Failure*f,Failure value){if(f)*f=value;return false;}
template<class T>bool IdentityField(uintptr_t p,size_t offset,T*out,IdentityReadDetail*d,const char*field,unsigned bit){
 if(!Read(p,offset,out)){if(d){d->field=field;d->base=p;d->offset=static_cast<uint32_t>(offset);}return false;}if(d)d->read_mask|=uint64_t{1}<<bit;return true;
}
bool Once(uintptr_t controller,uintptr_t player,uintptr_t ride,Identity*out,Failure*why,IdentityReadDetail*d=nullptr){
 Identity v{};v.controller=controller;v.player=player;v.ride_actor=ride;uintptr_t t=0,vc=0,meta=0,component=0;
 if(d)*d={};auto bad=[&](Failure f,const char*field=nullptr,uintptr_t base=0,uint32_t off=0){if(d){d->observed=v;if(field){d->field=field;d->base=base;d->offset=off;}}return Fail(why,f);};
 if(!IdentityField(controller,0x370,&v.hud_uid,d,"hud-ride-uid-read",0)||!IdentityField(player,0x60,&v.player_uid,d,"player-uid-read",1))return bad(Failure::PlayerChain);
 if(!v.player_uid)return bad(Failure::PlayerChain,"player-uid-zero",player,0x60);
 if(!IdentityField(player,0x68,&t,d,"player-table-read",2)||!IdentityField(player,0x88,&meta,d,"player-metadata-read",3)||!IdentityField(meta,1,&v.player_category,d,"player-category-read",4)||!IdentityField(t,0x170,&component,d,"player-dock-component-read",5)||(component&&!IdentityField(component,0x18,&v.player_dock_uid,d,"player-dock-uid-read",6))||!IdentityField(t,0x20,&component,d,"player-status-component-read",7)||(component&&!IdentityField(component,0x18,&v.player_owner,d,"player-status-owner-read",8))||!IdentityField(t,0x48,&vc,d,"player-vehicle-component-read",9))return bad(Failure::PlayerChain);
 if(vc&&(!IdentityField(vc,0x18,&v.ride_record,d,"player-ride-record-read",10)||(v.ride_record&&!IdentityField(v.ride_record,0x10,&v.ride_uid,d,"player-ride-uid-read",11))))return bad(Failure::PlayerChain);
 if(v.ride_uid!=v.hud_uid)return bad(Failure::HudRideMismatch,"hud-ride-uid-mismatch",controller,0x370);
 if(!ride){if(v.ride_uid)return bad(Failure::MissingRide,"missing-ride-actor");*out=v;if(d)d->observed=v;return true;}
 if(ride==player)return bad(Failure::SelfRide,"self-ride-actor");if(!v.ride_uid)return bad(Failure::MissingRide,"player-has-no-ride-uid");
 uint32_t actor_uid=0;if(!IdentityField(ride,0x60,&actor_uid,d,"ride-actor-uid-read",12))return bad(Failure::RideUid);
 if(actor_uid!=v.ride_uid)return bad(Failure::RideUid,"ride-actor-uid-mismatch",ride,0x60);
 if(!IdentityField(ride,0x68,&t,d,"ride-table-read",13)||!IdentityField(ride,0x88,&meta,d,"ride-metadata-read",14)||!IdentityField(meta,1,&v.ride_category,d,"ride-category-read",15)||!IdentityField(t,0x170,&component,d,"ride-dock-component-read",16)||(component&&!IdentityField(component,0x18,&v.ride_dock_uid,d,"ride-dock-uid-read",17))||!IdentityField(t,0x20,&component,d,"ride-status-component-read",18)||(component&&!IdentityField(component,0x18,&v.ride_owner,d,"ride-status-owner-read",19)))return bad(Failure::RideChain);
 *out=v;if(d)d->observed=v;return true;
}
bool IdentityDetailedWithRead(uintptr_t c,uintptr_t p,uintptr_t r,uintptr_t expected,Identity*out,Failure*why,IdentityReadDetail*d){
 if(d)*d={};if(!out||!expected||p!=expected){if(d)d->field="expected-player-mismatch";return Fail(why,Failure::ExpectedPlayer);}Identity first{},second{};if(!Once(c,p,r,&first,why,d)||!Once(c,p,r,&second,why,d))return false;
 if(first!=second){if(d)d->field="identity-changed-during-read";return Fail(why,Failure::Torn);}*out=first;if(why)*why=Failure::None;return true;
}
bool IdentityDetailed(uintptr_t c,uintptr_t p,uintptr_t r,uintptr_t expected,Identity*out,Failure*why){return IdentityDetailedWithRead(c,p,r,expected,out,why,nullptr);}

bool Live(const Identity&id){Identity now{};return id.player&&ReadIdentity(id.controller,id.player,id.ride_actor,id.player,&now)&&now==id;}
std::mutex trace_mutex;
uint64_t RealClock(){return GetTickCount64();}
std::atomic<Clock> clock_fn{RealClock};uint64_t Now(){return clock_fn.load(std::memory_order_acquire)();}
std::atomic<bool> enabled{false};
Identity identity{};Health ride_health{};Budget actors{5000},infos,queries,damages,heals,writers,healing_writers;CaptureWindow window{};unsigned generation=0;
uintptr_t CurrentPlayer(){std::unique_lock lock(g_state_mutex,std::try_to_lock);return lock.owns_lock()?g_player_resolve.actor:0;}

// Passive callback evidence does not grant functional authority.
struct Authority {uintptr_t actor=0,marker=0;};
bool CurrentAuthority(Authority*out){std::unique_lock lock(g_state_mutex,std::try_to_lock);if(!out||!lock.owns_lock())return false;*out={g_player_resolve.actor,g_player_resolve.marker};return out->actor>=kMinimumPointerAddress&&out->marker>=kMinimumPointerAddress;}
// Read only fields used by the exact native contracts. actor+8 is a reference
// operand, never followed or accepted as an alternate player actor.
struct DirectFields {
 uintptr_t reference=0,owner=0;uint32_t uid=0,ride=0,dock=0,mask=0;uint8_t category=0;
 bool operator==(const DirectFields&)const=default;
};
DirectFields Direct(uintptr_t actor){
 DirectFields x{};uintptr_t table=0,meta=0,v=0,record=0;
 if(Read(actor,8,&x.reference))x.mask|=1;
 if(Read(actor,0x60,&x.uid))x.mask|=2;
 if(Read(actor,0x68,&table)){x.mask|=4;if(Owner(table,&x.owner))x.mask|=8;
  if(Dock(table,&x.dock))x.mask|=16;
  if(Read(table,0x48,&v)&&(!v||(Read(v,0x18,&record)&&(!record||Read(record,0x10,&x.ride)))))x.mask|=32;
 }
 if(Read(actor,0x88,&meta)&&Read(meta,1,&x.category))x.mask|=64;
 return x;
}
struct CallbackIdentity {
 uintptr_t controller=0,player=0,ride=0,expected=0,marker=0,info_record=0;
 DirectFields p{},r{},a{};VehicleInfo info{};uint32_t hud=0,mask=0;
 bool operator==(const CallbackIdentity&v)const{
  return controller==v.controller&&player==v.player&&ride==v.ride&&expected==v.expected&&marker==v.marker&&
   info_record==v.info_record&&p==v.p&&r==v.r&&a==v.a&&info.external_key==v.info.external_key&&info.type_hash==v.info.type_hash&&info.platform_vehicle==v.info.platform_vehicle&&info.blocked==v.info.blocked&&hud==v.hud&&mask==v.mask;
 }
};
struct CallbackSlot {CallbackIdentity sample{},reported{};uint64_t sampled=UINT64_MAX,last_report=0;bool present=false,reported_once=false;};
std::array<CallbackSlot,3> callback_slots{};unsigned callback_transition_reports=0,callback_periodic_reports=0;
CallbackIdentity CallbackOnce(uintptr_t c,uintptr_t p,uintptr_t r,uintptr_t record){
 CallbackIdentity x{};x.controller=c;x.player=p;x.ride=r;x.info_record=record;
 x.p=Direct(p);x.r=Direct(r);Authority a{};
 if(CurrentAuthority(&a)){x.expected=a.actor;x.marker=a.marker;x.a=Direct(a.actor);x.mask|=1;}
 if(Read(c,0x370,&x.hud))x.mask|=2;
 if(record&&Read(record,8,&x.info.external_key)&&Read(record,0x40,&x.info.type_hash)&&Read(record,0x0D,&x.info.platform_vehicle)&&Read(record,0x0A,&x.info.blocked))x.mask|=4;
 return x;
}
void SampleCallback(uintptr_t c,uintptr_t p,uintptr_t r,uintptr_t record,unsigned origin){
 auto&slot=callback_slots[origin];const auto now=Now();
 if(slot.sampled!=UINT64_MAX&&(now<slot.sampled||now-slot.sampled<1000))return;
 slot.sampled=now;const auto a=CallbackOnce(c,p,r,record),b=CallbackOnce(c,p,r,record);
 if(a!=b)return;slot.sample=a;slot.present=true;
}
void CallbackHeartbeat(){
 static constexpr const char*names[]={"mounted","unridden","vehicle-info"};
 for(unsigned i=0;i<callback_slots.size();++i){CallbackIdentity x{};bool emit=false;
  {std::unique_lock lock(trace_mutex,std::try_to_lock);if(!lock.owns_lock()||!enabled.load(std::memory_order_acquire))return;
   auto&s=callback_slots[i];const auto now=Now();if(!s.present)continue;
   if(!s.reported_once){s.reported_once=true;emit=true;}
   else if(s.sample!=s.reported&&callback_transition_reports<20&&now>=s.last_report&&now-s.last_report>=1000){++callback_transition_reports;emit=true;}
   else if(callback_periodic_reports<9&&now>=s.last_report&&now-s.last_report>=5000){++callback_periodic_reports;emit=true;}
   if(emit){s.reported=s.sample;s.last_report=now;x=s.sample;}
  }
  if(emit)Log("ship-trace: callback-identity origin=%s controller=0x%p callbackPlayer=0x%p expectedPlayer=0x%p expectedMarker=0x%p rideActor=0x%p callbackUID=%u expectedUID=%u rideUID=%u hudUID=%u callbackRideUID=%u expectedRideUID=%u callbackOwner=0x%p expectedOwner=0x%p rideOwner=0x%p callbackReferenceRaw=0x%p callbackFields=%X expectedFields=%X rideFields=%X packetFields=%X playerEqual=%d externalVehicleKey=%u typeHash=%u platformVehicle=%u nativeBlocked=%u ship-binding-unproved=1 diagnostic-no-mutation=1",
   names[i],reinterpret_cast<void*>(x.controller),reinterpret_cast<void*>(x.player),reinterpret_cast<void*>(x.expected),reinterpret_cast<void*>(x.marker),reinterpret_cast<void*>(x.ride),x.p.uid,x.a.uid,x.r.uid,x.hud,x.p.ride,x.a.ride,reinterpret_cast<void*>(x.p.owner),reinterpret_cast<void*>(x.a.owner),reinterpret_cast<void*>(x.r.owner),reinterpret_cast<void*>(x.p.reference),x.p.mask,x.a.mask,x.r.mask,x.mask,x.player==x.expected?1:0,x.info.external_key,x.info.type_hash,static_cast<unsigned>(x.info.platform_vehicle),static_cast<unsigned>(x.info.blocked));
 }
}

bool Ready(){
 if(!enabled.load(std::memory_order_acquire))return false;if(!identity.player){Reject(Failure::NoIdentity);return false;}
 const auto now=Now();const bool expired=!window.Active(now);
 const bool player_changed=CurrentPlayer()!=identity.player;
 if(expired||player_changed||!Live(identity)){
  identity={};ride_health={};++generation;
  if(actors.Take(now,true))Log("ship-trace: identity invalidated generation=%u reason=%s",generation,expired?"diagnostic-duration-expired":player_changed?"current-player-changed":"ride-or-docking-state-changed");
  return false;
 }
 return true;
}
bool HaveVehicleContext(){return identity.ride_uid||identity.player_dock_uid;}
struct RawPlayer {uintptr_t actor=0,marker=0,table=0,ride_record=0;uint32_t uid=0,ride=0,dock=0;bool operator==(const RawPlayer&)const=default;};
struct Probe {RawPlayer raw{},reported_raw{};uint64_t sampled=0,last_report=0;bool waiting=false,player_seen=false;unsigned idle_reports=0,periodic_reports=0,transition_reports=0;uint32_t arrival_mask=0,failure_mask=0;};Probe probe{};
bool SameRawValues(const RawPlayer&a,const RawPlayer&b){return a.actor==b.actor&&a.marker==b.marker&&a.uid==b.uid&&a.ride==b.ride&&a.dock==b.dock;}
std::atomic<uint64_t> raw_last_probe{UINT64_MAX},probe_epoch{0};
bool RawAuthority(uintptr_t actor,uintptr_t marker){std::unique_lock lock(g_state_mutex,std::try_to_lock);return lock.owns_lock()&&actor>=kMinimumPointerAddress&&marker>=kMinimumPointerAddress&&g_player_resolve.actor==actor&&g_player_resolve.marker==marker;}
bool RawOnce(uintptr_t actor,uintptr_t marker,RawPlayer*out){RawPlayer v{};v.actor=actor;v.marker=marker;uintptr_t vehicle=0;uint32_t again=0;return Read(actor,0x60,&v.uid)&&v.uid&&Read(actor,0x68,&v.table)&&Read(v.table,0x48,&vehicle)&&(!vehicle||(Read(vehicle,0x18,&v.ride_record)&&(!v.ride_record||Read(v.ride_record,0x10,&v.ride))))&&Dock(v.table,&v.dock)&&Read(actor,0x60,&again)&&again==v.uid?(*out=v,true):false;}
void ResetProbe(){for(auto&v:failures)v.store(0,std::memory_order_relaxed);for(auto&v:callbacks)v.store(0,std::memory_order_relaxed);probe={};raw_last_probe.store(UINT64_MAX,std::memory_order_relaxed);probe_epoch.fetch_add(1,std::memory_order_relaxed);}
const char* Role(uintptr_t owner){return owner&&owner==identity.ride_owner?"ride-status-owner":owner&&owner==identity.player_owner?"player-status-owner":"unbound-raw-target";}
}
bool ReadIdentity(uintptr_t c,uintptr_t p,uintptr_t r,uintptr_t expected,Identity*out){return IdentityDetailed(c,p,r,expected,out,nullptr);}
bool ReadIdentityDetailed(uintptr_t c,uintptr_t p,uintptr_t r,uintptr_t expected,Identity*out,IdentityReadDetail*d){return IdentityDetailedWithRead(c,p,r,expected,out,nullptr,d);}
bool ReadHealthDetailed(const Identity&id,uintptr_t owner,uint16_t selector,uintptr_t entry,Health*out,HealthReadDetail*d){
 if(d)*d={};auto fail=[&](const char*field,uintptr_t base=0,uint32_t offset=0){if(d){d->field=field;d->base=base;d->offset=offset;}return false;};
 if(!out||selector||!Live(id)||!owner||(owner!=id.player_owner&&owner!=id.ride_owner))return fail("health-owner-or-identity-mismatch");
 uintptr_t entries=0;uint16_t actual=0;Health h{};h.owner=owner;h.entry=entry;
 if(!Read(owner,0x58,&entries))return fail("health-array-read",owner,0x58);if(d){d->array=entries;d->read_mask|=1;}
 if(entries<kMinimumPointerAddress)return fail("health-array-invalid",owner,0x58);
 if(entry!=entries)return fail("health-entry-not-array-base",owner,0x58);
 if(!Read(entry,0,&actual))return fail("health-selector-read",entry,0);if(d){d->tag=actual;d->read_mask|=2;}
 if(actual!=selector)return fail("health-selector-mismatch",entry,0);
 if(!Read(entry,8,&h.value))return fail("health-value-read",entry,8);if(d)d->read_mask|=4;
 if(!Read(entry,0x18,&h.maximum))return fail("health-maximum-read",entry,0x18);if(d)d->read_mask|=8;
 if(!Live(id))return fail("health-identity-changed");uintptr_t again=0;
 if(!Read(owner,0x58,&again))return fail("health-array-recheck-read",owner,0x58);
 if(again!=entries)return fail("health-array-changed",owner,0x58);if(d)d->read_mask|=16;*out=h;return true;
}
bool ReadHealth(const Identity&id,uintptr_t owner,uint16_t selector,uintptr_t entry,Health*out){return ReadHealthDetailed(id,owner,selector,entry,out,nullptr);}

bool ReadVehicleInfo(const Identity&id,uintptr_t record,VehicleInfo*out){if(!out||!Live(id)||!id.ride_actor)return false;VehicleInfo a{},b{};if(!Read(record,8,&a.external_key)||!Read(record,0x40,&a.type_hash)||!Read(record,0x0D,&a.platform_vehicle)||!Read(record,0x0A,&a.blocked)||!Read(record,8,&b.external_key)||!Read(record,0x40,&b.type_hash)||!Read(record,0x0D,&b.platform_vehicle)||!Read(record,0x0A,&b.blocked)||a.blocked!=b.blocked||a.platform_vehicle!=b.platform_vehicle||a.external_key!=b.external_key||a.type_hash!=b.type_hash||!Live(id))return false;*out=a;return true;}
bool Budget::Take(uint64_t now,bool change){if(count_>=limit||(!change&&count_&&now>=last_&&now-last_<interval_)||now<last_)return false;++count_;last_=now;return true;}
void Enable(bool on,Clock clock){std::lock_guard lock(trace_mutex);clock_fn.store(clock?clock:RealClock,std::memory_order_release);enabled.store(false,std::memory_order_release);ResetProbe();psm::shipproof::Enable(on);callback_slots={};callback_transition_reports=0;callback_periodic_reports=0;identity={};ride_health={};actors=Budget{5000};infos=Budget{};queries=Budget{};damages=Budget{};heals=Budget{};writers=Budget{};healing_writers=Budget{};generation=0;window={};enabled.store(on,std::memory_order_release);if(on)Log("ship-trace: passive identity diagnostics; independent ShipHealing route requires authoritative current-ride/native vessel class/exact health query; no ship incoming/projectile modifiers; budget=128-per-kind max896-lines-plus-markers duration=900s-from-first-coherent-vehicle-or-deck-context initially-unarmed observer-summary-extra-max41 callback-identity-extra-max32 ship-healing-extra-max64 worker-only raw sources never dereferenced");}
void ObserveActor(uintptr_t c,uintptr_t p,uintptr_t r,ActorOrigin origin,uintptr_t native_frame){
 if(!enabled.load(std::memory_order_acquire))return;Arrive(origin==ActorOrigin::Unridden?Callback::Unridden:Callback::Actor);
 psm::shipproof::Actor(c,r,native_frame);
 std::unique_lock lock(trace_mutex,std::try_to_lock);if(!lock.owns_lock()||!enabled.load(std::memory_order_acquire))return;const auto now=Now();
 SampleCallback(c,p,r,0,origin==ActorOrigin::Unridden?1:0);
 // An expired or backwards clock is terminal for this Enable session. A new
 // HUD callback must not revive it. Before arming, idle samples use no budget.
 if(window.Armed()&&!window.Active(now)){
  if(identity.player){identity={};ride_health={};++generation;if(actors.Take(now,true))Log("ship-trace: identity invalidated generation=%u reason=diagnostic-duration-expired",generation);}
  return;
 }
 Identity next{};Failure why=Failure::None;
 if(!IdentityDetailed(c,p,r,CurrentPlayer(),&next,&why)){
  Reject(why);
  if(identity.player){identity={};ride_health={};++generation;if(actors.Take(now,true))Log("ship-trace: identity invalidated generation=%u reason=unreadable-stale-or-player-changed",generation);}
  return;
 }
 if(!window.Armed()){
  if(!next.ride_uid&&!next.player_dock_uid){Reject(Failure::NoVehicle);return;}
  if(!window.Arm(now))return;
  Log("ship-trace: capture armed on first coherent vehicle/deck context playerUID=%u rideUID=%u playerDockParentUID=%u duration=900s budgets-fresh=1 no-ship-modifiers=1",next.player_uid,next.ride_uid,next.player_dock_uid);
 }
 const bool changed=next!=identity;if(changed){identity=next;ride_health={};++generation;}
 if(!actors.Take(now,changed))return;
 Log("ship-trace: identity generation=%u controller=0x%p player=0x%p playerUID=%u playerCategory=%u playerStatusOwner=0x%p playerDockParentUID=%u rideActor=0x%p rideUID=%u hudUID=%u rideCategory=%u rideStatusOwner=0x%p rideDockParentUID=%u role=unproved-operation no-native-writes=1",generation,reinterpret_cast<void*>(c),reinterpret_cast<void*>(p),next.player_uid,next.player_category,reinterpret_cast<void*>(next.player_owner),next.player_dock_uid,reinterpret_cast<void*>(r),next.ride_uid,next.hud_uid,next.ride_category,reinterpret_cast<void*>(next.ride_owner),next.ride_dock_uid);
}
void ObserveVehicleInfo(uintptr_t c,uintptr_t p,uintptr_t r,uintptr_t record,uint16_t native_vehicle_id,uintptr_t native_frame){if(!enabled.load(std::memory_order_acquire))return;Arrive(Callback::Info);psm::shipproof::Info(c,r,record,native_vehicle_id,native_frame);std::unique_lock lock(trace_mutex,std::try_to_lock);if(!lock.owns_lock()||!enabled.load(std::memory_order_acquire))return;SampleCallback(c,p,r,record,2);VehicleInfo v{};if(!Ready()||identity.controller!=c||identity.player!=p||identity.ride_actor!=r||!ReadVehicleInfo(identity,record,&v)||!infos.Take(Now()))return;Log("ship-trace: native-vehicle-info generation=%u rideUID=%u rideActor=0x%p record=0x%p externalVehicleKey=%u typeHash=%u source=native-getter-return operation-unproved=1",generation,identity.ride_uid,reinterpret_cast<void*>(r),reinterpret_cast<void*>(record),v.external_key,v.type_hash);}
void ObserveQuery(uintptr_t owner,uint16_t selector,uintptr_t entry){
 if(!enabled.load(std::memory_order_acquire))return;Arrive(Callback::Query);if(selector){Reject(Failure::NotHealth);return;}
 psm::shipproof::Query(owner,selector,entry);
 std::unique_lock lock(trace_mutex,std::try_to_lock);if(!lock.owns_lock()||!enabled.load(std::memory_order_acquire))return;if(!Ready()||!HaveVehicleContext())return;
 Health h{};const bool exact=ReadHealth(identity,owner,selector,entry,&h);
 if(exact&&owner==identity.ride_owner)ride_health=h;
 if(!queries.Take(Now()))return;
 Log("ship-trace: native-health-query generation=%u rideUID=%u playerDockParentUID=%u ownerRaw=0x%p role=%s entryRaw=0x%p selector=%u exactOwnerArrayBase=%d current=%lld baseMaximum=%lld source=native-query hull-binding-unproved=1",generation,identity.ride_uid,identity.player_dock_uid,reinterpret_cast<void*>(owner),Role(owner),reinterpret_cast<void*>(entry),selector,exact?1:0,static_cast<long long>(h.value),static_cast<long long>(h.maximum));
}
void ObserveDamage(uintptr_t target,uint16_t selector,int64_t delta,uintptr_t source,uintptr_t caller){if(!enabled.load(std::memory_order_acquire))return;Arrive(Callback::Damage);if(selector){Reject(Failure::NotHealth);return;}std::lock_guard lock(trace_mutex);if(!Ready()||!HaveVehicleContext()||!(delta<0?damages:heals).Take(Now(),delta<0))return;Log("ship-trace: native-status-receipt generation=%u rideUID=%u playerDockParentUID=%u targetRaw=0x%p targetRelation=%s selector=%u signedDeltaBeforeBaseline=%lld sourceContextRaw=0x%p callerRaw=0x%p source-uninterpreted=1 diagnostic-no-mutation=1",generation,identity.ride_uid,identity.player_dock_uid,reinterpret_cast<void*>(target),Role(target),selector,static_cast<long long>(delta),reinterpret_cast<void*>(source),reinterpret_cast<void*>(caller));}
void ObserveWriter(uintptr_t entry,int64_t request,uintptr_t caller){if(!enabled.load(std::memory_order_acquire))return;Arrive(Callback::Writer);std::unique_lock lock(trace_mutex,std::try_to_lock);if(!lock.owns_lock()||!Ready()||!HaveVehicleContext())return;uint16_t selector=0;int64_t old=0;if(!Read(entry,0,&selector)||selector||!Read(entry,8,&old)||old==request||!(request<old?writers:healing_writers).Take(Now(),request<old))return;const bool exact=ride_health.entry==entry&&ReadHealth(identity,ride_health.owner,0,entry,&ride_health);Log("ship-trace: native-pre-notification-writer generation=%u rideUID=%u playerDockParentUID=%u entry=0x%p selector=%u old=%lld nativeRequestBeforeBaseline=%lld callerRaw=0x%p exactRideQueryEntry=%d hull-binding-unproved=1 diagnostic-no-mutation=1",generation,identity.ride_uid,identity.player_dock_uid,reinterpret_cast<void*>(entry),selector,static_cast<long long>(old),static_cast<long long>(request),reinterpret_cast<void*>(caller),exact?1:0);}
void NoteAuthoritativePlayerCapture(){if(enabled.load(std::memory_order_acquire))Arrive(Callback::PlayerCapture);}
void ObserveAuthoritativePlayer(uintptr_t actor,uintptr_t marker){
 if(!enabled.load(std::memory_order_acquire))return;
 struct RestoreStatus{DWORD error=GetLastError();unsigned csr=_mm_getcsr();~RestoreStatus(){_mm_setcsr(csr);SetLastError(error);}} restore;
 const auto epoch=probe_epoch.load(std::memory_order_relaxed),now=Now();auto previous=raw_last_probe.load(std::memory_order_relaxed);
 if(previous!=UINT64_MAX&&now>=previous&&now-previous<250)return;
 if(!raw_last_probe.compare_exchange_strong(previous,now,std::memory_order_relaxed))return;
 if(!RawAuthority(actor,marker)){Reject(Failure::RawAuthority);return;}
 RawPlayer first{},second{};if(!RawOnce(actor,marker,&first)||!RawOnce(actor,marker,&second)){Reject(Failure::RawRead);return;}
 if(first!=second){Reject(Failure::RawTorn);return;}if(!RawAuthority(actor,marker)){Reject(Failure::RawAuthority);return;}
 std::unique_lock lock(trace_mutex,std::try_to_lock);if(!lock.owns_lock()){Reject(Failure::RawBusy);return;}
 if(!enabled.load(std::memory_order_acquire)||epoch!=probe_epoch.load(std::memory_order_relaxed))return;
 probe.raw=first;probe.sampled=now;
}
void Heartbeat(){
 if(!enabled.load(std::memory_order_acquire))return;
 psm::shipproof::Heartbeat(Now());
 CallbackHeartbeat();
 const auto now=Now();std::array<uint32_t,callbackCount> c{};std::array<uint32_t,failureCount> f{};RawPlayer raw{};uint64_t age=UINT64_MAX;bool initial=false,armed=false,seeded=false;uint32_t rejectsMask=0;const char*state="waiting-no-authoritative-sample";
 {std::unique_lock lock(trace_mutex,std::try_to_lock);if(!lock.owns_lock()||!enabled.load(std::memory_order_acquire))return;
  uint32_t arrivalMask=0,failureMask=0;for(unsigned i=0;i<callbackCount;++i){c[i]=callbacks[i].load(std::memory_order_relaxed);if(c[i])arrivalMask|=uint32_t{1}<<i;}for(unsigned i=0;i<failureCount;++i){f[i]=failures[i].load(std::memory_order_relaxed);if(f[i])failureMask|=uint32_t{1}<<i;}
  rejectsMask=failureMask;initial=!probe.waiting;const bool firstPlayer=!probe.player_seen&&probe.raw.uid;
  const bool changed=(arrivalMask&~probe.arrival_mask)||(failureMask&~probe.failure_mask)||(probe.raw.uid&&!SameRawValues(probe.raw,probe.reported_raw));
  if(initial){probe.waiting=true;}
  else if(firstPlayer){probe.player_seen=true;}
  else if(changed&&probe.transition_reports<15&&now>=probe.last_report&&now-probe.last_report>=1000){++probe.transition_reports;}
  else if(probe.player_seen){if(probe.periodic_reports>=16||now<probe.last_report||now-probe.last_report<5000)return;++probe.periodic_reports;}
  else {if(probe.idle_reports>=8||now<probe.last_report||now-probe.last_report<(uint64_t{5000}<<probe.idle_reports))return;++probe.idle_reports;}
  // First coherent player evidence has its own reserved summary even if ride
  // and docking UIDs are zero. Neither this nor raw changes arm CaptureWindow.
  if(probe.raw.uid)probe.player_seen=true;probe.reported_raw=probe.raw;probe.arrival_mask=arrivalMask;probe.failure_mask=failureMask;
  probe.last_report=now;raw=probe.raw;age=raw.uid&&now>=probe.sampled?now-probe.sampled:UINT64_MAX;armed=window.Armed();seeded=identity.player!=0;if(probe.player_seen)state="authoritative-player-sample-only";
 }
 char reasons[256]{};size_t used=0;for(unsigned i=1;i<failureCount;++i)if(f[i]){const auto n=std::snprintf(reasons+used,sizeof(reasons)-used,"%s%s:%u",used?",":"",failureNames[i],f[i]);if(n<0||static_cast<size_t>(n)>=sizeof(reasons)-used)break;used+=static_cast<size_t>(n);}
 Log("ship-trace: observer-%s state=%s captureArmed=%d identitySeeded=%d actorCalls=%u unriddenCalls=%u infoCalls=%u queryCalls=%u damageCalls=%u writerCalls=%u authoritativeCalls=%u rawPlayer=0x%p rawPlayerUID=%u rawRideUID=%u rawDockUID=%u rawSampleAgeMs=%llu rejectMask=%X rejects=%s counters-approx-saturating=1 sample-only-no-hull-binding=1 no-new-native-writes=1",initial?"waiting":"heartbeat",state,armed?1:0,seeded?1:0,c[0],c[1],c[2],c[3],c[4],c[5],c[6],reinterpret_cast<void*>(raw.actor),raw.uid,raw.ride,raw.dock,static_cast<unsigned long long>(age),rejectsMask,used?reasons:"none");
}


void ConfigureNativeMetadataCatalogs(uintptr_t character,uintptr_t gimmick,uintptr_t vehicle,
                                     std::array<uintptr_t,2> server_vtables){
 psm::shipproof::Configure(character,gimmick,vehicle,server_vtables);
}
bool IsObservedOccupiedShipHealthEntry(uintptr_t entry){return psm::shipproof::PreserveNative(entry);}
bool TryAdjustOccupiedShipHealthWrite(uintptr_t entry,int64_t native,double multiplier,int64_t*out){return psm::shipproof::Adjust(entry,native,multiplier,out);}
}
