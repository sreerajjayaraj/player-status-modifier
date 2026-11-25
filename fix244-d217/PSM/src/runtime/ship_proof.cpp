#include "runtime/ship_proof.h"
#include "runtime/native_catalog_3663.h"
#include "runtime/ship_trace.h"
#include "runtime/runtime_state.h"
#include "runtime/atomic_words.h"
#include "logger.h"
#include <Windows.h>
#include <array>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <cmath>
#include <immintrin.h>
namespace psm::shipproof {
namespace {
using Authority=psm::authority::Token;
using Identity=psm::shiptrace::Identity;
using VehicleInfo=psm::shiptrace::VehicleInfo;
using Health=psm::shiptrace::Health;
template<class T>bool Read(uintptr_t p,size_t offset,T*out){
 if(!out||p<kMinimumPointerAddress||offset>UINTPTR_MAX-p||sizeof(T)>UINTPTR_MAX-p-offset)return false;
 __try{*out=*reinterpret_cast<const T*>(p+offset);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
struct RestoreStatus {DWORD error=GetLastError();unsigned csr=_mm_getcsr();~RestoreStatus(){_mm_setcsr(csr);SetLastError(error);}};
enum class Validity {Valid,Unknown,Invalid};
// Diagnostic state never grants or extends a proof. Callback updates are fixed
// counters/payload only; formatting and logging remain on the worker.
enum class Stage:unsigned {Actor,Info,Query,Count};
enum class Reason:unsigned {None,AuthorityUnavailable,AuthorityInvalid,NoRide,IdentityChain,Owners,PlayerDead,VesselDead,IdentityChanged,AuthorityChanged,Frame,Inputs,Category,InfoNoPending,InfoFrame,InfoContext,InfoSession,InfoIdentity,InfoInputs,SourceCache,SourceReference,VehicleCache,VehicleRow,VehicleRecord,MetadataChanged,QueryNoSelection,QueryOwner,QueryEntry,ProofLifetime,ProofIdentity,ProofAlias,ProofHealth,ProofArray,ProofChanged,QueryStamp,AuthorityBridge,Count};
constexpr const char* reason_names[]={"none","authority-unavailable","authority-invalid","no-current-ride","identity-chain-unreadable-or-mismatch","owner-alias-or-missing","player-dead-or-marker-mismatch","vessel-dead-or-unreadable","identity-changed-during-read","authority-changed-during-read","native-frame-invalid","native-input-unreadable","category-mismatch","info-without-pending-actor","info-frame-or-thread-mismatch","info-controller-or-ride-mismatch","info-session-or-stamp-mismatch","info-identity-changed","info-inputs-changed","source-cache-unavailable","source-reference-mismatch","vehicle-cache-unavailable","vehicle-cache-row-mismatch","vehicle-record-or-class-invalid","metadata-changed-during-read","query-without-selection","query-owner-mismatch","query-entry-changed","proof-lifetime-invalid","proof-identity-changed","proof-source-alias","proof-health-unreadable-or-mismatch","proof-player-array-changed","proof-changed-during-read","query-selection-stamp-changed","authoritative-component-native-actor-bridge"};
static_assert(std::size(reason_names)==static_cast<unsigned>(Reason::Count)&&static_cast<unsigned>(Reason::Count)<64);
constexpr unsigned stage_count=static_cast<unsigned>(Stage::Count);
struct Diagnostic {
 std::array<std::atomic<uint32_t>,stage_count> calls{},busy{},accepted{};
 std::atomic<uint64_t> reason_mask{0};
 std::array<Reason,stage_count> last_failure{};
 std::array<psm::shiptrace::IdentityReadDetail,stage_count> identity{};
 psm::shiptrace::HealthReadDetail health{};uintptr_t query_owner=0,query_entry=0;
 uintptr_t controller=0,ride=0,frame=0,actor=0,marker=0;
 bool first_ride_reported=false,initial_reported=false;
 uint64_t last_report=0,idle_interval=5000,periodic_interval=5000,reported_mask=0;
 unsigned idle_reports=0,transition_reports=0,periodic_reports=0;
};
Diagnostic diagnostic{};
void Increment(std::atomic<uint32_t>&x){auto n=x.load(std::memory_order_relaxed);for(unsigned i=0;i<4&&n!=UINT32_MAX;++i)if(x.compare_exchange_weak(n,n+1,std::memory_order_relaxed))return;}
unsigned Index(Stage s){return static_cast<unsigned>(s);}
void Enter(Stage s){Increment(diagnostic.calls[Index(s)]);}
void Busy(Stage s){Increment(diagnostic.busy[Index(s)]);}
void Accept(Stage s){Increment(diagnostic.accepted[Index(s)]);}
void Note(Stage s,Reason r){if(r==Reason::None)return;diagnostic.last_failure[Index(s)]=r;diagnostic.reason_mask.fetch_or(uint64_t{1}<<static_cast<unsigned>(r),std::memory_order_relaxed);}
void SetReason(Reason*out,Reason r){if(out)*out=r;}
void ResetDiagnostic(){
 for(auto&x:diagnostic.calls)x.store(0,std::memory_order_relaxed);for(auto&x:diagnostic.busy)x.store(0,std::memory_order_relaxed);for(auto&x:diagnostic.accepted)x.store(0,std::memory_order_relaxed);
 diagnostic.reason_mask.store(0,std::memory_order_relaxed);diagnostic.last_failure={};diagnostic.identity={};diagnostic.health={};diagnostic.query_owner=diagnostic.query_entry=0;diagnostic.controller=diagnostic.ride=diagnostic.frame=diagnostic.actor=diagnostic.marker=0;
 diagnostic.first_ride_reported=diagnostic.initial_reported=false;diagnostic.last_report=diagnostic.reported_mask=0;diagnostic.idle_interval=diagnostic.periodic_interval=5000;diagnostic.idle_reports=diagnostic.transition_reports=diagnostic.periodic_reports=0;
}
struct Catalogs {uintptr_t character=0,gimmick=0,vehicle=0;std::array<uintptr_t,2> server_vtables{};};
struct Inputs {uintptr_t actor=0,metadata=0,table=0,component=0;uint8_t category=0;uint16_t source_id=0;bool operator==(const Inputs&)const=default;};
struct Cache {uintptr_t global=0,manager=0,array=0,row=0;uint32_t count=0;uint16_t id=0;bool operator==(const Cache&)const=default;};
struct Metadata {Inputs inputs{};Cache source{},vehicle{};VehicleInfo info{};bool operator==(const Metadata&b)const{return inputs==b.inputs&&source==b.source&&vehicle==b.vehicle&&info.external_key==b.info.external_key&&info.type_hash==b.info.type_hash&&info.platform_vehicle==b.info.platform_vehicle&&info.blocked==b.info.blocked;}};
struct MetadataDebug {Inputs expected{};Metadata metadata{};uintptr_t returned=0;uint16_t native_id=0,source_reference=0;};
MetadataDebug info_debug{},query_debug{};
struct Proof {uint64_t epoch=0,session=0;Authority authority{};Identity id{};Metadata metadata{};uintptr_t owner=0,entry=0,hull_actor=0,hull_vtable=0;};
struct Pending {uint64_t epoch=0,session=0,stamp=0;Authority authority{};Identity id{};Inputs inputs{};uintptr_t frame=0;DWORD thread=0;};
struct Active {Proof proof{};uint64_t selection_stamp=0;bool published=false;};
std::mutex proof_mutex;
Catalogs catalogs{};Pending pending{};Active active{};
std::atomic<bool> enabled{false},exhausted{false};
std::atomic<uint64_t> session{0},next_epoch{1},live_stamp{0};
constexpr size_t kWords=49;
using Words=psm::AtomicWords<kWords>::Words;
psm::AtomicWords<kWords> published{};
Words Pack(const Proof&p){
 const auto&a=p.authority;const auto&i=p.id;const auto&x=p.metadata.inputs;const auto&s=p.metadata.source;const auto&v=p.metadata.vehicle;const auto&f=p.metadata.info;
 return {p.epoch,p.session,a.actor,a.marker,a.root,a.resource_owner,a.health_entry,a.damage_source,a.damage_target,a.generation,
 i.controller,i.player,i.ride_actor,i.ride_record,i.player_owner,i.ride_owner,i.player_uid,i.ride_uid,i.hud_uid,i.player_dock_uid,i.ride_dock_uid,i.player_category,i.ride_category,
 x.actor,x.metadata,x.table,x.component,x.category,x.source_id,s.global,s.manager,s.array,s.row,s.count,s.id,v.global,v.manager,v.array,v.row,v.count,v.id,
 f.external_key,f.type_hash,f.platform_vehicle,f.blocked,p.owner,p.entry,p.hull_actor,p.hull_vtable};
}
Proof Unpack(const Words&w){
 Proof p{};size_t n=0;auto get=[&]<class T>(T&out){out=static_cast<T>(w[n++]);};
 get(p.epoch);get(p.session);auto&a=p.authority;get(a.actor);get(a.marker);get(a.root);get(a.resource_owner);get(a.health_entry);get(a.damage_source);get(a.damage_target);get(a.generation);
 auto&i=p.id;get(i.controller);get(i.player);get(i.ride_actor);get(i.ride_record);get(i.player_owner);get(i.ride_owner);get(i.player_uid);get(i.ride_uid);get(i.hud_uid);get(i.player_dock_uid);get(i.ride_dock_uid);get(i.player_category);get(i.ride_category);
 auto&x=p.metadata.inputs;get(x.actor);get(x.metadata);get(x.table);get(x.component);get(x.category);get(x.source_id);
 auto&s=p.metadata.source;get(s.global);get(s.manager);get(s.array);get(s.row);get(s.count);get(s.id);
 auto&v=p.metadata.vehicle;get(v.global);get(v.manager);get(v.array);get(v.row);get(v.count);get(v.id);
 auto&f=p.metadata.info;get(f.external_key);get(f.type_hash);get(f.platform_vehicle);get(f.blocked);get(p.owner);get(p.entry);get(p.hull_actor);get(p.hull_vtable);return p;
}
uint64_t Stamp(uint64_t epoch){return (epoch<<1)|1;}
void Revoke(uint64_t epoch){if(!epoch)return;auto expected=Stamp(epoch);live_stamp.compare_exchange_strong(expected,epoch<<1,std::memory_order_acq_rel);}
bool Current(const Proof&p){return p.epoch&&!exhausted.load(std::memory_order_acquire)&&enabled.load(std::memory_order_acquire)&&p.session==session.load(std::memory_order_acquire)&&live_stamp.load(std::memory_order_acquire)==Stamp(p.epoch);}
uint64_t NewEpoch(){const auto e=next_epoch.fetch_add(1,std::memory_order_relaxed);if(!e||e>UINT64_MAX/2){exhausted.store(true,std::memory_order_release);return 0;}return e;}
void ClearActive(){Revoke(active.proof.epoch);active={};}
void ClearAll(){ClearActive();pending={};published.Store({});live_stamp.store(0,std::memory_order_release);}
// Available state mutex: synchronize fixture/initial state, then read coherent
// atomics. Busy state mutex: use only publication, never ordinary g snapshot.
Validity ReadAuthority(Authority*out,bool*busy){
 std::unique_lock lock(g_state_mutex,std::try_to_lock);if(lock.owns_lock())psm::authority::SynchronizeLocked();
 else if(busy)*busy=true;
 if(!psm::authority::Read(out))return Validity::Unknown;
 return out->actor>=kMinimumPointerAddress&&out->marker>=kMinimumPointerAddress&&out->generation?Validity::Valid:Validity::Invalid;
}
bool Alive(uintptr_t actor,uintptr_t expected,psm::shiptrace::IdentityReadDetail*d=nullptr,bool player=false){
 uintptr_t table=0,status=0;uint8_t dead=1;const unsigned shift=player?0:4;
 auto fail=[&](const char*field,uintptr_t base,uint32_t offset){if(d){d->field=field;d->base=base;d->offset=offset;}return false;};
 if(!Read(actor,0x68,&table))return fail("alive-actor-table-read",actor,0x68);if(d)d->status_mask|=uint8_t(1u<<shift);
 if(!Read(table,0x20,&status))return fail("alive-status-read",table,0x20);if(d){d->status_mask|=uint8_t(2u<<shift);(player?d->player_status:d->ride_status)=status;}
 if(expected&&expected!=status)return fail("alive-marker-mismatch",table,0x20);
 if(!Read(status,0x293,&dead))return fail("alive-dead-flag-read",status,0x293);if(d){d->status_mask|=uint8_t(4u<<shift);(player?d->player_dead:d->ride_dead)=dead;}
 return !dead?true:fail("alive-dead-flag-set",status,0x293);
}
// The marker resolver publishes the component table. Authoritative current-player
// capture can publish the native actor. Both must close the same status links.
bool NativePlayer(const Authority&a,uintptr_t*out,psm::shiptrace::IdentityReadDetail*d){
 uintptr_t native=0,table=0,marker=0,owner=0,reverse=0;
 auto fail=[&](const char*field,uintptr_t base,uint32_t off){if(d){d->field=field;d->base=base;d->offset=off;}return false;};
 if(!Read(a.marker,8,&native)||native<kMinimumPointerAddress)return fail("authority-marker-native-actor",a.marker,8);
 if(!Read(native,0x68,&table)||table<kMinimumPointerAddress||table==native||(a.actor!=native&&a.actor!=table))
  return fail("authority-native-actor-component-table",native,0x68);
 if(!Read(table,0x20,&marker)||marker!=a.marker)return fail("authority-component-status-marker",table,0x20);
 if(!Read(marker,0x18,&owner)||owner!=a.root||owner!=a.resource_owner)return fail("authority-status-root-owner",marker,0x18);
 if(!Read(owner,0,&reverse)||reverse!=marker)return fail("authority-owner-status-reverse",owner,0);
 *out=native;return true;
}
Validity LiveIdentity(uintptr_t controller,uintptr_t ride,Authority*out,Identity*id,bool*busy,Reason*why=nullptr,psm::shiptrace::IdentityReadDetail*d=nullptr){
 if(d)*d={};Authority first{},last{};Identity a{},b{};const auto one=ReadAuthority(&first,busy);if(one==Validity::Valid)*out=first;if(one!=Validity::Valid){SetReason(why,one==Validity::Unknown?Reason::AuthorityUnavailable:Reason::AuthorityInvalid);return one;}
 if(!ride){SetReason(why,Reason::NoRide);return Validity::Invalid;}
 uintptr_t native=0,native_again=0;if(!NativePlayer(first,&native,d)){SetReason(why,Reason::AuthorityBridge);return Validity::Invalid;}
 if(!psm::shiptrace::ReadIdentityDetailed(controller,native,ride,native,&a,d)){SetReason(why,Reason::IdentityChain);return Validity::Invalid;}
 if(!a.ride_uid||!a.player_uid||a.ride_uid==a.player_uid||!a.player_owner||!a.ride_owner||a.player_owner==a.ride_owner){SetReason(why,Reason::Owners);return Validity::Invalid;}
 if(!Alive(native,first.marker,d,true)){SetReason(why,Reason::PlayerDead);return Validity::Invalid;}
 if(!Alive(ride,0,d,false)){SetReason(why,Reason::VesselDead);return Validity::Invalid;}
 if(!psm::shiptrace::ReadIdentityDetailed(controller,native,ride,native,&b,d)||a!=b){SetReason(why,Reason::IdentityChanged);return Validity::Invalid;}
 if(!Alive(native,first.marker,d,true)){SetReason(why,Reason::PlayerDead);return Validity::Invalid;}
 if(!Alive(ride,0,d,false)){SetReason(why,Reason::VesselDead);return Validity::Invalid;}
 if(!NativePlayer(first,&native_again,d)||native_again!=native){SetReason(why,Reason::AuthorityBridge);return Validity::Invalid;}
 const auto two=ReadAuthority(&last,busy);if(two!=Validity::Valid){SetReason(why,two==Validity::Unknown?Reason::AuthorityUnavailable:Reason::AuthorityInvalid);return two;}
 if(first!=last){SetReason(why,Reason::AuthorityChanged);return Validity::Invalid;}*out=first;*id=a;return Validity::Valid;
}
bool CharacterBranch(uint8_t category){return category==1||(category>=3&&category<=9&&category!=7);}
bool InputOnce(uintptr_t actor,Inputs*out){
 Inputs x{};x.actor=actor;if(!Read(actor,0x88,&x.metadata)||!Read(x.metadata,1,&x.category)||!Read(actor,0x68,&x.table))return false;
 if(CharacterBranch(x.category)){if(!Read(x.table,0x20,&x.component)||!Read(x.component,0x30,&x.source_id))return false;}
 else if(x.category==7){if(!Read(x.table,0x30,&x.component)||!Read(x.component,0x48,&x.source_id))return false;}
 else return false;
 *out=x;return true;
}
bool ReadInputs(uintptr_t actor,Inputs*out){Inputs a{},b{};return InputOnce(actor,&a)&&InputOnce(actor,&b)&&a==b?(*out=a,true):false;}

// The HUD actor identifies the current ride and vehicle class. Only a native
// health query can nominate the separate server status owner for that same ride.
bool ResolveHullActor(const Proof& p, uintptr_t owner, uintptr_t* actor, uintptr_t* vtable) {
 uintptr_t marker=0, native=0, table=0, reverse=0, actual_owner=0, actual_vtable=0;
 uint32_t uid=0;
 Inputs inputs{};
 if(owner==p.id.ride_owner||owner==p.id.player_owner||!Read(owner,0,&marker)||!Read(marker,8,&native)||
    native==p.id.ride_actor||native==p.id.player||!Read(native,0x60,&uid)||uid!=p.id.ride_uid||
    !Read(native,0,&actual_vtable)||!actual_vtable||!Read(native,0x68,&table)||
    !Read(table,0x20,&reverse)||reverse!=marker||!Read(marker,0x18,&actual_owner)||actual_owner!=owner||
    !Alive(native,marker)||!ReadInputs(native,&inputs)||inputs.category!=p.metadata.inputs.category||
    inputs.source_id!=p.metadata.inputs.source_id) return false;
 *actor=native;
 *vtable=actual_vtable;
 return true;
}

bool ReadHullHealth(const Proof& p,Health* health,psm::shiptrace::HealthReadDetail* detail=nullptr) {
 if(!p.hull_actor)return psm::shiptrace::ReadHealthDetailed(p.id,p.owner,0,p.entry,health,detail);
 uintptr_t entry=0,again=0;
 uint16_t tag=0;
 Health value{p.owner,p.entry};
 if(!Read(p.owner,0x58,&entry)||entry!=p.entry||!Read(entry,0,&tag)||tag||
    !Read(entry,8,&value.value)||!Read(entry,0x18,&value.maximum)||value.maximum<=0||
    !Read(p.owner,0x58,&again)||again!=entry) {
  if(detail){detail->field="server-hull-selector-array";detail->base=p.owner;detail->offset=0x58;detail->array=entry;detail->tag=tag;}
  return false;
 }
 *health=value;
 return true;
}
bool CacheOnce(uintptr_t global,uint16_t id,Cache*out){
 Cache x{};x.global=global;x.id=id;auto done=[&](bool ok){*out=x;return ok;};
 const auto read = [](uintptr_t address, size_t offset, auto* value) { return Read(address, offset, value); };
 return done(psm::native_catalog::ReadRow(read, global, id, x.manager, x.count, x.array, x.row));
}
bool NativeVessel(const VehicleInfo&v){return v.platform_vehicle==1&&!v.blocked&&((v.external_key==16970&&v.type_hash==0x4798223F)||(v.external_key==16964&&v.type_hash==0x2197F080)||(v.external_key==16981&&v.type_hash==0x1C2DC3E1));}
bool MetadataOnce(const Inputs&expected,uintptr_t source_global,uintptr_t vehicle_global,uint16_t native_id,uintptr_t record,const Identity&id,Metadata*out,Reason*why,MetadataDebug*debug){
 Metadata m{};uint16_t ref=0;
 if(debug){*debug={};debug->expected=expected;debug->native_id=native_id;debug->returned=record;}
 struct Save {MetadataDebug*d;Metadata*m;uint16_t*ref;~Save(){if(d){d->metadata=*m;d->source_reference=*ref;}}} save{debug,&m,&ref};
 if(!ReadInputs(expected.actor,&m.inputs)||m.inputs!=expected){SetReason(why,Reason::InfoInputs);return false;}
 if(!CacheOnce(source_global,expected.source_id,&m.source)){SetReason(why,Reason::SourceCache);return false;}
 if(!Read(m.source.row,CharacterBranch(expected.category)?0xA0:0x2E8,&ref)||ref!=native_id){SetReason(why,Reason::SourceReference);return false;}
 if(!CacheOnce(vehicle_global,native_id,&m.vehicle)){SetReason(why,Reason::VehicleCache);return false;}
 if(m.vehicle.row!=record){SetReason(why,Reason::VehicleRow);return false;}
 if(!psm::shiptrace::ReadVehicleInfo(id,record,&m.info)||!NativeVessel(m.info)){SetReason(why,Reason::VehicleRecord);return false;}
 *out=m;return true;
}
bool ReadMetadata(const Inputs&expected,uintptr_t source_global,uintptr_t vehicle_global,uint16_t native_id,uintptr_t record,const Identity&id,Metadata*out,Reason*why=nullptr,MetadataDebug*debug=nullptr){
 Metadata a{},b{};if(!MetadataOnce(expected,source_global,vehicle_global,native_id,record,id,&a,why,debug)||!MetadataOnce(expected,source_global,vehicle_global,native_id,record,id,&b,why,debug))return false;
 if(a!=b){SetReason(why,Reason::MetadataChanged);return false;}*out=a;return true;
}
bool Aliases(const Proof&p){
 const auto&a=p.authority;
 return p.entry==a.health_entry||p.owner==a.resource_owner||p.owner==a.root||p.owner==a.damage_source||p.owner==a.damage_target;
}
bool CurrentHullActor(const Proof& p) {
 uintptr_t actor=0,vtable=0;
 return ResolveHullActor(p,p.owner,&actor,&vtable)&&actor==p.hull_actor&&vtable==p.hull_vtable;
}
Validity Validate(const Proof&p,bool*busy,bool require_published=true,Reason*why=nullptr,psm::shiptrace::IdentityReadDetail*d=nullptr,MetadataDebug*md=nullptr,psm::shiptrace::HealthReadDetail*hd=nullptr){
 if(!enabled.load(std::memory_order_acquire)||p.session!=session.load(std::memory_order_acquire)||!p.epoch||(require_published&&!Current(p))){SetReason(why,Reason::ProofLifetime);return Validity::Invalid;}
 Authority a{},again{};Identity id{},second{};auto result=LiveIdentity(p.id.controller,p.id.ride_actor,&a,&id,busy,why,d);if(result!=Validity::Valid)return result;
 if(a!=p.authority||id!=p.id||!p.entry){SetReason(why,Reason::ProofIdentity);return Validity::Invalid;}
 if(p.hull_actor?!CurrentHullActor(p):p.owner!=id.ride_owner){SetReason(why,Reason::ProofIdentity);return Validity::Invalid;}
 if(Aliases(p)){SetReason(why,Reason::ProofAlias);return Validity::Invalid;}
 Metadata m{};Health h{};uintptr_t player_entries=0,again_entries=0,hud_entries=0;
 if(!ReadMetadata(p.metadata.inputs,p.metadata.source.global,p.metadata.vehicle.global,p.metadata.vehicle.id,p.metadata.vehicle.row,id,&m,why,md))return Validity::Invalid;
 if(m!=p.metadata){SetReason(why,Reason::MetadataChanged);return Validity::Invalid;}
 if(!Read(id.player_owner,0x58,&player_entries)||player_entries==p.entry||!ReadHullHealth(p,&h,hd)){SetReason(why,Reason::ProofHealth);return Validity::Invalid;}
 if(p.hull_actor&&(!Read(id.ride_owner,0x58,&hud_entries)||hud_entries==p.entry)){SetReason(why,Reason::ProofAlias);return Validity::Invalid;}
 if(!Read(id.player_owner,0x58,&again_entries)||again_entries!=player_entries){SetReason(why,Reason::ProofArray);return Validity::Invalid;}
 result=LiveIdentity(p.id.controller,p.id.ride_actor,&again,&second,busy,why,d);if(result!=Validity::Valid)return result;
 if(again!=a||second!=id||(p.hull_actor&&!CurrentHullActor(p))||(require_published&&!Current(p))){SetReason(why,Reason::ProofChanged);return Validity::Invalid;}return Validity::Valid;
}
struct Metrics {uint32_t positive=0,scaled=0,equal=0,loss=0;int64_t old=0,native=0,final=0;bool operator==(const Metrics&)const=default;};
Metrics metrics{},reported{};unsigned reports=0;uintptr_t reported_entry=0;uint64_t reported_at=0;bool reported_live=false;
void Bump(uint32_t&x){if(x!=UINT32_MAX)++x;}
}
void Configure(uintptr_t character,uintptr_t gimmick,uintptr_t vehicle,std::array<uintptr_t,2> server_vtables){std::lock_guard lock(proof_mutex);session.fetch_add(1,std::memory_order_acq_rel);ClearAll();catalogs={character,gimmick,vehicle,server_vtables};}
void Enable(bool on){enabled.store(false,std::memory_order_release);std::lock_guard lock(proof_mutex);session.fetch_add(1,std::memory_order_acq_rel);ClearAll();ResetDiagnostic();info_debug={};query_debug={};metrics={};reported={};reports=0;reported_entry=0;reported_at=0;reported_live=false;enabled.store(on,std::memory_order_release);}
void Actor(uintptr_t controller,uintptr_t ride,uintptr_t frame){
 RestoreStatus status;if(!enabled.load(std::memory_order_acquire))return;Enter(Stage::Actor);std::unique_lock lock(proof_mutex,std::try_to_lock);if(!lock.owns_lock()){Busy(Stage::Actor);return;}
 diagnostic.controller=controller;diagnostic.ride=ride;diagnostic.frame=frame;diagnostic.actor=diagnostic.marker=0;
 if(active.published&&!Current(active.proof))active={};
 Authority a{};Identity id{};Inputs inputs{};bool busy=false;Reason why=Reason::None;const auto result=LiveIdentity(controller,ride,&a,&id,&busy,&why,&diagnostic.identity[Index(Stage::Actor)]);
 diagnostic.actor=a.actor;diagnostic.marker=a.marker;
 if(result==Validity::Unknown){Note(Stage::Actor,why);return;}
 if(result!=Validity::Valid){Note(Stage::Actor,why);ClearActive();pending={};return;}
 if(frame<kMinimumPointerAddress){Note(Stage::Actor,Reason::Frame);ClearActive();pending={};return;}
 if(!ReadInputs(ride,&inputs)){Note(Stage::Actor,Reason::Inputs);ClearActive();pending={};return;}
 if(inputs.category!=id.ride_category){Note(Stage::Actor,Reason::Category);ClearActive();pending={};return;}
 if(active.proof.epoch){bool active_busy=false;const auto valid=Validate(active.proof,&active_busy,active.published);if(valid==Validity::Invalid)ClearActive();}
 const auto stamp=live_stamp.load(std::memory_order_acquire),epoch=NewEpoch();if(!epoch)return;
 pending={epoch,session.load(std::memory_order_acquire),stamp,a,id,inputs,frame,GetCurrentThreadId()};Accept(Stage::Actor);
}
void Info(uintptr_t controller,uintptr_t ride,uintptr_t record,uint16_t native_id,uintptr_t frame){
 RestoreStatus status;if(!enabled.load(std::memory_order_acquire))return;Enter(Stage::Info);std::unique_lock lock(proof_mutex,std::try_to_lock);if(!lock.owns_lock()){Busy(Stage::Info);return;}
 if(active.published&&!Current(active.proof))active={};
 if(!pending.epoch){Note(Stage::Info,Reason::InfoNoPending);return;}
 if(pending.frame!=frame||pending.thread!=GetCurrentThreadId()){Note(Stage::Info,Reason::InfoFrame);return;}
 if(pending.id.controller!=controller||pending.id.ride_actor!=ride){Note(Stage::Info,Reason::InfoContext);return;}
 const auto selected=pending;pending={};
 if(selected.session!=session.load(std::memory_order_acquire)||selected.stamp!=live_stamp.load(std::memory_order_acquire)){Note(Stage::Info,Reason::InfoSession);return;}
 Authority a{};Identity id{};Inputs inputs{};Metadata m{};bool busy=false;Reason why=Reason::None;
 const auto valid=LiveIdentity(controller,ride,&a,&id,&busy,&why,&diagnostic.identity[Index(Stage::Info)]);
 if(valid==Validity::Unknown){Note(Stage::Info,why);return;}
 const auto source=CharacterBranch(selected.inputs.category)?catalogs.character:catalogs.gimmick;
 if(valid!=Validity::Valid){Note(Stage::Info,why);ClearActive();return;}
 if(a!=selected.authority||id!=selected.id){Note(Stage::Info,Reason::InfoIdentity);ClearActive();return;}
 if(!ReadInputs(ride,&inputs)||inputs!=selected.inputs){Note(Stage::Info,Reason::InfoInputs);ClearActive();return;}
 if(!ReadMetadata(inputs,source,catalogs.vehicle,native_id,record,id,&m,&why,&info_debug)){Note(Stage::Info,why);ClearActive();return;}
 if(selected.stamp!=live_stamp.load(std::memory_order_acquire)){Note(Stage::Info,Reason::InfoSession);ClearActive();return;}
 Accept(Stage::Info);
 if(active.published&&Current(active.proof)&&active.proof.authority==a&&active.proof.id==id&&active.proof.metadata==m)return;
 ClearActive();active.proof.epoch=selected.epoch;active.proof.session=selected.session;active.proof.authority=a;active.proof.id=id;active.proof.metadata=m;
 if(!catalogs.server_vtables[0])active.proof.owner=id.ride_owner;
 active.selection_stamp=live_stamp.load(std::memory_order_acquire);active.published=false;
}
void Query(uintptr_t owner,uint16_t selector,uintptr_t entry){
 RestoreStatus status;if(selector||!enabled.load(std::memory_order_acquire))return;Enter(Stage::Query);std::unique_lock lock(proof_mutex,std::try_to_lock);if(!lock.owns_lock()){Busy(Stage::Query);return;}
 diagnostic.query_owner=owner;diagnostic.query_entry=entry;
 if(active.published&&!Current(active.proof)){Note(Stage::Query,Reason::ProofLifetime);active={};return;}
 if(!active.proof.epoch){Note(Stage::Query,Reason::QueryNoSelection);return;}
 if(active.proof.owner&&owner!=active.proof.owner){Note(Stage::Query,Reason::QueryOwner);return;}
 if(active.published&&entry!=active.proof.entry){Note(Stage::Query,Reason::QueryEntry);ClearActive();return;}
 Proof candidate=active.proof;candidate.entry=entry;bool busy=false;Reason why=Reason::None;
 if(!candidate.owner){
  if(!ResolveHullActor(candidate,owner,&candidate.hull_actor,&candidate.hull_vtable)||
     (candidate.hull_vtable!=catalogs.server_vtables[0]&&candidate.hull_vtable!=catalogs.server_vtables[1])){
   Note(Stage::Query,Reason::QueryOwner);return;
  }
  candidate.owner=owner;
 }
 const auto valid=Validate(candidate,&busy,active.published,&why,&diagnostic.identity[Index(Stage::Query)],&query_debug,&diagnostic.health);
 if(valid==Validity::Unknown){Note(Stage::Query,why);return;}
 if(valid!=Validity::Valid){Note(Stage::Query,why);ClearActive();return;}
 if(!active.published&&active.selection_stamp!=live_stamp.load(std::memory_order_acquire)){Note(Stage::Query,Reason::QueryStamp);ClearActive();return;}
 Accept(Stage::Query);
 if(active.published&&active.proof.entry==entry)return;
 active.proof=candidate;published.Store(Pack(candidate));live_stamp.store(Stamp(candidate.epoch),std::memory_order_release);active.published=true;
}
bool Adjust(uintptr_t entry,int64_t native,double multiplier,int64_t*out){
 RestoreStatus status;if(!out||!entry||!enabled.load(std::memory_order_acquire)||!std::isfinite(multiplier)||multiplier<0)return false;
 std::unique_lock lock(proof_mutex,std::try_to_lock);if(!lock.owns_lock()||!active.published||entry!=active.proof.entry)return false;
 const auto proof=active.proof;bool busy=false;const auto validity=Validate(proof,&busy);
 if(validity==Validity::Invalid){Revoke(proof.epoch);return false;}if(validity!=Validity::Valid)return false;
 Health h{},again{};int64_t bonus=0,minimum=0,bonus_again=0,minimum_again=0;
 if(!ReadHullHealth(proof,&h)||!Read(entry,0x20,&bonus)||!Read(entry,0x28,&minimum)||(bonus>0&&h.maximum>INT64_MAX-bonus)||(bonus<0&&h.maximum<INT64_MIN-bonus)){Revoke(proof.epoch);return false;}
 const auto upper=h.maximum+bonus;if(minimum<0||minimum>upper||h.value<minimum||h.value>upper||native<minimum||native>upper){Revoke(proof.epoch);return false;}
 int64_t adjusted=native;
 if(!busy&&native>h.value){const auto missing=upper-h.value,delta=native-h.value;const auto scaled=std::floor(static_cast<double>(delta)*multiplier);const auto gain=!std::isfinite(scaled)||scaled>=static_cast<double>(missing)?missing:scaled<=0?int64_t{0}:static_cast<int64_t>(scaled);adjusted=h.value+gain;}
 bool final_busy=false;const auto final_validity=Validate(proof,&final_busy);
 if(final_validity==Validity::Invalid){Revoke(proof.epoch);return false;}if(final_validity!=Validity::Valid)return false;
 if(!ReadHullHealth(proof,&again)||again.value!=h.value||again.maximum!=h.maximum||!Read(entry,0x20,&bonus_again)||bonus_again!=bonus||!Read(entry,0x28,&minimum_again)||minimum_again!=minimum){Revoke(proof.epoch);return false;}
 Authority last_authority{};const auto authority_validity=ReadAuthority(&last_authority,&final_busy);
 if(authority_validity==Validity::Invalid){Revoke(proof.epoch);return false;}if(authority_validity!=Validity::Valid)return false;
 if(last_authority!=proof.authority){Revoke(proof.epoch);return false;}
 if(!Current(proof))return false;if(final_busy)adjusted=native;
 metrics.old=h.value;metrics.native=native;metrics.final=adjusted;if(native>h.value){Bump(metrics.positive);if(adjusted!=native)Bump(metrics.scaled);}else if(native==h.value)Bump(metrics.equal);else Bump(metrics.loss);
 *out=adjusted;return true;
}
bool PreserveNative(uintptr_t entry){
 RestoreStatus status;if(!entry||!enabled.load(std::memory_order_acquire))return false;Words words{};uint64_t version=0;if(!published.Load(&words,&version))return false;const auto proof=Unpack(words);
 if(entry!=proof.entry||!Current(proof))return false;bool busy=false;const auto valid=Validate(proof,&busy);
 if(valid==Validity::Invalid){Revoke(proof.epoch);return false;}return valid==Validity::Valid&&Current(proof)&&published.Version()==version;
}
void DiagnosticHeartbeat(uint64_t now){
 struct Snapshot {std::array<uint32_t,stage_count>calls{},busy{},accepted{};std::array<Reason,stage_count>failure{};uint64_t mask=0;uintptr_t controller=0,ride=0,frame=0,actor=0,marker=0,owner=0,entry=0;const char*state="waiting-for-native-actor";std::array<psm::shiptrace::IdentityReadDetail,stage_count>details{};psm::shiptrace::HealthReadDetail health{};MetadataDebug metadata{};uintptr_t query_owner=0,query_entry=0;};Snapshot x{};
 {std::unique_lock lock(proof_mutex,std::try_to_lock);if(!lock.owns_lock()||!enabled.load(std::memory_order_acquire))return;
  auto&d=diagnostic;const auto mask=d.reason_mask.load(std::memory_order_relaxed);const bool elapsed=now>=d.last_report;const auto age=elapsed?now-d.last_report:0;bool emit=false;
  if(!d.initial_reported){d.initial_reported=true;emit=true;}
  else if(d.ride&&!d.first_ride_reported){d.first_ride_reported=true;emit=true;}
  else if(!d.first_ride_reported&&d.idle_reports<8&&elapsed&&age>=d.idle_interval){++d.idle_reports;d.idle_interval=(std::min)(d.idle_interval*2,uint64_t{360000});emit=true;}
  else if(d.first_ride_reported&&mask!=d.reported_mask&&d.transition_reports<24&&elapsed&&age>=1000){++d.transition_reports;emit=true;}
  else if(d.first_ride_reported&&d.periodic_reports<16&&elapsed&&age>=d.periodic_interval){++d.periodic_reports;d.periodic_interval=(std::min)(d.periodic_interval*2,uint64_t{60000});emit=true;}
  if(!emit)return;d.last_report=now;d.reported_mask=mask;x.mask=mask;x.failure=d.last_failure;x.controller=d.controller;x.ride=d.ride;x.frame=d.frame;x.actor=d.actor;x.marker=d.marker;
  for(unsigned i=0;i<stage_count;++i){x.calls[i]=d.calls[i].load(std::memory_order_relaxed);x.busy[i]=d.busy[i].load(std::memory_order_relaxed);x.accepted[i]=d.accepted[i].load(std::memory_order_relaxed);}
  x.owner=active.proof.owner;x.entry=active.proof.entry;x.details=d.identity;x.health=d.health;x.metadata=info_debug;x.query_owner=d.query_owner;x.query_entry=d.query_entry;
  bool busy=false;const auto validity=active.published?Validate(active.proof,&busy):Validity::Unknown;
  x.state=active.published?(validity==Validity::Valid?"current-vessel-health-bound":validity==Validity::Invalid?"published-proof-invalid":"published-proof-authority-unavailable"):active.proof.epoch?"waiting-for-health-query":pending.epoch?"waiting-for-native-info":d.ride?"native-context-rejected":"waiting-for-native-actor";
 }
 Log("ship-proof: state=%s actorCalls=%u infoCalls=%u healthQueryCalls=%u actorAccepted=%u infoAccepted=%u queryAccepted=%u actorBusy=%u infoBusy=%u queryBusy=%u lastActorFailure=%s lastInfoFailure=%s lastQueryFailure=%s reasonMask=%llX controller=0x%p rideActor=0x%p frame=0x%p authorityComponentTable=0x%p authorityMarker=0x%p selectedOwner=0x%p selectedEntry=0x%p cumulative-approx-counters=1 identity-authority-unchanged=1 worker-max50-lines=1",x.state,x.calls[0],x.calls[1],x.calls[2],x.accepted[0],x.accepted[1],x.accepted[2],x.busy[0],x.busy[1],x.busy[2],reason_names[static_cast<unsigned>(x.failure[0])],reason_names[static_cast<unsigned>(x.failure[1])],reason_names[static_cast<unsigned>(x.failure[2])],static_cast<unsigned long long>(x.mask),reinterpret_cast<void*>(x.controller),reinterpret_cast<void*>(x.ride),reinterpret_cast<void*>(x.frame),reinterpret_cast<void*>(x.actor),reinterpret_cast<void*>(x.marker),reinterpret_cast<void*>(x.owner),reinterpret_cast<void*>(x.entry));
 const auto&a=x.details[0];const auto&i=x.details[1];const auto&q=x.details[2];const auto&id=a.observed;
 Log("ship-proof-identity: actorField=%s actorBase=0x%p actorOffset=%X actorReadMask=%llX nativePlayerActorRaw=0x%p playerUID=%u rideUID=%u hudUID=%u playerCategory=%u rideCategory=%u playerOwner=0x%p rideOwner=0x%p playerStatus=0x%p rideStatus=0x%p playerDead=%u rideDead=%u statusReadMask=%X infoField=%s infoBase=0x%p infoOffset=%X queryField=%s queryBase=0x%p queryOffset=%X last-detailed-identity-attempt=1 no-R15-authority=1 worker-max50-lines=1",a.field,reinterpret_cast<void*>(a.base),a.offset,static_cast<unsigned long long>(a.read_mask),reinterpret_cast<void*>(id.player),id.player_uid,id.ride_uid,id.hud_uid,id.player_category,id.ride_category,reinterpret_cast<void*>(id.player_owner),reinterpret_cast<void*>(id.ride_owner),reinterpret_cast<void*>(a.player_status),reinterpret_cast<void*>(a.ride_status),a.player_dead,a.ride_dead,a.status_mask,i.field,reinterpret_cast<void*>(i.base),i.offset,q.field,reinterpret_cast<void*>(q.base),q.offset);
 const auto&md=x.metadata;const auto&mi=md.expected;const auto&ms=md.metadata.source;const auto&mv=md.metadata.vehicle;const auto&h=x.health;
 Log("ship-proof-native: infoCategory=%u infoMetadata=0x%p infoTable=0x%p infoComponent=0x%p sourceID=%u nativeVehicleID=%u sourceReference=%u sourceManager=0x%p sourceArray=0x%p sourceRow=0x%p sourceCount=%u vehicleManager=0x%p vehicleArray=0x%p vehicleRow=0x%p vehicleCount=%u returnedInfo=0x%p queryOwnerRaw=0x%p queryEntryRaw=0x%p healthField=%s healthBase=0x%p healthOffset=%X healthArray=0x%p healthTag=%u healthReadMask=%X last-detailed-metadata-and-health-attempt=1 raw-query-never-dereferenced-until-proof=1 worker-max50-lines=1",mi.category,reinterpret_cast<void*>(mi.metadata),reinterpret_cast<void*>(mi.table),reinterpret_cast<void*>(mi.component),mi.source_id,md.native_id,md.source_reference,reinterpret_cast<void*>(ms.manager),reinterpret_cast<void*>(ms.array),reinterpret_cast<void*>(ms.row),ms.count,reinterpret_cast<void*>(mv.manager),reinterpret_cast<void*>(mv.array),reinterpret_cast<void*>(mv.row),mv.count,reinterpret_cast<void*>(md.returned),reinterpret_cast<void*>(x.query_owner),reinterpret_cast<void*>(x.query_entry),h.field,reinterpret_cast<void*>(h.base),h.offset,reinterpret_cast<void*>(h.array),h.tag,h.read_mask);
}
void Heartbeat(uint64_t now){
 DiagnosticHeartbeat(now);
 if(!enabled.load(std::memory_order_acquire))return;Proof proof{};Metrics m{};bool live=false;
 {std::unique_lock lock(proof_mutex,std::try_to_lock);if(!lock.owns_lock()||reports>=64)return;proof=active.proof;bool busy=false;const auto v=active.published?Validate(proof,&busy):Validity::Unknown;if(v==Validity::Invalid)Revoke(proof.epoch);live=v==Validity::Valid;m=metrics;const bool changed=m!=reported||proof.entry!=reported_entry||live!=reported_live;if(reports&&(!changed||now<reported_at||now-reported_at<1000))return;++reports;reported=m;reported_entry=proof.entry;reported_live=live;reported_at=now;}
 Log("ship-healing: state=%s currentRideUID=%u nativeVehicleKey=%u typeHash=%X serverHullActor=0x%p serverHullOwner=0x%p healthEntry=0x%p proofEpoch=%llu nativePositive=%u scaledPositive=%u nativeEqual=%u nativeLoss=%u lastOld=%lld lastNative=%lld lastFinal=%lld current-ride-only=1 native-positive-events-only=1 custom-timer=0 getter-input-and-native-cache-bound=1 worker-max64-lines=1",live?"current-vessel-health-bound":"waiting-for-coherent-current-vessel-health",proof.id.ride_uid,proof.metadata.info.external_key,proof.metadata.info.type_hash,reinterpret_cast<void*>(proof.hull_actor),reinterpret_cast<void*>(proof.hull_actor?proof.owner:0),reinterpret_cast<void*>(proof.entry),static_cast<unsigned long long>(proof.epoch),m.positive,m.scaled,m.equal,m.loss,static_cast<long long>(m.old),static_cast<long long>(m.native),static_cast<long long>(m.final));
}
}
