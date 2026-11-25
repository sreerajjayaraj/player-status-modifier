#include "config.h"
#include "runtime/item_receipt.h"
#include "runtime/runtime_state.h"
#include "runtime/item_timing.h"
#include <intrin.h>
#include <Windows.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>

uintptr_t RunBorrowedItemPlanTest(uintptr_t,uint32_t*,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
uintptr_t RunMoveParentTest(uintptr_t,uint32_t*,uintptr_t,uintptr_t,uint16_t,uintptr_t,uint8_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
void SetItemReceiptTestMetadata3446(uintptr_t);
void SetItemPlanOwnerLookupTest(uintptr_t);
bool InstallItemTimingTestHooks(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
void RemoveItemTimingTestHooks();
bool InstallMoveParentHookTest(uintptr_t);
void DisableMoveParentHookTest();void RemoveMoveParentHookTest();
namespace {
template<class T> void Put(void* p,size_t off,T v){std::memcpy(static_cast<std::byte*>(p)+off,&v,sizeof(v));}
template<class T> T Read(uintptr_t p,size_t off){T v{};std::memcpy(&v,reinterpret_cast<void*>(p+off),sizeof(v));return v;}
struct Fixture {
 alignas(16) std::array<std::byte,0x4000> image{};
 alignas(16) std::array<std::byte,0x400> behavior{};
 std::array<std::byte,0x30> groups{};
 std::array<psm::items::Receipt,4> items{};
 std::array<std::byte,0x28> removal{};
 std::array<std::byte,0x500> metadata{};
 std::array<std::byte,0x120> owner{};
 std::array<std::byte,0x20> container{};
 std::array<psm::items::Receipt,2> existing{};
 uintptr_t container_pointer=0,entries_override=0;
 uint32_t count_override=0,capacity_override=2;
 bool owner_available=true;
 uintptr_t expected_owner_actor=0,owner_seen=0;
 std::array<std::byte,0x120> source_owner{};std::array<std::byte,0x20> source_container{};
 std::array<uintptr_t,2> shared_containers{};uintptr_t source_container_pointer=0;
 bool owner_alias=false,container_alias=false;
 int64_t current_second=0,singleton_stack=1;
 void SyncOwner(){
  container_pointer=reinterpret_cast<uintptr_t>(container.data());Put(owner.data(),0x18,&container_pointer);Put(owner.data(),0x20,uint32_t{1});
  Put(container.data(),0x10,Read<uint16_t>(reinterpret_cast<uintptr_t>(groups.data()),0));
  source_container_pointer=reinterpret_cast<uintptr_t>(source_container.data());Put(source_container.data(),0x10,Read<uint16_t>(reinterpret_cast<uintptr_t>(removal.data()),0x10));
  Put(source_owner.data(),0x18,&source_container_pointer);Put(source_owner.data(),0x20,uint32_t{1});
  if(container_alias){source_container_pointer=container_pointer;}
  if(owner_alias){shared_containers={container_pointer,source_container_pointer};Put(owner.data(),0x18,shared_containers.data());Put(owner.data(),0x20,uint32_t{container_pointer==source_container_pointer?1u:2u});}

  const uint32_t count=count_override?count_override:(current_second>0?2:current>0?1:0);
  Put(container.data(),0,entries_override?entries_override:reinterpret_cast<uintptr_t>(existing.data()));
  Put(container.data(),8,count);Put(container.data(),0xC,capacity_override);
  Put(existing[0].data(),8,uint16_t{42});Put(existing[0].data(),0x10,current);
  Put(existing[1].data(),8,uint16_t{45});Put(existing[1].data(),0x10,current_second);
 }
 std::array<std::array<std::byte,0xE0>,8> outputs{};
 uintptr_t player=0x20000000;
 uint32_t output_count=0,available_slots=8;
 int64_t stack=100,current=0,charge=7,source=50,observed=0;
 uintptr_t borrowed_groups=0,borrowed_items=0;
 bool nested=false,raise=false,fail=false;
 Fixture* child=nullptr;
 size_t push_offset=0;
 uintptr_t push(){return reinterpret_cast<uintptr_t>(behavior.data())+push_offset;}
 uintptr_t parent(){return reinterpret_cast<uintptr_t>(behavior.data());}
 void Fallback(){
  Reset(0x18,false);std::memmove(behavior.data()+0x98,behavior.data(),0x80);std::memset(behavior.data(),0,0x98);push_offset=0x98;
  Type(0,0x1800,".?AVMoveItemBehavior_InventoryToOtherInventory@pa@@");
  Type(0x30,0x1000,".?AVItemPopInventory@pa@@");
  Put(behavior.data(),0x40,player);Put(behavior.data(),0x80,removal.data());Put(behavior.data(),0x88,uint32_t{1});
  Put(removal.data(),0,uint16_t{42});Put(removal.data(),0x10,uint16_t{1});Put(removal.data(),0x18,int64_t{1});
  Put(behavior.data(),0xA8,uintptr_t{0x30000000});expected_owner_actor=0x30000000;
 }
 uintptr_t base(){return reinterpret_cast<uintptr_t>(image.data());}
 void Type(size_t object,uint32_t rva,const char* name){
  Put(behavior.data(),object,base()+rva);Put(image.data(),rva-8,base()+rva+0x20);
  Put(image.data(),rva+0x20,uint32_t{1});Put(image.data(),rva+0x2C,rva+0x40);
  std::memcpy(image.data()+rva+0x50,name,std::strlen(name)+1);
 }
 void Reset(uint16_t mode=0x37,bool transfer=true){
  push_offset=0;image={};behavior={};groups={};items={};removal={};outputs={};metadata={};owner={};container={};existing={};entries_override=0;count_override=0;capacity_override=2;owner_available=true;current_second=0;singleton_stack=1;
  expected_owner_actor=player;owner_seen=0;owner_alias=container_alias=false;source_owner={};source_container={};
  output_count=0;available_slots=8;stack=100;current=0;charge=7;source=50;observed=0;
  nested=raise=fail=false;child=nullptr;borrowed_groups=borrowed_items=0;
  Put(image.data(),offsetof(IMAGE_DOS_HEADER,e_lfanew),int32_t{0x100});
  reinterpret_cast<IMAGE_NT_HEADERS64*>(image.data()+0x100)->OptionalHeader.SizeOfImage=DWORD(image.size());
  Type(0,0x800,".?AVItemPushInventory@pa@@");
  Put(behavior.data(),0x10,player);Put(behavior.data(),0x48,groups.data());Put(behavior.data(),0x50,uint32_t{1});
  Put(behavior.data(),0x68,mode);Put(groups.data(),0,uint16_t{15});Put(groups.data(),8,items.data());
  Put(groups.data(),0x10,uint32_t{1});Put(groups.data(),0x14,uint32_t{4});
  Put(items[0].data(),8,uint16_t{42});Put(items[0].data(),0x10,int64_t{1});Put(items[0].data(),0,std::numeric_limits<uint64_t>::max());
  Put(metadata.data(),0x10,int64_t{100});
  if(transfer){
   Type(0x80,0x1000,".?AVItemPopInventory@pa@@");Put(behavior.data(),0x90,player);
   Put(behavior.data(),0xD0,removal.data());Put(behavior.data(),0xD8,uint32_t{1});
   Put(removal.data(),0,uint16_t{42});Put(removal.data(),0x10,uint16_t{1});Put(removal.data(),0x18,int64_t{1});
  }
 }
};
Fixture* active=nullptr;
uintptr_t __fastcall Lookup(uintptr_t,uint32_t key){
 if(!active)return 0;
 Put(active->metadata.data(),8,uint32_t{key==45?10042u:10000u+key});
 Put(active->metadata.data(),0x10,key==43?active->singleton_stack:active->stack);
 return reinterpret_cast<uintptr_t>(active->metadata.data());
}
uintptr_t __fastcall OwnerLookup(uintptr_t actor){
 if(!active||!active->owner_available)return 0;
 active->SyncOwner();
 if(actor==active->player&&active->expected_owner_actor!=active->player)return reinterpret_cast<uintptr_t>(active->owner_alias?active->owner.data():active->source_owner.data());
 if(actor!=active->expected_owner_actor)return 0;
 active->owner_seen=actor;return reinterpret_cast<uintptr_t>(active->owner.data());
}
uintptr_t __fastcall Planner(uintptr_t push,uint32_t* error){
 auto& f=*active;
 const auto groups=Read<uintptr_t>(push,0x48);const auto count=Read<uint32_t>(push,0x50);
 if(count>2){*error=98;return reinterpret_cast<uintptr_t>(error);}
 if(f.raise){RaiseException(0xE0123456,0,0,nullptr);}
 if(f.nested){f.nested=false;return RunBorrowedItemPlanTest(push,error,f.base(),f.player,reinterpret_cast<uintptr_t>(Planner),0x123456);}
 if(f.child){
  auto* child=f.child;f.child=nullptr;auto changed=GetConfig();changed.items.gain_multiplier=2;SetConfigSnapshot(L"",changed);
  active=child;uint32_t child_error=99;RunBorrowedItemPlanTest(child->push(),&child_error,child->base(),child->player,reinterpret_cast<uintptr_t>(Planner),0x123456);active=&f;
 }
 f.borrowed_groups=groups;f.borrowed_items=Read<uintptr_t>(groups,8);f.output_count=0;
 for(uint32_t g=0;g<count;++g){
  const auto group=groups+g*0x18;const auto items=Read<uintptr_t>(group,8);const auto n=Read<uint32_t>(group,0x10);
  if(n>4){*error=97;return reinterpret_cast<uintptr_t>(error);}
  for(uint32_t i=0;i<n;++i){
   const auto input=items+i*0xC8;auto quantity=Read<int64_t>(input,0x10);f.observed=quantity;
   if(f.fail||quantity<=0||quantity>800){*error=17;return reinterpret_cast<uintptr_t>(error);}
   while(quantity){
    if(f.output_count>=f.available_slots){*error=19;return reinterpret_cast<uintptr_t>(error);}
    const auto room=f.output_count==0?f.stack-f.current:f.stack;
    const auto chunk=quantity<room?quantity:room;
    auto& out=f.outputs[f.output_count++];std::memcpy(out.data(),reinterpret_cast<void*>(input),0xC8);Put(out.data(),0x10,chunk);
    quantity-=chunk;
   }
  }
 }
 Put(reinterpret_cast<void*>(push),0x58,f.outputs.data());Put(reinterpret_cast<void*>(push),0x60,f.output_count);
 *error=0;return reinterpret_cast<uintptr_t>(error);
}
std::array<uintptr_t,8> parent_args{};DWORD parent_incoming_error=0;
__declspec(noinline) uintptr_t __fastcall ParentPassThrough(uintptr_t parent,uint32_t* error,uintptr_t source,uintptr_t destination,uint16_t container,uintptr_t requests,uint8_t option,uintptr_t context){
 parent_incoming_error=GetLastError();parent_args={parent,reinterpret_cast<uintptr_t>(error),source,destination,container,requests,option,context};*error=0xBADC0DE;SetLastError(0x12345678);return reinterpret_cast<uintptr_t>(error);
}
uintptr_t parent_plan_caller=0x456789;bool parent_raise=false,parent_nested_wrong=false,parent_other=false,parent_torn=false;
std::array<std::byte,16> request_vector{};
uintptr_t __fastcall ParentNative(uintptr_t parent,uint32_t* error,uintptr_t source,uintptr_t destination,uint16_t container,uintptr_t requests,uint8_t option,uintptr_t context){
 parent_args={parent,reinterpret_cast<uintptr_t>(error),source,destination,container,requests,option,context};
 if(parent_raise){SetLastError(0x12345678);RaiseException(0xE0123457,0,0,nullptr);}
 if(parent_torn)Put(reinterpret_cast<void*>(requests),8,uint32_t{2});
 if(parent_nested_wrong){parent_nested_wrong=false;return RunMoveParentTest(parent,error,source,destination,container,requests,option,context,active->base(),active->player,reinterpret_cast<uintptr_t>(ParentNative),0x777777);}
 if(parent_other){parent_other=false;Fixture other;other.Fallback();auto* outer=active;active=&other;RunBorrowedItemPlanTest(other.push(),error,other.base(),other.player,reinterpret_cast<uintptr_t>(Planner),parent_plan_caller);active=outer;return reinterpret_cast<uintptr_t>(error);}
 const auto result=RunBorrowedItemPlanTest(parent+0x98,error,active->base(),active->player,reinterpret_cast<uintptr_t>(Planner),parent_plan_caller);SetLastError(0x12345678);return result;
}
bool CatchParentRaise(Fixture* f){
 uint32_t error=99;
 __try{RunMoveParentTest(f->parent(),&error,f->player,0x30000000,15,reinterpret_cast<uintptr_t>(request_vector.data()),0xA5,0xABCDEF,f->base(),f->player,reinterpret_cast<uintptr_t>(ParentNative),0x234567);}
 __except(GetExceptionCode()==0xE0123457?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}return false;
}
uintptr_t timing_plan_return=0,timing_push_return=0,timing_receipt_return=0;
bool timing_reset_in_push=false;
DWORD timing_plan_in=0,timing_push_in=0,timing_receipt_in=0;volatile unsigned timing_barrier=0;int timing_raise=0;
std::array<uintptr_t,5> timing_receipt_args{};
__declspec(noinline) uintptr_t __fastcall TimingReceiptNative(uintptr_t container,uint32_t*error,const void* incoming,uint16_t slot,uint8_t flag){
 timing_receipt_in=GetLastError();timing_receipt_return=reinterpret_cast<uintptr_t>(_ReturnAddress());
 timing_receipt_args={container,reinterpret_cast<uintptr_t>(error),reinterpret_cast<uintptr_t>(incoming),slot,flag};
 if(timing_raise==3){SetLastError(0xA003);RaiseException(0xE0123459,0,0,nullptr);}
 *error=17;SetLastError(0xCAFE1234);return reinterpret_cast<uintptr_t>(error);
}
__declspec(noinline) uintptr_t __fastcall TimingPushNative(uintptr_t push,uint32_t*error){
 timing_push_in=GetLastError();timing_push_return=reinterpret_cast<uintptr_t>(_ReturnAddress());
 if(timing_reset_in_push){timing_reset_in_push=false;psm::item_timing::Enable(true);}
 if(timing_raise==2){SetLastError(0xA002);RaiseException(0xE0123458,0,0,nullptr);}
 const auto r=TimingReceiptNative(reinterpret_cast<uintptr_t>(active->container.data()),error,active->outputs[0].data(),93,0xA5);
 timing_barrier=timing_barrier+1;SetLastError(0xDEAD1234);return r;
}
__declspec(noinline) uintptr_t __fastcall TimingPlanNative(uintptr_t push,uint32_t*error){
 timing_plan_in=GetLastError();timing_plan_return=reinterpret_cast<uintptr_t>(_ReturnAddress());
 if(timing_raise==1){SetLastError(0xA001);RaiseException(0xE0123456,0,0,nullptr);}
 auto r=Planner(push,error);SetLastError(0xBEEF1234);return r;
}
__declspec(noinline) uintptr_t TimingPlanCaller(uintptr_t push,uint32_t*error){auto r=TimingPlanNative(push,error);timing_barrier=timing_barrier+1;return r;}
__declspec(noinline) uintptr_t TimingPushCaller(uintptr_t push,uint32_t*error){auto r=TimingPushNative(push,error);timing_barrier=timing_barrier+1;return r;}
bool CatchTimingRaise(Fixture*f,int kind){
 uint32_t error=99;
 __try{if(kind==1)TimingPlanCaller(f->push(),&error);else TimingPushCaller(f->push(),&error);}
 __except((GetExceptionCode()==0xE0123456||GetExceptionCode()==0xE0123458||GetExceptionCode()==0xE0123459)?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}return false;
}
bool CatchNativeRaise(Fixture* f){
 uint32_t error=99;
 __try { RunBorrowedItemPlanTest(f->push(),&error,f->base(),f->player,reinterpret_cast<uintptr_t>(Planner),0x123456); }
 __except(GetExceptionCode()==0xE0123456?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}
 return false;
}
}
int TestBorrowedItemPlans(){
 int failures=0;Fixture f;active=&f;
 const auto check=[&](bool ok,const char* label){std::printf("%s: borrowed native-plan %s\n",ok?"PASS":"FAIL",label);if(!ok)++failures;};
 ModConfig config{};config.general.enabled=true;config.items.gain_multiplier=3;SetConfigSnapshot(L"",config);g_runtime_enabled.store(true);
 SetItemReceiptTestMetadata3446(reinterpret_cast<uintptr_t>(Lookup));
 SetItemPlanOwnerLookupTest(reinterpret_cast<uintptr_t>(OwnerLookup));
 const auto run=[&](uintptr_t player=0x20000000,uintptr_t caller=0x123456){uint32_t error=99;RunBorrowedItemPlanTest(f.push(),&error,f.base(),player,reinterpret_cast<uintptr_t>(Planner),caller);return error;};
 const auto qty=[&](size_t i=0){return Read<int64_t>(reinterpret_cast<uintptr_t>(f.outputs[i].data()),0x10);};
 f.Reset();const auto original=f.items;const auto removal=f.removal;const auto groups=f.groups;
 check(run()==0&&qty()==3,"transfer original 1 reaches native planner as 3");
 check(f.items==original&&f.removal==removal&&f.groups==groups&&Read<uintptr_t>(f.push(),0x48)==reinterpret_cast<uintptr_t>(f.groups.data()),"source deduction request and original groups/items restored");
 check(f.borrowed_groups!=reinterpret_cast<uintptr_t>(f.groups.data())&&f.borrowed_items!=reinterpret_cast<uintptr_t>(f.items.data()),"native planner receives private group and item copies");
 check(f.source==50&&f.charge==7,"source stock and charge input remain original");
 check(qty()==3,"owned SQL/client receipts survive private input release");
 f.Reset(7,false);check(run()==0&&qty()==3&&f.charge==7,"shop purchase scales destination while original charge stays 7");
 f.Reset(0,false);Put(f.groups.data(),0x10,uint32_t{2});Put(f.items[1].data(),8,uint16_t{44});Put(f.items[1].data(),0x10,int64_t{2});
 check(run()==0&&f.output_count==2&&qty()==3&&qty(1)==6,"ordinary field loot scales each positive stackable item once");
 f.Reset(0,false);Put(f.groups.data(),0x10,uint32_t{2});Put(f.items[1].data(),8,uint16_t{43});Put(f.items[1].data(),0x10,int64_t{1});
 check(run()==0&&qty()==3&&qty(1)==1,"mixed loot leaves singleton unchanged");
 f.Reset();f.current=99;check(run()==0&&f.output_count==2&&qty()==1&&qty(1)==2,"native planner splits scaled receipt at existing-stack capacity");
 f.Reset();f.current=99;f.available_slots=1;check(run()==19&&f.items[0]==original[0],"native full-slot failure preserves original input and reports native error");
 f.Reset();Put(f.groups.data(),0,uint16_t{1});check(run()==0&&qty()==1,"same actor canonical same-container move passes through");
 f.Reset();Put(f.behavior.data(),0x10,uintptr_t{0x30000000});check(run()==0&&qty()==1,"unrelated NPC destination passes through");
 // d180 RED: genuine player-origin external destinations, exact native move caller.
 f.Reset(0x38);Put(f.behavior.data(),0x10,uintptr_t{0x30000000});f.expected_owner_actor=0x30000000;
 check(run()==0&&qty()==3&&f.owner_seen==0x30000000,"d180 deposit INTO external storage uses actual destination owner");
 f.Reset(0x38);Put(f.behavior.data(),0x10,uintptr_t{0x30000000});f.expected_owner_actor=0x30000000;Put(f.groups.data(),0,uint16_t{1});
 check(run()==0&&qty()==3,"d180 same canonical index across distinct actors is a real transfer");
 f.Reset(0x38);Put(f.behavior.data(),0x10,uintptr_t{0x30000000});f.expected_owner_actor=0x30000000;f.current=99;
 check(run()==0&&f.output_count==2&&qty()==1&&qty(1)==2,"d180 external existing stack splits native gain once");
 f.Reset(0x38);Put(f.behavior.data(),0x10,uintptr_t{0x30000000});f.expected_owner_actor=0x30000000;const auto externalSource=f.removal;const auto externalItems=f.items;
 check(run()==0&&qty()==3&&f.removal==externalSource&&f.items==externalItems&&f.source==50&&f.charge==7,"d180 deposit changes only private destination quantity; source and charges exact");
 f.Reset(0x38);Put(f.behavior.data(),0x10,uintptr_t{0x30000000});f.expected_owner_actor=0x30000000;Put(f.behavior.data(),0x90,uintptr_t{0x40000000});
 check(run()==0&&qty()==1,"d180 NPC to NPC transfer remains native");
 f.Reset(0x38);Put(f.behavior.data(),0x10,uintptr_t{0x30000000});f.expected_owner_actor=0x30000000;
 check(run(f.player,0x777777)==0&&qty()==1,"d180 external arbitrary caller remains native");
 f.Reset(7,false);Put(f.behavior.data(),0x10,uintptr_t{0x30000000});f.expected_owner_actor=0x30000000;
 check(run()==0&&qty()==1,"d180 merchant sell or nonmove destination remains native");
 f.Reset(0x38);Put(f.behavior.data(),0x10,uintptr_t{0x30000000});f.expected_owner_actor=0x30000000;f.owner_available=false;
 check(run()==0&&qty()==1,"d180 unknown external destination owner refuses scaling");
 f.Reset(0x38);Put(f.behavior.data(),0x10,uintptr_t{0x30000000});f.expected_owner_actor=f.player;
 check(run()==0&&qty()==1,"d180 player headroom cannot substitute for external owner");
 f.Reset(0x38);Put(f.behavior.data(),0x10,uintptr_t{0x30000000});f.expected_owner_actor=0x30000000;Put(f.removal.data(),0x18,int64_t{2});
 check(run()==0&&qty()==1,"d180 external source destination quantity mismatch remains native");
 f.Reset(0x38);Put(f.behavior.data(),0x10,uintptr_t{0x30000000});f.expected_owner_actor=0x30000000;f.owner_alias=f.container_alias=true;Put(f.groups.data(),0,uint16_t{1});
 check(run()==0&&qty()==1,"d180 different actors proxying same owner and container cannot create gain");
 f.Reset(0x38);Put(f.behavior.data(),0x10,uintptr_t{0x30000000});f.expected_owner_actor=0x30000000;f.owner_alias=true;
 check(run()==0&&qty()==3,"d180 same effective owner with genuinely different containers is a transfer");
 f.Reset(0x38);Put(f.behavior.data(),0x10,uintptr_t{0x30000000});f.expected_owner_actor=0x30000000;f.container_alias=true;Put(f.groups.data(),0,uint16_t{1});
 check(run()==0&&qty()==1,"d180 distinct owner wrappers sharing actual container cannot create gain");
 f.Reset(0x38);Put(f.behavior.data(),0x90,uintptr_t{0x30000000});
 check(run()==0&&qty()==3&&f.owner_seen==f.player,"d180 withdrawal TO player preserves baseline source external semantics");
 f.Reset();check(run(0)==0&&qty()==1,"missing tracked player fails closed");
 f.Reset();Put(f.items[0].data(),8,uint16_t{43});check(run()==0&&qty()==1,"nonstackable transfer unchanged");
 f.Reset();Put(f.items[0].data(),0x10,int64_t{-1});check(run()==17&&f.observed==-1,"negative removal reaches native unchanged");
 f.Reset();Put(f.items[0].data(),0x10,std::numeric_limits<int64_t>::max()/2+1);check(run()==17&&f.observed==std::numeric_limits<int64_t>::max()/2+1,"overflow does not saturate or write arbitrary huge scaled count");
 f.Reset();Put(f.behavior.data(),0x50,uint32_t{1000});check(run()==98&&Read<uintptr_t>(f.push(),0x48)==reinterpret_cast<uintptr_t>(f.groups.data()),"malformed group bound passes through with no pointer substitution");
 f.Reset();f.nested=true;check(run()==0&&qty()==3,"same-push reentry never multiplies by 9");
 f.Reset();f.fail=true;check(run()==17&&Read<uintptr_t>(f.push(),0x48)==reinterpret_cast<uintptr_t>(f.groups.data()),"native failure restores borrowed pointer");
 f.Reset();Put(f.removal.data(),0x18,int64_t{2});check(run()==0&&qty()==1,"source request quantity mismatch passes through unscaled");
 f.Reset();Put(f.behavior.data(),0x90,uintptr_t{0});check(run()==0&&qty()==1,"invalid source actor fails closed");
 f.Reset();f.raise=true;check(CatchNativeRaise(&f)&&Read<uintptr_t>(f.push(),0x48)==reinterpret_cast<uintptr_t>(f.groups.data()),"native exception propagates after pointer and TLS restoration");
 f.Reset();check(run()==0&&qty()==3,"next transaction still scales after exception");
 Fixture child;child.Reset();f.Reset();f.child=&child;check(run()==0&&qty()==3&&Read<int64_t>(reinterpret_cast<uintptr_t>(child.outputs[0].data()),0x10)==2,"nested different transaction owns its copy and frozen multiplier");
 config.items.gain_multiplier=3;SetConfigSnapshot(L"",config);
 f.Reset(0,false);f.stack=std::numeric_limits<int64_t>::max()/32767;const int64_t precise=(int64_t{1}<<53)+1;Put(f.items[0].data(),0x10,precise);
 check(run()==17&&f.observed==int64_t{27021597764222979},"integer gain above 2^53 preserves exact signed quantity");
 // R1 red: each product fits, but native aggregate/WORD domains do not.
 f.Reset(0,false);f.stack=std::numeric_limits<int64_t>::max()/2;f.current=2;
 const int64_t large=std::numeric_limits<int64_t>::max()/3;Put(f.items[0].data(),0x10,large);
 check(run()==17&&f.observed==large,"R1 existing2 plus scaled INT64_MAX-1 declines before unguarded native ADD");
 f.Reset(0,false);f.stack=std::numeric_limits<int64_t>::max()/2;f.current=2;Put(f.groups.data(),0x10,uint32_t{2});
 const int64_t half=std::numeric_limits<int64_t>::max()/6;Put(f.items[0].data(),0x10,half);Put(f.items[1].data(),8,uint16_t{45});Put(f.items[1].data(),0x10,half);
 check(run()==17&&f.observed==half,"R1 alias keys aggregate by native item ID before existing plus incoming sum");
 f.Reset(0,false);f.stack=std::numeric_limits<int64_t>::max()/2;Put(f.behavior.data(),0x50,uint32_t{2});
 Put(f.groups.data(),0x18,uint16_t{15});Put(f.groups.data(),0x20,f.items[1].data());Put(f.groups.data(),0x28,uint32_t{1});Put(f.groups.data(),0x2C,uint32_t{1});
 Put(f.items[0].data(),0x10,large);Put(f.items[1].data(),8,uint16_t{45});Put(f.items[1].data(),0x10,large);
 check(run()==17&&f.observed==large,"R1 duplicate container groups cannot bypass combined incoming quantity guard");
 f.Reset(0,false);f.stack=2;Put(f.groups.data(),0x10,uint32_t{2});Put(f.items[0].data(),0x10,int64_t{11000});Put(f.items[1].data(),8,uint16_t{44});Put(f.items[1].data(),0x10,int64_t{11000});
 check(run()==17&&f.observed==11000,"R1 summed native slot deltas cannot narrow 33000 to signed WORD");
 f.Reset(0,false);f.stack=2;Put(f.items[0].data(),0x10,int64_t{21846});check(run()==17&&f.observed==21846,"R1 per-item quotient cannot exceed signed WORD domain");
 f.Reset();f.current=2;f.count_override=65537;check(run()==0&&qty()==1,"R1 full DWORD container count rejects low-word alias");
 f.Reset();f.current=2;f.capacity_override=0;check(run()==0&&qty()==1,"R1 count beyond native vector capacity passes through");
 f.Reset();f.owner_available=false;check(run()==0&&qty()==1,"R1 unknown native owner fails closed");
 f.Reset();f.current=2;void* inaccessible=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_NOACCESS);
 f.entries_override=reinterpret_cast<uintptr_t>(inaccessible);check(inaccessible&&run()==0&&qty()==1,"R1 unreadable existing inventory declines without mutation");if(inaccessible)VirtualFree(inaccessible,0,MEM_RELEASE);
 f.Reset();f.current=2;void* housing=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,size_t{1000}*0xC8);
 if(housing){for(size_t i=0;i<1000;++i)Put(housing,i*0xC8+8,uint16_t{0xFFFF});Put(housing,8,uint16_t{42});Put(housing,0x10,int64_t{2});}
 f.entries_override=reinterpret_cast<uintptr_t>(housing);f.count_override=1000;f.capacity_override=1000;
 check(housing&&run()==0&&qty()==3,"R1 ordinary 1000-slot housing vector still delivers multiplier3");if(housing)HeapFree(GetProcessHeap(),0,housing);
 f.Reset(0,false);f.singleton_stack=0;Put(f.groups.data(),0x10,uint32_t{2});Put(f.items[1].data(),8,uint16_t{43});Put(f.items[1].data(),0x10,int64_t{1});
 check(run()==0&&qty()==1&&qty(1)==1,"R1 mixed zero-capacity metadata declines entire substitution");
 f.Reset(0,false);f.singleton_stack=-7;Put(f.groups.data(),0x10,uint32_t{2});Put(f.items[1].data(),8,uint16_t{43});Put(f.items[1].data(),0x10,int64_t{1});
 check(run()==0&&qty()==1&&qty(1)==1,"R1 mixed negative-capacity metadata declines entire substitution");
 // R2 red: require exact floor for fractional settings above double integer precision.
 f.Reset(0,false);f.stack=std::numeric_limits<int64_t>::max()/32767;config.items.gain_multiplier=1.5;SetConfigSnapshot(L"",config);Put(f.items[0].data(),0x10,int64_t{9007199254740993});
 check(run()==17&&f.observed==int64_t{13510798882111489},"R2 odd quantity above 2^53 times 1.5 has exact decimal floor");
 f.Reset(0,false);f.stack=std::numeric_limits<int64_t>::max()/32767;config.items.gain_multiplier=0.5;SetConfigSnapshot(L"",config);Put(f.items[0].data(),0x10,int64_t{9007199254740995});
 check(run()==17&&f.observed==int64_t{4503599627370497},"R2 odd quantity above 2^53 times 0.5 does not round upward");
 f.Reset(0,false);f.stack=std::numeric_limits<int64_t>::max()/2;Put(f.items[0].data(),0x10,std::numeric_limits<int64_t>::max());
 check(run()==17&&f.observed==int64_t{4611686018427387903},"R2 INT64_MAX times 0.5 retains exact floor");
 f.Reset(0,false);config.items.gain_multiplier=1.2;SetConfigSnapshot(L"",config);Put(f.items[0].data(),0x10,int64_t{5});
 check(run()==0&&qty()==6,"R2 ordinary decimal 1.2 times 5 stays 6");
 f.Reset(0,false);config.items.gain_multiplier=2.5;SetConfigSnapshot(L"",config);Put(f.items[0].data(),0x10,int64_t{7});
 check(run()==0&&qty()==17,"R2 ordinary decimal 2.5 times 7 floors to 17");
 f.Reset(0,false);config.items.gain_multiplier=0.1;SetConfigSnapshot(L"",config);Put(f.items[0].data(),0x10,int64_t{30});
 check(run()==0&&qty()==3,"R2 ordinary decimal 0.1 times 30 stays 3");
 config.items.gain_multiplier=3;SetConfigSnapshot(L"",config);
 const auto fallback=[&](uintptr_t caller=0x234567,uintptr_t source=0x20000000,uint16_t container=15){
  Put(request_vector.data(),0,f.removal.data());Put(request_vector.data(),8,uint32_t{1});uint32_t error=99;
  const auto result=RunMoveParentTest(f.parent(),&error,source,0x30000000,container,reinterpret_cast<uintptr_t>(request_vector.data()),0xA5,0xABCDEF,f.base(),f.player,reinterpret_cast<uintptr_t>(ParentNative),caller);
  check(result==reinterpret_cast<uintptr_t>(&error),"d180 fallback preserves native error-pointer return");
  return error;
 };
 f.Fallback();check(fallback()==0&&qty()==3,"d180 exact zero-receipt fallback deposit scales once");
 check(parent_args[0]==f.parent()&&parent_args[2]==f.player&&parent_args[3]==0x30000000&&parent_args[4]==15&&parent_args[5]==reinterpret_cast<uintptr_t>(request_vector.data())&&parent_args[6]==0xA5&&parent_args[7]==0xABCDEF,"d180 transparent parent preserves all eight native arguments");
 check(GetLastError()==0x12345678,"d180 TLS restoration preserves native LastError");
 f.Fallback();check(fallback(0x777777)==0&&qty()==1,"d180 generic factory fallback caller has no external permission");
 f.Fallback();check(fallback(0x234567,0x40000000)==0&&qty()==1,"d180 fallback NPC source remains native");
 f.Fallback();parent_plan_caller=0x777777;check(fallback()==0&&qty()==1,"d180 fallback token cannot authorize wrong Plan caller");parent_plan_caller=0x456789;
 f.Fallback();check(fallback(0x234567,0x20000000,16)==0&&qty()==1,"d180 fallback destination container mismatch remains native");
 f.Fallback();Put(f.removal.data(),0x18,int64_t{2});check(fallback()==0&&qty()==1,"d180 fallback original source quantity mismatch remains native");
 f.Fallback();parent_torn=true;check(fallback()==0&&qty()==1,"d180 fallback changed request vector refuses scaling");parent_torn=false;
 f.Fallback();parent_nested_wrong=true;check(fallback()==0&&qty()==1,"d180 unqualified nested parent shadows outer token");
 f.Fallback();parent_raise=true;check(CatchParentRaise(&f),"d180 fallback exception propagates");parent_raise=false;
 f.Fallback();check(run(f.player,0x456789)==0&&qty()==1,"d180 fallback token cleared after native exception");
 f.Fallback();check(fallback()==0&&qty()==3,"d180 valid fallback works after exception without stale token");
 f.Fallback();check(fallback()==0&&f.removal==externalSource&&f.source==50&&f.charge==7,"d180 fallback source request stock and charge remain native");
 f.Fallback();check(InstallMoveParentHookTest(reinterpret_cast<uintptr_t>(ParentPassThrough)),"d180 actual SafetyHook eight-argument parent installs on authored private code");
 uint32_t parent_error=0;SetLastError(0x87654321);const auto parent_result=ParentPassThrough(f.parent(),&parent_error,f.player,0x30000000,15,reinterpret_cast<uintptr_t>(request_vector.data()),0xA5,0xABCDEF);
 check(parent_result==reinterpret_cast<uintptr_t>(&parent_error)&&parent_error==0xBADC0DE&&parent_args==std::array<uintptr_t,8>{f.parent(),reinterpret_cast<uintptr_t>(&parent_error),f.player,0x30000000,15,reinterpret_cast<uintptr_t>(request_vector.data()),0xA5,0xABCDEF},"d180 real inline callback preserves eight args and native return/error");
 check(GetLastError()==0x12345678&&parent_incoming_error==0x87654321,"d180 real inline callback preserves incoming and outgoing native LastError");
 DisableMoveParentHookTest();parent_error=0;ParentPassThrough(f.parent(),&parent_error,f.player,0x30000000,15,reinterpret_cast<uintptr_t>(request_vector.data()),0xA5,0xABCDEF);check(parent_error==0xBADC0DE,"d180 disabled parent resumes original native mock");RemoveMoveParentHookTest();
 // Actual authored native entries exercise the production timing callback ABI.
 namespace it=psm::item_timing;it::Sample packets[128]{};
 f.Reset();uint32_t timing_error=99;TimingPlanCaller(f.push(),&timing_error);TimingPushCaller(f.push(),&timing_error);
 const auto plan_site=timing_plan_return,push_site=timing_push_return,receipt_site=timing_receipt_return;
 check(InstallItemTimingTestHooks(f.base(),f.player,reinterpret_cast<uintptr_t>(TimingPlanNative),reinterpret_cast<uintptr_t>(TimingPushNative),
  reinterpret_cast<uintptr_t>(TimingReceiptNative),plan_site,push_site,receipt_site),"timing real Plan/Push/Receipt hooks install on authored code");
 it::Enable(true);f.Reset();SetLastError(0x12345678);timing_error=99;auto timed_result=TimingPlanCaller(f.push(),&timing_error);
 check(timed_result==reinterpret_cast<uintptr_t>(&timing_error)&&timing_error==0&&f.observed==3&&timing_plan_in==0x12345678&&GetLastError()==0xBEEF1234,
  "timing Plan callback preserves two arguments, native return/quantity and incoming/outgoing LastError");
 auto n=it::Drain(packets,128);check(n==1&&packets[0].kind==it::Kind::Plan&&packets[0].prepared&&packets[0].completed&&!packets[0].parent,
  "timing actual accepted transfer emits independent Plan phases");
 Put(f.behavior.data(),0x98,f.items.data());Put(f.behavior.data(),0xA0,uint32_t{1});
 SetLastError(0x23456789);timed_result=TimingPushCaller(f.push(),&timing_error);
 check(timed_result==reinterpret_cast<uintptr_t>(&timing_error)&&timing_error==17&&timing_push_in==0x23456789&&timing_receipt_in==0x23456789&&GetLastError()==0xDEAD1234,
  "timing Push and Receipt preserve native incoming/outgoing LastError and return");
 check(timing_receipt_args==std::array<uintptr_t,5>{reinterpret_cast<uintptr_t>(f.container.data()),reinterpret_cast<uintptr_t>(&timing_error),reinterpret_cast<uintptr_t>(f.outputs[0].data()),93,0xA5},
  "timing real receipt keeps all five ABI args including stack flag");
 n=it::Drain(packets,128);uint64_t push_id=0,receipt_parent=0;
 for(size_t i=0;i<n;++i){if(packets[i].kind==it::Kind::Push){push_id=packets[i].id;check(!packets[i].parent,"timing Push never guesses a returned Plan parent");}if(packets[i].kind==it::Kind::Receipt)receipt_parent=packets[i].parent;}
 check(n==2&&push_id&&receipt_parent==push_id,"timing Receipt binds only still-live Push TLS generation");
 it::Enable(true);f.Reset();timing_raise=1;check(CatchTimingRaise(&f,1)&&GetLastError()==0xA001,"timing native Plan exception propagates with LastError");
 n=it::Drain(packets,128);check(n==1&&packets[0].kind==it::Kind::Plan&&packets[0].abnormal&&!packets[0].completed,
  "timing Plan finally captures native exception after pointer/TLS cleanup");
 timing_raise=0;f.Reset();TimingPlanCaller(f.push(),&timing_error);check(f.observed==3,"timing next transfer works after Plan exception");
 it::Drain(packets,128);Put(f.behavior.data(),0x98,f.items.data());Put(f.behavior.data(),0xA0,uint32_t{1});timing_raise=3;
 check(CatchTimingRaise(&f,3)&&GetLastError()==0xA003,"timing nested Receipt/Push native exception propagates");
 n=it::Drain(packets,128);check(n==2&&packets[0].kind==it::Kind::Receipt&&packets[0].abnormal&&packets[1].kind==it::Kind::Push&&packets[1].abnormal,
  "timing both receipt and push finally emit abnormal packets without swallowing exception");
 timing_raise=0;it::Enable(true);for(unsigned i=0;i<200;++i){f.Reset(7,false);TimingPlanCaller(f.push(),&timing_error);}
 check(!it::Drain(packets,128),"timing cold loot/shop plans never spend transfer budgets");
 for(unsigned i=0;i<200;++i)TimingReceiptNative(reinterpret_cast<uintptr_t>(f.container.data()),&timing_error,f.items[0].data(),1,0x5A);
 check(!it::Drain(packets,128),"timing unrelated receipt caller never spends receipt budget");
 f.Reset();TimingPlanCaller(f.push(),&timing_error);check(it::Drain(packets,128)==1,"timing first meaningful transfer still captured after background traffic");
 f.Fallback();Put(request_vector.data(),0,f.removal.data());Put(request_vector.data(),8,uint32_t{1});
 RunMoveParentTest(f.parent(),&timing_error,f.player,0x30000000,15,reinterpret_cast<uintptr_t>(request_vector.data()),0xA5,0xABCDEF,f.base(),f.player,reinterpret_cast<uintptr_t>(ParentNative),0x234567);
 n=it::Drain(packets,128);check(n==1&&packets[0].kind==it::Kind::Plan&&packets[0].prepared&&f.observed==3,"timing exact fallback transfer keeps scoped gain and captures Plan");
 it::Enable(true);f.Reset();TimingPlanCaller(f.push(),&timing_error);it::Drain(packets,128);
 Put(f.behavior.data(),0x98,f.items.data());Put(f.behavior.data(),0xA0,uint32_t{1});timing_reset_in_push=true;TimingPushCaller(f.push(),&timing_error);
 check(!it::Drain(packets,128),"timing reset during native Push cannot link new-epoch Receipt to old live TLS ID");
 RemoveItemTimingTestHooks();it::Enable(false);
 f.Reset();config.general.enabled=false;SetConfigSnapshot(L"",config);check(run()==0&&qty()==1,"disabled config passes through");
 config.general.enabled=true;SetConfigSnapshot(L"",config);active=nullptr;return failures;
}
