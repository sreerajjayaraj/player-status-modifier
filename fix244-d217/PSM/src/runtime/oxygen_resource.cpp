#include "runtime/oxygen_resource.h"
#include "runtime/native_catalog_3663.h"
#include "runtime/runtime_state.h"
#include "runtime/player_identity_publication.h"
#include "config.h"
#include "logger.h"
#include <array>
#include <atomic>
#include <limits>

namespace {
struct Binding {
 uintptr_t actor=0,gauge=0,owner=0,entries=0,entry=0;
 uint16_t selector=0xffff;
 std::array<uintptr_t,8> player{};
};
struct ServerBinding {
 uintptr_t owner=0,entry=0;
 std::array<uintptr_t,8> player{};
};
struct LocalBreath {
 uintptr_t root=0, context=0, token=0, actor=0, marker=0, owner=0, entries=0, entry=0;
 uint32_t uid=0, hero=0;
 uint16_t selector=0;
 bool operator==(const LocalBreath&) const = default;
};
Binding binding;
ServerBinding server_binding;
LocalBreath local_breath{};
std::atomic<uintptr_t> local_breath_entry{0};
uintptr_t client_control_slot=0;
unsigned local_bind_logs=0;
std::atomic<uintptr_t> observed_entry{0};
std::atomic<uintptr_t> observed_server_entry{0};
uintptr_t server_actor_vtable=0, server_status_vtable=0;
std::atomic<bool> catalog_available{false};
psm::oxygen::Catalog catalog{};
bool enabled=false;
unsigned bind_logs=0,adjustment_logs[2][3]={},server_refill_logs=0;
constexpr std::size_t kMaxStatusEntries=512,kEntryStride=0x90;
constexpr uint32_t kBreathKey=1000078,kPlayerGroupKey=1000001;
template<class T> bool Read(uintptr_t base,size_t offset,T* value) {
 if(!value||base<kMinimumPointerAddress||offset>UINTPTR_MAX-base||
    sizeof(T)>UINTPTR_MAX-base-offset)return false;
 __try{*value=*reinterpret_cast<const T*>(base+offset);return true;}
 __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
std::array<uintptr_t,8> Player() {
 const auto& p=g_player_resolve;
 return {p.actor,p.marker,p.root,p.resource_owner,p.health_entry,p.stamina_entry,p.spirit_entry,
         (static_cast<uintptr_t>(static_cast<uint32_t>(p.stamina_type))<<32)|
          static_cast<uint32_t>(p.spirit_type)};
}
void Reset() { observed_entry.store(0,std::memory_order_release);binding={}; }
void ResetLocal() {
 local_breath_entry.store(0,std::memory_order_release);
 local_breath={};
}
void ResetServer() {
 observed_server_entry.store(0,std::memory_order_release);
 server_binding={};
}
bool Ready() {
 return enabled&&g_runtime_enabled.load(std::memory_order_acquire)&&
  g_player_resolve.valid()&&g_player_resolve.actor>=kMinimumPointerAddress&&
  g_player_resolve.resource_owner>=kMinimumPointerAddress;
}
bool PlayerOwner(uintptr_t* owner, uintptr_t observed_actor=0) {
 const auto& p=g_player_resolve;
 uintptr_t native=0,table=0,status=0,reverse=0;uint8_t dead=1;
 return Read(p.marker,8,&native)&&(!observed_actor||native==observed_actor)&&
  Read(native,0x68,&table)&&(p.actor==native||p.actor==table)&&
  Read(table,0x20,&status)&&status==p.marker&&Read(status,0x293,&dead)&&!dead&&
  Read(status,0x18,owner)&&*owner==p.resource_owner&&Read(*owner,0,&reverse)&&reverse==status;
}
bool Live(const Binding& b) {
 uintptr_t owner=0,entries=0;uint16_t selector=0xffff;
 return Ready()&&b.player==Player()&&PlayerOwner(&owner,b.actor)&&owner==b.owner&&
  Read(b.gauge,0xE2,&selector)&&selector==b.selector&&selector>0&&selector!=0xffff&&
  selector!=g_player_resolve.stamina_type&&selector!=g_player_resolve.spirit_type&&
  Read(owner,0x58,&entries)&&entries==b.entries&&entries>=kMinimumPointerAddress;
}
bool IsStatusArrayEntry(uintptr_t entry,uintptr_t entries) {
 return entries>=kMinimumPointerAddress&&entry>=entries&&
  entry-entries<kMaxStatusEntries*kEntryStride&&(entry-entries)%kEntryStride==0;
}
bool Entry(const Binding& b,uintptr_t entry) {
 uint16_t selector=0xffff;int64_t maximum=0,bonus=0,minimum=0;
 const auto& p=g_player_resolve;
 return IsStatusArrayEntry(entry,b.entries)&&
  entry!=p.health_entry&&entry!=p.stamina_entry&&entry!=p.spirit_entry&&
  Read(entry,0,&selector)&&selector==b.selector&&Read(entry,0x18,&maximum)&&
  Read(entry,0x20,&bonus)&&Read(entry,0x28,&minimum)&&maximum>0&&bonus>=0&&
  bonus<=INT64_MAX-maximum&&minimum>=0&&minimum<maximum+bonus;
}
struct CatalogRow {
 uintptr_t registry=0,rows=0,row=0;
 bool operator==(const CatalogRow&)const=default;
};
bool CachedRow(uintptr_t global,uint16_t index,CatalogRow* out) {
 uint32_t count=0;
 const auto read = [](uintptr_t address, size_t offset, auto* value) { return Read(address, offset, value); };
 return psm::native_catalog::ReadRow(read, global, index, out->registry, count, out->rows, out->row);
}
struct CatalogProof {
 uintptr_t owner=0,marker=0,entries=0,map=0,selectors=0,server_actor=0;
 CatalogRow actor{},group{},status{};
 uint16_t selector=0xFFFF;
 uint32_t native_index=0;
 int32_t slot=-1;
 bool operator==(const CatalogProof&)const=default;
};
bool ResolveCatalogSlot(uintptr_t marker, uint16_t selector, CatalogProof* proof) {
 uint16_t actor_index=0,group_index=0,entry_selector=0xFFFF;
 uint8_t rate_kind=0;
 uint32_t status_key=0,group_key=0,map_count=0,selector_count=0;
 return CachedRow(catalog.statuses,selector,&proof->status) &&
    Read(proof->status.row,8,&status_key) && status_key==kBreathKey &&
    Read(proof->status.row,0x1A,&rate_kind) && rate_kind==2 &&
    Read(proof->status.row,0x2C,&proof->native_index) &&
    Read(marker,0x30,&actor_index) && CachedRow(catalog.actors,actor_index,&proof->actor) &&
    Read(proof->actor.row,0x3A6,&group_index) && CachedRow(catalog.groups,group_index,&proof->group) &&
    Read(proof->group.row,8,&group_key) && group_key==kPlayerGroupKey &&
    Read(proof->group.row,0x50,&proof->map) && Read(proof->group.row,0x58,&map_count) &&
    proof->native_index<map_count &&
    Read(proof->map,sizeof(proof->slot)*static_cast<std::size_t>(proof->native_index),&proof->slot) &&
    proof->slot>=0 && static_cast<std::size_t>(proof->slot)<kMaxStatusEntries &&
    Read(proof->group.row,0x10,&proof->selectors) && Read(proof->group.row,0x18,&selector_count) &&
    static_cast<uint32_t>(proof->slot)<selector_count &&
    Read(proof->selectors,sizeof(entry_selector)*static_cast<std::size_t>(proof->slot),&entry_selector) &&
    entry_selector==selector;
}

bool ReadLocalBreath(uintptr_t actor, uint16_t selector, LocalBreath* result) {
 if (!client_control_slot || !actor || !catalog || !selector || selector==0xffff) return false;
 LocalBreath view{};
 view.actor=actor;
 view.selector=selector;
 uintptr_t control=0, table=0, reverse=0;
 uint8_t local=0, flags=0, dead=1;
 if (!Read(client_control_slot,0,&view.root) || !Read(view.root,0x30,&view.context) ||
     !Read(view.context,0x58,&view.token) || !view.token ||
     !Read(actor,0xA0,&control) || control!=view.token ||
     !Read(actor,0x94,&local) || !local || !Read(actor,0x60,&view.uid) || !view.uid ||
     !Read(actor,0x5E,&flags) || !(flags&0x10) ||
     !Read(actor,0x68,&table) || !Read(table,0x20,&view.marker) ||
     !Read(view.marker,8,&reverse) || reverse!=actor ||
     !Read(view.marker,0x293,&dead) || dead ||
     !Read(view.marker,0x18,&view.owner) ||
     !Read(view.owner,0,&reverse) || reverse!=view.marker ||
     !Read(view.owner,0x58,&view.entries) || view.entries<kMinimumPointerAddress) return false;

 CatalogProof proof{};
 if (!ResolveCatalogSlot(view.marker,selector,&proof) || !Read(proof.actor.row,8,&view.hero) ||
     (view.hero!=1 && view.hero!=4 && view.hero!=6)) return false;
 const auto stride=static_cast<std::size_t>(proof.slot)*kEntryStride;
 if (stride>UINTPTR_MAX-view.entries) return false;
 view.entry=view.entries+stride;
 uint16_t health_selector=0xffff, entry_selector=0xffff;
 int64_t maximum=0, minimum=0;
 if (!Read(view.entries,0,&health_selector) || health_selector!=0 ||
     !Read(view.entry,0,&entry_selector) || entry_selector!=selector ||
     !Read(view.entry,0x18,&maximum) || !Read(view.entry,0x28,&minimum) ||
     minimum<0 || maximum<=minimum) return false;
 *result=view;
 return true;
}

bool LegacyServerOwner(uintptr_t owner, uintptr_t* marker) {
 const auto& p=g_player_resolve;
 if (!server_actor_vtable || owner==p.resource_owner ||
     !Read(owner,0,marker) || *marker==p.marker) return false;
 uintptr_t active_owner=0, actor=0, table=0, reverse=0, vtable=0, local_actor=0;
 uintptr_t local_meta=0, server_meta=0;
 uint32_t local_uid=0, uid=0;
 uint8_t local_category=0, category=0, dead=1;
 return PlayerOwner(&active_owner) &&
  Read(*marker,8,&actor) && Read(actor,0,&vtable) && vtable==server_actor_vtable &&
  Read(actor,0x68,&table) && Read(table,0x20,&reverse) && reverse==*marker &&
  Read(*marker,0x18,&reverse) && reverse==owner && Read(*marker,0x293,&dead) && !dead &&
  Read(p.marker,8,&local_actor) && actor!=local_actor &&
  Read(local_actor,0x60,&local_uid) && local_uid && Read(actor,0x60,&uid) && uid==local_uid &&
  Read(local_actor,0x88,&local_meta) && Read(local_meta,1,&local_category) &&
  (local_category==1 || local_category==4) &&
  Read(actor,0x88,&server_meta) && Read(server_meta,1,&category) && category==local_category;
}
// The controlled actor owns the HUD bank. The native server actor can own
// another bank for the same hero and process its minimum-value passives.
bool ControlledServerOwner(uintptr_t owner, CatalogProof* proof, LocalBreath* controlled) {
 uintptr_t& actor=proof->server_actor;
 uintptr_t* marker=&proof->marker;
 uintptr_t vtable=0, table=0, reverse=0, metadata=0;
 uint32_t uid=0;
 uint8_t dead=1, category=0;
 return server_status_vtable &&
     ReadLocalBreath(local_breath.actor,local_breath.selector,controlled) &&
     *controlled==local_breath && owner!=controlled->owner &&
     Read(owner,0,marker) && *marker!=controlled->marker &&
     Read(*marker,0,&vtable) && vtable==server_status_vtable &&
     Read(*marker,8,&actor) && actor!=controlled->actor &&
     Read(actor,0x60,&uid) && uid==controlled->uid &&
     Read(actor,0x68,&table) && Read(table,0x20,&reverse) && reverse==*marker &&
     Read(*marker,0x18,&reverse) && reverse==owner &&
     Read(*marker,0x293,&dead) && !dead &&
     Read(actor,0x88,&metadata) && Read(metadata,1,&category) &&
     category==(controlled->hero==1 ? 1 : 4);
}

bool ObserveCatalogEntry(uintptr_t entry,CatalogProof* proof,uintptr_t server_owner=0) {
 const auto& p=g_player_resolve;
 if(!catalog)return false;
 LocalBreath controlled{};
 const bool controlled_server=server_owner && client_control_slot;
 if (server_owner) {
  proof->owner=server_owner;
  if (controlled_server) {
   if (!ControlledServerOwner(server_owner,proof,&controlled)) return false;
  } else if (!Ready() || !LegacyServerOwner(server_owner,&proof->marker)) return false;
 } else if (!Ready() || !PlayerOwner(&proof->owner) || !Read(proof->owner,0,&proof->marker) || proof->marker!=p.marker) {
  return false;
 }
 uint16_t array_selector=0xFFFF;
 if(!Read(proof->owner,0x58,&proof->entries)||
    (controlled_server ? proof->entries==controlled.entries :
     server_owner ? proof->entries==p.health_entry : proof->entries!=p.health_entry)||
    !IsStatusArrayEntry(entry,proof->entries)||
    !Read(proof->entries,0,&array_selector)||array_selector!=0||
    !Read(entry,0,&proof->selector)||proof->selector==0||proof->selector==0xFFFF||
    (!controlled_server && (proof->selector==p.stamina_type||proof->selector==p.spirit_type))||
    !ResolveCatalogSlot(proof->marker,proof->selector,proof)||
    static_cast<std::size_t>(proof->slot)!=(entry-proof->entries)/kEntryStride)
  return false;
 if (controlled_server) {
  uint32_t hero=0;
  int64_t maximum=0, bonus=0, minimum=0;
  return proof->selector==controlled.selector &&
      Read(proof->actor.row,8,&hero) && hero==controlled.hero &&
      Read(entry,0x18,&maximum) && Read(entry,0x20,&bonus) && Read(entry,0x28,&minimum) &&
      maximum>0 && bonus>=0 && bonus<=INT64_MAX-maximum && minimum==0;
 }
 Binding exact{};
 exact.entries=proof->entries;
 exact.selector=proof->selector;
 return Entry(exact,entry);
}
}
void SetPlayerOxygenEnabled(bool value,psm::oxygen::Catalog next,uintptr_t server_vtable,
                            uintptr_t control_slot, uintptr_t status_vtable) {
 std::lock_guard lock(g_state_mutex);
 enabled=value;catalog=value?next:psm::oxygen::Catalog{};
 server_actor_vtable=catalog ? server_vtable : 0;
 client_control_slot=catalog ? control_slot : 0;
 server_status_vtable=client_control_slot ? status_vtable : 0;
 catalog_available.store(bool(catalog),std::memory_order_release);
 ResetLocal();
 Reset();
 ResetServer();
}
void ObservePlayerOxygenGauge(uintptr_t gauge,uintptr_t actor) {
 std::lock_guard lock(g_state_mutex);
 Binding next{};next.actor=actor;next.gauge=gauge;next.player=Player();
 if(!Ready()||!actor||!PlayerOwner(&next.owner,actor)||
    !Read(gauge,0xE2,&next.selector)||!Read(next.owner,0x58,&next.entries)||!Live(next)){
  Reset();return;
 }
 if(binding.actor==actor&&binding.gauge==gauge&&binding.owner==next.owner&&
    binding.entries==next.entries&&binding.selector==next.selector&&binding.player==next.player)return;
 Reset();binding=next;
}
bool ReadPlayerOxygenHud(uintptr_t widget, uintptr_t actor, OxygenHudSample* sample) {
 if (!sample || !g_runtime_enabled.load(std::memory_order_acquire) ||
     !catalog_available.load(std::memory_order_acquire)) return false;
 const auto config=GetConfig();
 if (!config.general.enabled || !config.oxygen_unlimited) return false;
 std::unique_lock lock(g_state_mutex,std::try_to_lock);
 if (!lock || !enabled) return false;

 OxygenHudSample next{};
 uintptr_t gauge=0, table=0, marker=0, reverse=0, owner=0, entries=0;
 if (!Read(widget,0x1B0,&gauge) || !Read(gauge,0xE2,&next.selector) ||
     !Read(gauge,0xE8,&next.shown) || !Read(gauge,0xF0,&next.shown_max) || next.shown_max<=0) return false;
 if (!Read(actor,0x68,&table) || !Read(table,0x20,&marker) ||
     !Read(marker,8,&reverse) || reverse!=actor || !Read(marker,0x18,&owner) ||
     !Read(owner,0,&reverse) || reverse!=marker || !Read(owner,0x58,&entries)) {
  *sample=next;
  return true;
 }
 next.same_player=g_player_resolve.marker==marker && g_player_resolve.resource_owner==owner;
 CatalogProof proof{};
 if (!ResolveCatalogSlot(marker,next.selector,&proof)) {
  *sample=next;
  return true;
 }
 uint16_t entry_selector=0;
 const auto entry=entries+static_cast<std::size_t>(proof.slot)*kEntryStride;
 if (!IsStatusArrayEntry(entry,entries) || !Read(entry,0,&entry_selector) ||
     entry_selector!=next.selector || !Read(entry,8,&next.stored) ||
     !Read(entry,0x10,&next.rate) || !Read(entry,0x18,&next.maximum) ||
     !Read(entry,0x28,&next.minimum) || next.maximum<=next.minimum) {
  *sample=next;
  return true;
 }
 next.raw_available=true;
 LocalBreath first{}, again{};
 if (ReadLocalBreath(actor,next.selector,&first) && first.marker==marker &&
     first.owner==owner && first.entry==entry &&
     ReadLocalBreath(actor,next.selector,&again) && first==again) {
  next.control_bound=true;
  if (local_breath!=first) {
   if (client_control_slot) ResetServer();
   local_breath=first;
   local_breath_entry.store(first.entry,std::memory_order_release);
   if (local_bind_logs++<24)
    Log("oxygen: bound controlled Breath uid=%u selector=%u source=native-local-control",
        first.uid,static_cast<unsigned>(first.selector));
  }
 }
 uintptr_t active_owner=0;
 next.entry_proved=next.same_player && Ready() && PlayerOwner(&active_owner,actor) &&
     active_owner==owner && ObserveCatalogEntry(entry,&proof);
 *sample=next;
 return true;
}

void ObservePlayerOxygenEntry(uintptr_t owner,uintptr_t entry) {
 std::lock_guard lock(g_state_mutex);
 if(!enabled)return;
 if (client_control_slot ? server_status_vtable && owner!=local_breath.owner :
                          server_actor_vtable && owner!=g_player_resolve.resource_owner) {
  uint16_t type=0;
  CatalogRow status{};
  uint32_t key=0;
  // Most native lookups are unrelated. Validate the cheap typed key before
  // touching actor ownership or the second realm's resource-bank metadata.
  if (Read(entry,0,&type) && type && type!=0xFFFF &&
      (client_control_slot ? type==local_breath.selector :
       type!=g_player_resolve.stamina_type && type!=g_player_resolve.spirit_type) &&
      CachedRow(catalog.statuses,type,&status) && Read(status.row,8,&key) && key==kBreathKey) {
   CatalogProof first{},again{};
   if (ObserveCatalogEntry(entry,&first,owner) && ObserveCatalogEntry(entry,&again,owner) && first==again &&
       (server_binding.owner!=owner || server_binding.entry!=entry ||
        (!client_control_slot && server_binding.player!=Player()))) {
    server_binding={owner,entry,client_control_slot ? decltype(server_binding.player){} : Player()};
    observed_server_entry.store(entry,std::memory_order_release);
    if(bind_logs++<32) Log("oxygen: bound server Breath owner=0x%p entry=0x%p selector=%u",
       reinterpret_cast<void*>(owner),reinterpret_cast<void*>(entry),first.selector);
   }
  }
 }
 if(!binding.owner||owner!=binding.owner)return;
 if(!Live(binding)){Reset();return;}
 if(!Entry(binding,entry))return;
 if(binding.entry==entry)return;
 binding.entry=entry;observed_entry.store(entry,std::memory_order_release);
 if(bind_logs++<32){
  int64_t current=0,maximum=0;Read(entry,8,&current);Read(entry,0x18,&maximum);
  Log("oxygen: bound player=0x%p owner=0x%p gauge=0x%p entry=0x%p selector=%u current=%lld max=%lld source=native-breath-gauge no-stored-writes=1",
   reinterpret_cast<void*>(binding.actor),reinterpret_cast<void*>(owner),reinterpret_cast<void*>(binding.gauge),
   reinterpret_cast<void*>(entry),binding.selector,static_cast<long long>(current),static_cast<long long>(maximum));
 }
}
namespace {
enum class OxygenAdjustment { Rate, Cost, Write };
bool ProtectPlayerOxygen(uintptr_t entry,int64_t value,int64_t* result,OxygenAdjustment kind) {
 if(!result||!entry||(kind!=OxygenAdjustment::Write&&value>=0))return false;
 const bool observed=entry==observed_entry.load(std::memory_order_acquire);
 const bool catalog_candidate=catalog_available.load(std::memory_order_acquire)&&
  IsStatusArrayEntry(entry,psm::authority::HealthEntryHint());
 const bool server_candidate=entry==observed_server_entry.load(std::memory_order_acquire);
 const bool control_candidate=entry==local_breath_entry.load(std::memory_order_acquire);
 if(!observed&&!catalog_candidate&&!server_candidate&&!control_candidate)return false;
 const auto config=GetConfig();
 if(!config.general.enabled||!config.oxygen_unlimited)return false;
 std::lock_guard lock(g_state_mutex);
 if(!enabled)return false;
 const char* source="native-breath-gauge";
 uint16_t selector=binding.selector;
 bool proven=false, drowning_bank=false;
 if (control_candidate && entry==local_breath.entry) {
  LocalBreath first{}, again{};
  proven=ReadLocalBreath(local_breath.actor,local_breath.selector,&first) && first==local_breath &&
      ReadLocalBreath(first.actor,first.selector,&again) && again==first;
  if (proven) {
   selector=first.selector;
   source="native-breath-hud-control";
  } else ResetLocal();
 }
 // A native local-control contract takes priority over the shared combat
 // resource tracker, which can retarget during unrelated combat updates.
 if (!proven && !client_control_slot)
     proven=entry==binding.entry&&Live(binding)&&Entry(binding,entry);
 if(!proven&&entry==binding.entry)Reset();
 if(!proven&&catalog_candidate&&!client_control_slot){
  CatalogProof first{},second{};
  proven=ObserveCatalogEntry(entry,&first)&&ObserveCatalogEntry(entry,&second)&&first==second;
  source="native-breath-catalog";
  selector=first.selector;
 }
 if (!proven && (server_candidate || (client_control_slot && catalog_candidate))) {
  uintptr_t owner=0;
  if (server_candidate && entry==server_binding.entry) {
   if (client_control_slot || server_binding.player==Player()) owner=server_binding.owner;
  } else if (client_control_slot && IsStatusArrayEntry(entry,g_player_resolve.health_entry)) {
   // A server rate read can precede the observed lookup. The tracked Health
   // bank is only a hint. The same independent proof still gates the entry.
   if (!PlayerOwner(&owner)) owner=0;
  }
  CatalogProof first{},second{};
  proven=owner && ObserveCatalogEntry(entry,&first,owner) &&
        ObserveCatalogEntry(entry,&second,owner) && first==second;
  if (proven) {
   drowning_bank=client_control_slot!=0;
   source=drowning_bank ? "native-breath-drowning-bank" : "native-breath-server-catalog";
   selector=first.selector;
   if (client_control_slot && (!server_candidate || server_binding.owner!=owner)) {
    server_binding={owner,entry,{}};
    observed_server_entry.store(entry,std::memory_order_release);
    if (bind_logs++<32) Log("oxygen: bound server Breath uid=%u selector=%u source=controlled-character",
        local_breath.uid,static_cast<unsigned>(selector));
   }
  } else if (server_candidate && entry==server_binding.entry) ResetServer();
 }
 if(!proven)return false;
 int64_t adjusted=0;
 if(kind==OxygenAdjustment::Write){
  int64_t maximum=0,bonus=0,minimum=0;
  if(!Read(entry,8,&adjusted)||!Read(entry,0x18,&maximum)||!Read(entry,0x20,&bonus)||
     !Read(entry,0x28,&minimum)||maximum<=0||bonus<0||bonus>INT64_MAX-maximum||
     minimum<0||adjusted<minimum||adjusted>maximum+bonus)return false;
  if (value>=adjusted) {
   if (drowning_bank && value>adjusted && server_refill_logs++<24)
    Log("oxygen: native server refill uid=%u current=%lld requested=%lld",
        local_breath.uid,static_cast<long long>(adjusted),static_cast<long long>(value));
   return false;
  }
 }
 *result=adjusted;
 const unsigned index=static_cast<unsigned>(kind);
 if(adjustment_logs[drowning_bank?1:0][index]++<24)Log("oxygen: suppressed %s entry=0x%p selector=%u original=%lld adjusted=%lld source=%s stored-rate-unchanged=1",
  kind==OxygenAdjustment::Rate?"rate":kind==OxygenAdjustment::Cost?"cost":"stat-write",
  reinterpret_cast<void*>(entry),selector,static_cast<long long>(value),static_cast<long long>(adjusted),source);
 return true;
}
}
bool TryLockPlayerOxygen(uintptr_t entry,int64_t value,int64_t* result,bool rate) {
 return ProtectPlayerOxygen(entry,value,result,rate?OxygenAdjustment::Rate:OxygenAdjustment::Cost);
}
bool TryPreservePlayerOxygenWrite(uintptr_t entry,int64_t requested,int64_t* result) {
 return ProtectPlayerOxygen(entry,requested,result,OxygenAdjustment::Write);
}
