#pragma once
#include "item_3446.h"
#include "menu_item_gain_512_profiles.h"
#include "menu_item_gain_514_profiles.h"
#include "menu_item_gain_516_profiles.h"
#include <span>
namespace psm::menu_gain512 {
enum class State {Absent,Unsupported,NotReady,Ready};
struct Probe {const char* reason="provider-absent";std::size_t rva=0;};
struct Target {
 uintptr_t worker=0,caller=0;
 State state=State::Absent;
 Probe probe{};
 bool capacity_wrapper=false;
 const v3446::Profile* worker_profile=nullptr;
 const char* provider_name=nullptr;
};
inline bool ExactAt(const v3446::Image& image,std::size_t at,const v3446::Profile& p){
 return image.Exec(at,p.size)&&image.Fragments(at,p)&&!std::memcmp(image.data+at,p.code,p.size);
}
struct Edge { std::size_t at, target; unsigned char opcode; };
struct Unwind {
 std::size_t start, rva;
 const unsigned char* bytes;
 std::size_t size;
};
#include "menu_item_gain_3663_profiles.h"
#include "menu_item_gain_3663_menu601_profiles.h"
struct OwnedReceipt {uintptr_t target=0,callback=0,trampoline=0;const unsigned char* original=nullptr;std::size_t size=0;bool enabled=false;};
struct ReceiptSnapshot {std::array<unsigned char,4096> native{};std::array<unsigned char,64> trampoline{};std::array<unsigned char,32> original{};bool operator==(const ReceiptSnapshot&)const=default;};
inline bool ReceiptOwnObservation(const v3446::Image& image,const OwnedReceipt& own,ReceiptSnapshot& out,const capture_compat::Reader& reader={},
                                  std::size_t rva=0xD6FF310,const v3446::Profile& profile=kNativeReceiptBody){
 const auto entry=reinterpret_cast<uintptr_t>(image.data)+rva;
 if(!own.enabled||own.target!=entry||!own.callback||!own.trampoline||!own.original||(own.size!=5&&own.size!=17)||
    !image.Exec(rva,profile.size)||!image.Fragments(rva,profile)||
    !reader.Read(entry,out.native.data(),profile.size)||
    !reader.Read(reinterpret_cast<uintptr_t>(own.original),out.original.data(),own.size)||
    std::memcmp(out.original.data(),profile.code,own.size)||
    std::memcmp(out.native.data()+own.size,profile.code+own.size,profile.size-own.size))return false;
 const auto bytes=own.size+(own.size==5?19:14);
 if(own.trampoline>(std::numeric_limits<uintptr_t>::max)()-bytes||!reader.Exec(own.trampoline,bytes)||
    !reader.Read(own.trampoline,out.trampoline.data(),bytes)||std::memcmp(out.trampoline.data(),out.original.data(),own.size))return false;
 if(own.size==5){
  if(out.native[0]!=0xE9||out.trampoline[5]!=0xE9)return false;int32_t entryDelta=0,continuationDelta=0;
  std::memcpy(&entryDelta,out.native.data()+1,4);std::memcpy(&continuationDelta,out.trampoline.data()+6,4);
  if(std::int64_t(entry)+5+entryDelta!=std::int64_t(own.trampoline)+10||
     std::int64_t(own.trampoline)+10+continuationDelta!=std::int64_t(entry)+5||
     !capture_compat::AbsoluteJump(out.trampoline.data()+10,own.callback))return false;
 }else{
  if(!capture_compat::AbsoluteJump(out.native.data(),own.callback)||
     !capture_compat::AbsoluteJump(out.trampoline.data()+17,entry+17))return false;
  for(unsigned i=14;i<17;++i)if(out.native[i]!=0x90)return false;
 }
 return image.Fragments(rva,profile);
}
inline bool ReceiptReady(const v3446::Image& image,const OwnedReceipt& own,const capture_compat::Reader& reader={}){
 if(ExactAt(image,0xD6FF310,kNativeReceiptBody))return true;
 ReceiptSnapshot a{},b{};return ReceiptOwnObservation(image,own,a,reader)&&ReceiptOwnObservation(image,own,b,reader)&&a==b;
}
#include "menu_item_gain_3663.h"

inline Target Resolve(const void* game,const void* menu,const OwnedReceipt& own={}){
 if (const auto current = current3663::Resolve(game, menu, own); current.state != State::Absent)
  return current;
 Target out{};if(!menu)return out;out.state=State::Unsupported;
 const auto fail=[&](const char* reason,std::size_t rva=0){out.probe={reason,rva};return out;};
 const auto pending=[&](const char* reason,std::size_t rva){out.state=State::NotReady;return fail(reason,rva);};
 v3446::Image provider,native;
 if(!provider.Init(menu,v3446::ModuleSize(menu))||!native.Init(game,v3446::ModuleSize(game))||
    provider.nt.OptionalHeader.CheckSum||
    native.nt.FileHeader.TimeDateStamp!=0x6AED2E85u||native.size!=0x13CC4000||native.nt.OptionalHeader.CheckSum!=0x137C1424u)return fail("image-identity");
 const bool menu516_active=provider.nt.FileHeader.TimeDateStamp==0x847F423Du&&provider.size==0xA1E000;
 const bool current=provider.nt.FileHeader.TimeDateStamp==0x7CCDF8C0u&&provider.size==0x990000;
 if(!menu516_active&&!current&&(provider.nt.FileHeader.TimeDateStamp!=0x1BB5DE60u||provider.size!=0x95E000))return fail("image-identity");
 const auto context=capture_compat::Context::From(menu);if(!context.valid)return fail("capture-provider");
 if(menu516_active){
  const std::pair<std::size_t,const v3446::Profile*> profiles[]={
   {menu516::kWorkerRva,&menu516::kWorker},
   {menu516::kQueueRva,&menu516::kQueue},
   {menu516::kPlanRva,&menu516::kPlan},
   {menu516::kExpectedRva,&menu516::kExpected},
   {menu516::kCommitRva,&menu516::kCommit},
   {menu516::kContextRva,&menu516::kContext},
   {menu516::kCleanupRva,&menu516::kCleanup},
   {menu516::kSnapshotRva,&menu516::kSnapshot},
   {menu516::kSamePairRva,&menu516::kSamePair},
   {menu516::kSnapshotEqualRva,&menu516::kSnapshotEqual},
   {menu516::kInstanceRva,&menu516::kInstance},
   {menu516::kClientRva,&menu516::kClient},
   {menu516::kAuthorityRva,&menu516::kAuthority},
   {menu516::kIdentityRva,&menu516::kIdentity},
   {menu516::kMetadataRva,&menu516::kMetadata}
  };
  for(const auto& [rva,profile]:profiles)
   if(!ExactAt(provider,rva,*profile))return fail("menu-code",rva);
  for(const auto& [site,target]:menu516::kEdges)
   if(provider.Branch(site,0xE8)!=target)return fail("menu-edge",site);
 } else if(current){
  const std::pair<std::size_t,const v3446::Profile*> profiles[]={
   {menu514::kWorkerRva,&menu514::kWorker},
   {menu514::kQueueRva,&menu514::kQueue},
   {menu514::kPlanRva,&menu514::kPlan},
   {menu514::kExpectedRva,&menu514::kExpected},
   {menu514::kCommitRva,&menu514::kCommit},
   {menu514::kContextRva,&menu514::kContext},
   {menu514::kCleanupRva,&menu514::kCleanup},
   {menu514::kSnapshotRva,&menu514::kSnapshot},
   {menu514::kSamePairRva,&menu514::kSamePair},
   {menu514::kSnapshotEqualRva,&menu514::kSnapshotEqual},
   {menu514::kInstanceRva,&menu514::kInstance},
   {menu514::kClientRva,&menu514::kClient},
   {menu514::kAuthorityRva,&menu514::kAuthority},
   {menu514::kIdentityRva,&menu514::kIdentity},
   {menu514::kMetadataRva,&menu514::kMetadata}
  };
  for(const auto& [rva,profile]:profiles)
   if(!ExactAt(provider,rva,*profile))return fail("menu-code",rva);
  for(const auto& [site,target]:menu514::kEdges)
   if(provider.Branch(site,0xE8)!=target)return fail("menu-edge",site);
 }else{
  if(!ExactAt(provider,0x76550,kWorker))return fail("menu-code",0x76550);
  if(!ExactAt(provider,0x71520,kQueue))return fail("menu-code",0x71520);
  if(!ExactAt(provider,0x76000,kPlan))return fail("menu-code",0x76000);
  if(!ExactAt(provider,0x706E0,kExpected))return fail("menu-code",0x706E0);
  if(!ExactAt(provider,0x6E480,kCommit))return fail("menu-code",0x6E480);
  if(!ExactAt(provider,0x6E620,kContext))return fail("menu-code",0x6E620);
  if(!ExactAt(provider,0x6E330,kCleanup))return fail("menu-code",0x6E330);
  if(!ExactAt(provider,0x6CE60,kSnapshot))return fail("menu-code",0x6CE60);
  if(!ExactAt(provider,0x7C660,kSamePair))return fail("menu-code",0x7C660);
  if(!ExactAt(provider,0x66110,kSnapshotEqual))return fail("menu-code",0x66110);
  if(!ExactAt(provider,0x70B10,kInstance))return fail("menu-code",0x70B10);
  if(!ExactAt(provider,0x6E8C0,kClient))return fail("menu-code",0x6E8C0);
  if(!ExactAt(provider,0x70F70,kAuthority))return fail("menu-code",0x70F70);
  if(!ExactAt(provider,0x71D80,kIdentity))return fail("menu-code",0x71D80);
  if(!ExactAt(provider,0x6EC90,kMetadata))return fail("menu-code",0x6EC90);
  if(provider.Branch(0x76550+0x170,0xE8)!=0x6EC90)return fail("menu-edge",0x76550+0x170);
  if(provider.Branch(0x76550+0x17D,0xE8)!=0x6E8C0)return fail("menu-edge",0x76550+0x17D);
  if(provider.Branch(0x76550+0x185,0xE8)!=0x70F70)return fail("menu-edge",0x76550+0x185);
  if(provider.Branch(0x76550+0x368,0xE8)!=0x6E620)return fail("menu-edge",0x76550+0x368);
  if(provider.Branch(0x76550+0x391,0xE8)!=0x6E620)return fail("menu-edge",0x76550+0x391);
  if(provider.Branch(0x76550+0x3B0,0xE8)!=0x6CE60)return fail("menu-edge",0x76550+0x3B0);
  if(provider.Branch(0x76550+0x3CF,0xE8)!=0x6CE60)return fail("menu-edge",0x76550+0x3CF);
  if(provider.Branch(0x76550+0x3F6,0xE8)!=0x7C660)return fail("menu-edge",0x76550+0x3F6);
  if(provider.Branch(0x76550+0x40A,0xE8)!=0x71D80)return fail("menu-edge",0x76550+0x40A);
  if(provider.Branch(0x76550+0x465,0xE8)!=0x71D80)return fail("menu-edge",0x76550+0x465);
  if(provider.Branch(0x76550+0x4D4,0xE8)!=0x70B10)return fail("menu-edge",0x76550+0x4D4);
  if(provider.Branch(0x76550+0x4F2,0xE8)!=0x70B10)return fail("menu-edge",0x76550+0x4F2);
  if(provider.Branch(0x76550+0x515,0xE8)!=0x76000)return fail("menu-edge",0x76550+0x515);
  if(provider.Branch(0x76550+0x533,0xE8)!=0x76000)return fail("menu-edge",0x76550+0x533);
  if(provider.Branch(0x76550+0x55E,0xE8)!=0x706E0)return fail("menu-edge",0x76550+0x55E);
  if(provider.Branch(0x76550+0x589,0xE8)!=0x706E0)return fail("menu-edge",0x76550+0x589);
  if(provider.Branch(0x76550+0x5A8,0xE8)!=0x7C660)return fail("menu-edge",0x76550+0x5A8);
  if(provider.Branch(0x76550+0x801,0xE8)!=0x66110)return fail("menu-edge",0x76550+0x801);
  if(provider.Branch(0x76550+0xA51,0xE8)!=0x66110)return fail("menu-edge",0x76550+0xA51);
  if(provider.Branch(0x76550+0xA65,0xE8)!=0x6E480)return fail("menu-edge",0x76550+0xA65);
  if(provider.Branch(0x76550+0xCC8,0xE8)!=0x66110)return fail("menu-edge",0x76550+0xCC8);
  if(provider.Branch(0x76550+0xF27,0xE8)!=0x66110)return fail("menu-edge",0x76550+0xF27);
  if(provider.Branch(0x76550+0xF37,0xE8)!=0x6E330)return fail("menu-edge",0x76550+0xF37);
  if(provider.Branch(0x76550+0xF46,0xE8)!=0x6E330)return fail("menu-edge",0x76550+0xF46);
  if(provider.Branch(0x76550+0xF8A,0xE8)!=0x66110)return fail("menu-edge",0x76550+0xF8A);
  if(provider.Branch(0x76550+0xF9E,0xE8)!=0x6E480)return fail("menu-edge",0x76550+0xF9E);
  if(provider.Branch(0x76550+0x11EE,0xE8)!=0x66110)return fail("menu-edge",0x76550+0x11EE);
  if(provider.Branch(0x76550+0x11FE,0xE8)!=0x6E330)return fail("menu-edge",0x76550+0x11FE);
  if(provider.Branch(0x76550+0x120D,0xE8)!=0x6E330)return fail("menu-edge",0x76550+0x120D);
  if(provider.Branch(0x76550+0x121B,0xE8)!=0x6E330)return fail("menu-edge",0x76550+0x121B);
  if(provider.Branch(0x76550+0x1227,0xE8)!=0x6E330)return fail("menu-edge",0x76550+0x1227);
  if(provider.Branch(0x76550+0x124B,0xE8)!=0x6E330)return fail("menu-edge",0x76550+0x124B);
  if(provider.Branch(0x76550+0x125A,0xE8)!=0x6E330)return fail("menu-edge",0x76550+0x125A);
  if(provider.Branch(0x76550+0x1280,0xE8)!=0x6CE60)return fail("menu-edge",0x76550+0x1280);
  if(provider.Branch(0x76550+0x12A0,0xE8)!=0x66110)return fail("menu-edge",0x76550+0x12A0);
  if(provider.Branch(0x76550+0x12B7,0xE8)!=0x6CE60)return fail("menu-edge",0x76550+0x12B7);
  if(provider.Branch(0x76550+0x12E3,0xE8)!=0x66110)return fail("menu-edge",0x76550+0x12E3);
  if(provider.Branch(0x76550+0x13E9,0xE8)!=0x66110)return fail("menu-edge",0x76550+0x13E9);
  if(provider.Branch(0x76550+0x185B,0xE8)!=0x6E330)return fail("menu-edge",0x76550+0x185B);
  if(provider.Branch(0x76550+0x1868,0xE8)!=0x6E330)return fail("menu-edge",0x76550+0x1868);
  if(provider.Branch(0x71520+0x18,0xE8)!=0x6E8C0)return fail("menu-edge",0x71520+0x18);
  if(provider.Branch(0x71520+0x35A,0xE8)!=0x76550)return fail("menu-edge",0x71520+0x35A);
  if(provider.Branch(0x70F70+0x13,0xE8)!=0x6E8C0)return fail("menu-edge",0x70F70+0x13);
 }
 if(!ExactAt(native,0x25FED70,kNativeCtor))return fail("native-code",0x25FED70);
 if(!ExactAt(native,0x22F6680,kNativeCommit))return fail("native-code",0x22F6680);
 if(!ExactAt(native,0x3D8090,kNativeFree))return fail("native-code",0x3D8090);
 if(!ExactAt(native,0x88E7500,kNativeFreeBody))return fail("native-code",0x88E7500);
 if(!ExactAt(native,0x22F1FC0,kNativeSpecialThunk))return fail("native-code",0x22F1FC0);
 if(!ExactAt(native,0xCB0C780,kNativeSpecialBody))return fail("native-code",0xCB0C780);
 if(!ExactAt(native,0x25FCA40,kNativeReceiptThunk))return fail("native-code",0x25FCA40);
 if(native.Branch(0x22F6741)!=0x25FCA40||native.Branch(0x25FCA40,0xE9)!=0xD6FF310||
    native.Branch(0x22F1FC0,0xE9)!=0xCB0C780||native.Branch(0x3D8090,0xE9)!=0x88E7500)return fail("native-edges");
 if(!ReceiptReady(native,own))return fail("receipt-owner",0xD6FF310);

 constexpr std::size_t capacity_rva=0x25FCDB0;
 const std::size_t slot_base=menu516_active?0x4E1668:(current?0x4D0088:0x4A1538),capacity_slot=slot_base+8;
 constexpr auto& capacity_profile=items3446::kBorrowCapacityPlanner;
 const std::pair<std::size_t,std::size_t> bindings[]={
  {slot_base,0x25FED70},{capacity_slot,capacity_rva},{slot_base+16,0x22F6680},{slot_base+24,0x3D8090}
 };
 const auto game_base=reinterpret_cast<uintptr_t>(game);
 const auto menu_base=reinterpret_cast<uintptr_t>(menu);

 // The role 1 factory result's +0x10 is the native entry used for logging.
 // Menu publishes the upper qword (+0x18), its exported typed CapacityCall,
 // to the exact build-specific planner slot.
 // Only the exact wrapper backed by the exact live capture may be accepted.
 capture_compat::Snapshot first{},second{};
 const bool stock=capture_compat::ObserveStrict(native,capacity_profile,capacity_rva,first);
 if(!stock&&!capture_compat::Observe(native,capacity_profile,capacity_rva,context.capacity,first))
  return pending("capacity-owner",capacity_rva);

 std::array<uintptr_t,std::size(bindings)> values{};
 std::size_t pending_slot=0;
 for(std::size_t i=0;i<values.size();++i){
  const auto [slot,rva]=bindings[i];
  auto& value=values[i];
  if(!capture_compat::Context::DataSection(provider,slot,8,true)||!provider.Read(slot,value))
   return fail("binding-read",slot);
  if(!value){if(!pending_slot)pending_slot=slot;continue;}
  if(value==game_base+rva)continue;
  if(slot!=capacity_slot||value!=menu_base+(menu516_active?capture_compat::menu516::kCapacityCallRva:(current?capture_compat::menu514::kCapacityCallRva:0x65DF0))||value!=context.capacity_call)
   return fail("binding-target",slot);
  if(stock)return pending("capacity-wrapper-unbound",slot);
  out.capacity_wrapper=true;
 }
 if(pending_slot)return pending("binding-pending",pending_slot);

 // Verify the same stock/capture shape and the exact same slots across
 // discovery. The publish path repeats this whole proof before enabling.
 const bool capacity_same=stock
  ?capture_compat::ObserveStrict(native,capacity_profile,capacity_rva,second)
  :capture_compat::Observe(native,capacity_profile,capacity_rva,context.capacity,second);
 if(!capacity_same||first!=second)return pending("capacity-changed",capacity_rva);
 for(std::size_t i=0;i<values.size();++i){
  uintptr_t value=0;
  if(!provider.Read(bindings[i].first,value)||value!=values[i])
   return pending("binding-changed",bindings[i].first);
 }
 out.worker=menu_base+(menu516_active?menu516::kWorkerRva:(current?menu514::kWorkerRva:0x76550));
 out.caller=menu_base+(menu516_active?menu516::kQueueRva+0x35F:(current?menu514::kQueueRva+0x35F:0x7187F));
 out.worker_profile=menu516_active?&menu516::kWorker:(current?&menu514::kWorker:&kWorker);
 out.provider_name=menu516_active?"Menu15A1":(current?"Menu6AE2":"Menu1A69");
 out.state=State::Ready;out.probe={"ready",0};return out;
}
struct StartupRetry {
 uint64_t deadline=0,next=0;bool active=false;
 void Begin(uint64_t now){deadline=now+30000;next=now;active=true;}
 bool Due(uint64_t now){if(!active)return false;if(now>=deadline){active=false;return false;}if(now<next)return false;next=now+1000;return true;}
 void Stop(){active=false;}
};
}
