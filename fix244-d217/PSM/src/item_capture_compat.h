#pragma once
#include "item_capture_compat_profiles.h"
#include "item_capture_menu206_profiles.h"
#include "item_capture_menu509_profiles.h"
#include "item_capture_menu510_profiles.h"
#include "item_capture_menu512_profiles.h"
#include "item_capture_menu512b_profiles.h"
#include "item_capture_menu514_profiles.h"
#include "item_capture_menu516_profiles.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
namespace psm::capture_compat {
inline bool SafeCopy(uintptr_t at,void* out,std::size_t size) noexcept {
 if(!at||!out||size>4096)return false;
 __try {std::memcpy(out,reinterpret_cast<const void*>(at),size);return true;}
 __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
inline bool PrivateExecutable(uintptr_t at,std::size_t size) noexcept {
 if(!at||size>(std::numeric_limits<uintptr_t>::max)()-at)return false;
 MEMORY_BASIC_INFORMATION m{};
 if(!VirtualQuery(reinterpret_cast<const void*>(at),&m,sizeof(m))||m.State!=MEM_COMMIT||m.Type!=MEM_PRIVATE||
    (m.Protect&(PAGE_GUARD|PAGE_NOACCESS))||!(m.Protect&(PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return false;
 const auto begin=reinterpret_cast<uintptr_t>(m.BaseAddress);
 return at>=begin&&at-begin<=m.RegionSize&&size<=m.RegionSize-(at-begin);
}
struct Reader {
 void* opaque=nullptr;
 bool (*copy)(void*,uintptr_t,void*,std::size_t)=nullptr;
 bool (*executable)(void*,uintptr_t,std::size_t)=nullptr;
 bool Read(uintptr_t p,void* out,std::size_t n)const{return copy?copy(opaque,p,out,n):SafeCopy(p,out,n);}
 bool Exec(uintptr_t p,std::size_t n)const{return executable?executable(opaque,p,n):PrivateExecutable(p,n);}
};
struct Capture {uintptr_t handler=0,original_slot=0;unsigned span=0;uintptr_t record=0;};
struct Context {
 const void* module=nullptr;Capture owner{},capacity{};bool valid=false;
 uintptr_t capacity_call=0; // Exact final Menu's validated typed capacity wrapper.
 static std::size_t Rip(const v3446::Image& image,std::size_t at,const std::array<unsigned char,3>& prefix){
  std::int32_t delta=0;
  if(!image.Exec(at,7)||std::memcmp(image.data+at,prefix.data(),3)||!image.Read(at+3,delta))return 0;
  const auto target=std::int64_t(at)+7+delta;
  return target>0&&image.Range(std::size_t(target),8)?std::size_t(target):0;
 }
 static bool DataSection(const v3446::Image& image,std::size_t at,std::size_t n,bool writable){
  if(!image.Range(at,n)||image.Exec(at,n))return false;
  for(unsigned i=0;i<image.nt.FileHeader.NumberOfSections;++i){const auto&sec=image.sections[i];const auto extent=(std::max)(sec.Misc.VirtualSize,sec.SizeOfRawData);
   if(at>=sec.VirtualAddress&&at-sec.VirtualAddress<=extent&&n<=extent-(at-sec.VirtualAddress))
    return (sec.Characteristics&IMAGE_SCN_MEM_READ)&&bool(sec.Characteristics&IMAGE_SCN_MEM_WRITE)==writable;
  }return false;
 }
 static std::size_t DataExport(const v3446::Image& image,const char* name){
  const auto d=image.nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];IMAGE_EXPORT_DIRECTORY e{};
  if(!d.Size||!image.Read(d.VirtualAddress,e)||e.NumberOfNames>4096||e.NumberOfFunctions>4096)return 0;
  std::size_t result=0;
  for(DWORD i=0;i<e.NumberOfNames;++i){DWORD str=0;WORD ordinal=0;
   if(!image.Read(e.AddressOfNames+i*4,str)||!image.Read(e.AddressOfNameOrdinals+i*2,ordinal)||ordinal>=e.NumberOfFunctions)return 0;
   const auto n=std::strlen(name)+1;if(!image.Range(str,n)||std::memcmp(image.data+str,name,n))continue;
   DWORD address=0;if(result||!image.Read(e.AddressOfFunctions+ordinal*4,address)||address>=d.VirtualAddress&&address-d.VirtualAddress<d.Size)return 0;
   result=address;
  }return result;
 }
 static Context FromMenu206(const v3446::Image& image,const void* module){
  Context c{};using namespace menu206;
  const std::array<const v3446::Profile*,18> profiles{&kFactory,&kOwner,&kCapacity,&kDispatch,&kInvoke,&kThunk,&kValidateApi,&kPin,&kCreateHook,&kBuiltin,&kCtor,&kCtorCall,&kCapacityCall,&kOwnerCall,&kQuery,&kDescribe,&kSubscribe,&kUnsubscribe};
  std::array<std::size_t,18> found{};for(unsigned i=0;i<profiles.size();++i)if(!(found[i]=image.Find(*profiles[i])))return c;
  const auto factory=found[0],owner=found[1],capacity=found[2];
  const auto descriptor=Rip(image,factory+(0x5E009-0x5DD90),{0x48,0x8D,0x0D});
  const auto state=Rip(image,factory+(0x5E040-0x5DD90),{0x4C,0x8D,0x0D});
  if(!descriptor||descriptor!=DataExport(image,"DesertLinkInventoryCaptureApiV1")||
     !DataSection(image,descriptor,136,false)||!DataSection(image,state,120,true)||
     Rip(image,owner+0x56,{0x48,0x8B,0x05})!=state+2*40+8||
     Rip(image,capacity+0xC6,{0x48,0x8B,0x05})!=state+40+8||
     Rip(image,factory+(0x5E071-0x5DD90),{0x4C,0x8D,0x25})!=owner||
     Rip(image,factory+(0x5E078-0x5DD90),{0x48,0x8D,0x05})!=capacity)return c;
  const auto base=reinterpret_cast<uintptr_t>(module);
  std::array<unsigned char,136> expected{};const uint64_t magic=0x31435041434C4444ULL,abi=0x31395F32315F4944ULL;
  const uint32_t version=1,size=136;std::memcpy(expected.data(),&magic,8);std::memcpy(expected.data()+8,&version,4);
  std::memcpy(expected.data()+12,&size,4);std::memcpy(expected.data()+24,&abi,8);
  std::memcpy(expected.data()+32,&base,8);const auto statePointer=base+state;std::memcpy(expected.data()+40,&statePointer,8);
  const std::array<unsigned,11> fnIndex{9,10,2,1,11,12,13,14,15,16,17};
  for(unsigned i=0;i<fnIndex.size();++i){const auto address=base+found[fnIndex[i]];std::memcpy(expected.data()+48+i*8,&address,8);}
  if(std::memcmp(image.data+descriptor,expected.data(),expected.size()))return c;
  c.module=module;c.owner={base+owner,base+state+2*40+8,6,base+state+2*40};
  c.capacity={base+capacity,base+state+40+8,5,base+state+40};c.valid=true;return c;
 }
 static Context FromMenu509(const v3446::Image& image,const void* module){
  Context c{};using namespace menu509;
  const std::array<const v3446::Profile*,24> profiles{&kFactory,&kOwner,&kCapacity,&kDispatch,&kInvoke,&kThunk,&kValidateApi,&kPin,&kCreateHook,&kBuiltin,&kCtor,&kCtorCall,&kCapacityCall,&kOwnerCall,&kQuery,&kDescribe,&kSubscribe,&kUnsubscribe,&kCreateHookCore,&kTrampoline,&kExecutable,&kAcquire,&kRead,&kDecode};
  std::array<std::size_t,24> found{};for(unsigned i=0;i<profiles.size();++i)if(!(found[i]=image.Find(*profiles[i])))return c;
  const auto factory=found[0],owner=found[1],capacity=found[2];
  const auto descriptor=Rip(image,factory+(0x5E009-0x5DD90),{0x48,0x8D,0x0D});
  const auto state=Rip(image,factory+(0x5E040-0x5DD90),{0x4C,0x8D,0x0D});
  if(!descriptor||descriptor!=DataExport(image,"DesertLinkInventoryCaptureApiV1")||
     !DataSection(image,descriptor,136,false)||!DataSection(image,state,120,true)||
     Rip(image,owner+0x56,{0x48,0x8B,0x05})!=state+2*40+8||
     Rip(image,capacity+0xC6,{0x48,0x8B,0x05})!=state+40+8||
     Rip(image,factory+(0x5E071-0x5DD90),{0x4C,0x8D,0x25})!=owner||
     Rip(image,factory+(0x5E078-0x5DD90),{0x48,0x8D,0x05})!=capacity)return c;
  // The v5.0.9 factory delegates to a bounded-retry wrapper, not the old
  // direct MH_CreateHook. Require its complete core/trampoline/decode owners
  // and all relevant direct call edges. No target-normalization assumption.
  if(image.Branch(factory+0x306)!=found[8] ||
     image.Branch(found[8]+0x1A3)!=found[18] ||
     image.Branch(found[18]+0x156)!=found[19] ||
     image.Branch(found[19]+0xA6)!=found[23] ||
     image.Branch(found[18]+0x3F)!=found[21] ||
     image.Branch(found[18]+0x54)!=found[20] ||
     image.Branch(found[18]+0x64)!=found[20] ||
     image.Branch(found[18]+0x120)!=found[22] ||
     image.Branch(found[8]+0x111)!=found[22])return c;
  // These two dispatch operands are image-relative displacements, not RIP
  // operands. Bind them to this exact state and the three subscriber lanes.
  uint32_t dispatchState=0,dispatchSubscribers=0;
  if(!image.Read(found[3]+0x8B,dispatchState)||dispatchState!=state+32||
     !image.Read(found[3]+0xA5,dispatchSubscribers)||dispatchSubscribers!=state+128||
     !DataSection(image,dispatchSubscribers,3*0xB90,true))return c;
  const auto base=reinterpret_cast<uintptr_t>(module);
  std::array<unsigned char,136> expected{};const uint64_t magic=0x31435041434C4444ULL,abi=0x31395F32315F4944ULL;
  const uint32_t version=1,size=136;std::memcpy(expected.data(),&magic,8);std::memcpy(expected.data()+8,&version,4);
  std::memcpy(expected.data()+12,&size,4);std::memcpy(expected.data()+24,&abi,8);
  std::memcpy(expected.data()+32,&base,8);const auto statePointer=base+state;std::memcpy(expected.data()+40,&statePointer,8);
  const std::array<unsigned,11> fnIndex{9,10,2,1,11,12,13,14,15,16,17};
  for(unsigned i=0;i<fnIndex.size();++i){const auto address=base+found[fnIndex[i]];std::memcpy(expected.data()+48+i*8,&address,8);}
  if(std::memcmp(image.data+descriptor,expected.data(),expected.size()))return c;
  c.module=module;c.owner={base+owner,base+state+2*40+8,6,base+state+2*40};
  c.capacity={base+capacity,base+state+40+8,5,base+state+40};c.valid=true;return c;
 }
 // EXPERIMENTAL author TEST provider: exact input and contract in menu510 profiles.
 static Context FromMenu510(const v3446::Image& image,const void* module){
  Context c{};using namespace menu510;
  const std::array<const v3446::Profile*,24> profiles{&kFactory,&kOwner,&kCapacity,&kDispatch,&kInvoke,&kThunk,&kValidateApi,&kPin,&kCreateHook,&kBuiltin,&kCtor,&kCtorCall,&kCapacityCall,&kOwnerCall,&kQuery,&kDescribe,&kSubscribe,&kUnsubscribe,&kCreateHookCore,&kTrampoline,&kExecutable,&kAcquire,&kRead,&kDecode};
  std::array<std::size_t,24> found{};for(unsigned i=0;i<profiles.size();++i)if(!(found[i]=image.Find(*profiles[i])))return c;
  const auto factory=found[0],owner=found[1],capacity=found[2];
  const auto descriptor=Rip(image,factory+(0x5E009-0x5DD90),{0x48,0x8D,0x0D});
  const auto state=Rip(image,factory+(0x5E040-0x5DD90),{0x4C,0x8D,0x0D});
  if(!descriptor||descriptor!=DataExport(image,"DesertLinkInventoryCaptureApiV1")||
     !DataSection(image,descriptor,136,false)||!DataSection(image,state,120,true)||
     Rip(image,owner+0x56,{0x48,0x8B,0x05})!=state+2*40+8||
     Rip(image,capacity+0xC6,{0x48,0x8B,0x05})!=state+40+8||
     Rip(image,factory+(0x5E071-0x5DD90),{0x4C,0x8D,0x25})!=owner||
     Rip(image,factory+(0x5E078-0x5DD90),{0x48,0x8D,0x05})!=capacity)return c;
  // The v5.1.0 factory delegates to a bounded-retry wrapper, not the old
  // direct MH_CreateHook. Require its complete core/trampoline/decode owners
  // and all relevant direct call edges. No target-normalization assumption.
  if(image.Branch(factory+0x306)!=found[8] ||
     image.Branch(found[8]+0x1A3)!=found[18] ||
     image.Branch(found[18]+0x156)!=found[19] ||
     image.Branch(found[19]+0xA6)!=found[23] ||
     image.Branch(found[18]+0x3F)!=found[21] ||
     image.Branch(found[18]+0x54)!=found[20] ||
     image.Branch(found[18]+0x64)!=found[20] ||
     image.Branch(found[18]+0x120)!=found[22] ||
     image.Branch(found[8]+0x111)!=found[22])return c;
  // These two dispatch operands are image-relative displacements, not RIP
  // operands. Bind them to this exact state and the three subscriber lanes.
  // v5.1.0 places subscribers at state+176, independently of the120-byte state.
  uint32_t dispatchState=0,dispatchSubscribers=0;
  if(!image.Read(found[3]+0x8B,dispatchState)||dispatchState!=state+32||
     !image.Read(found[3]+0xA5,dispatchSubscribers)||dispatchSubscribers!=state+176||
     !DataSection(image,dispatchSubscribers,3*0xB90,true))return c;
  if(image.Branch(found[4]+4)!=found[5] ||
     image.Branch(owner+0x70)!=found[3] ||
     image.Branch(capacity+0xB9)!=found[3])return c;
  const auto base=reinterpret_cast<uintptr_t>(module);
  std::array<unsigned char,136> expected{};const uint64_t magic=0x31435041434C4444ULL,abi=0x31395F32315F4944ULL;
  const uint32_t version=1,size=136;std::memcpy(expected.data(),&magic,8);std::memcpy(expected.data()+8,&version,4);
  std::memcpy(expected.data()+12,&size,4);std::memcpy(expected.data()+24,&abi,8);
  std::memcpy(expected.data()+32,&base,8);const auto statePointer=base+state;std::memcpy(expected.data()+40,&statePointer,8);
  const std::array<unsigned,11> fnIndex{9,10,2,1,11,12,13,14,15,16,17};
  for(unsigned i=0;i<fnIndex.size();++i){const auto address=base+found[fnIndex[i]];std::memcpy(expected.data()+48+i*8,&address,8);}
  if(std::memcmp(image.data+descriptor,expected.data(),expected.size()))return c;
  c.module=module;c.owner={base+owner,base+state+2*40+8,6,base+state+2*40};
  c.capacity={base+capacity,base+state+40+8,5,base+state+40};c.valid=true;return c;
 }
 // Exact observed v5.1.2 ACFE provider; complete contract in menu512 profiles.
 static Context FromMenu512(const v3446::Image& image,const void* module){
  Context c{};using namespace menu512;
  const std::array<const v3446::Profile*,24> profiles{&kFactory,&kOwner,&kCapacity,&kDispatch,&kInvoke,&kThunk,&kValidateApi,&kPin,&kCreateHook,&kBuiltin,&kCtor,&kCtorCall,&kCapacityCall,&kOwnerCall,&kQuery,&kDescribe,&kSubscribe,&kUnsubscribe,&kCreateHookCore,&kTrampoline,&kExecutable,&kAcquire,&kRead,&kDecode};
  std::array<std::size_t,24> found{};for(unsigned i=0;i<profiles.size();++i)if(!(found[i]=image.Find(*profiles[i])))return c;
  const auto factory=found[0],owner=found[1],capacity=found[2];
  const auto descriptor=Rip(image,factory+(0x5E009-0x5DD90),{0x48,0x8D,0x0D});
  const auto state=Rip(image,factory+(0x5E040-0x5DD90),{0x4C,0x8D,0x0D});
  if(!descriptor||descriptor!=DataExport(image,"DesertLinkInventoryCaptureApiV1")||
     !DataSection(image,descriptor,136,false)||!DataSection(image,state,120,true)||
     Rip(image,owner+0x56,{0x48,0x8B,0x05})!=state+2*40+8||
     Rip(image,capacity+0xC6,{0x48,0x8B,0x05})!=state+40+8||
     Rip(image,factory+(0x5E071-0x5DD90),{0x4C,0x8D,0x25})!=owner||
     Rip(image,factory+(0x5E078-0x5DD90),{0x48,0x8D,0x05})!=capacity)return c;
  // The v5.1.2 factory delegates to a bounded-retry wrapper, not the old
  // direct MH_CreateHook. Require its complete core/trampoline/decode owners
  // and all relevant direct call edges. No target-normalization assumption.
  if(image.Branch(factory+0x306)!=found[8] ||
     image.Branch(found[8]+0x1A3)!=found[18] ||
     image.Branch(found[18]+0x156)!=found[19] ||
     image.Branch(found[19]+0xA6)!=found[23] ||
     image.Branch(found[18]+0x3F)!=found[21] ||
     image.Branch(found[18]+0x54)!=found[20] ||
     image.Branch(found[18]+0x64)!=found[20] ||
     image.Branch(found[18]+0x120)!=found[22] ||
     image.Branch(found[8]+0x111)!=found[22])return c;
  // These two dispatch operands are image-relative displacements, not RIP
  // operands. Bind them to this exact state and the three subscriber lanes.
  // v5.1.2 places subscribers at state+192, independently of the120-byte state.
  uint32_t dispatchState=0,dispatchSubscribers=0;
  if(!image.Read(found[3]+0x8B,dispatchState)||dispatchState!=state+32||
     !image.Read(found[3]+0xA5,dispatchSubscribers)||dispatchSubscribers!=state+192||
     !DataSection(image,dispatchSubscribers,3*0xB90,true))return c;
  if(image.Branch(found[4]+4)!=found[5] ||
     image.Branch(owner+0x70)!=found[3] ||
     image.Branch(capacity+0xB9)!=found[3])return c;
  const auto base=reinterpret_cast<uintptr_t>(module);
  std::array<unsigned char,136> expected{};const uint64_t magic=0x31435041434C4444ULL,abi=0x31395F32315F4944ULL;
  const uint32_t version=1,size=136;std::memcpy(expected.data(),&magic,8);std::memcpy(expected.data()+8,&version,4);
  std::memcpy(expected.data()+12,&size,4);std::memcpy(expected.data()+24,&abi,8);
  std::memcpy(expected.data()+32,&base,8);const auto statePointer=base+state;std::memcpy(expected.data()+40,&statePointer,8);
  const std::array<unsigned,11> fnIndex{9,10,2,1,11,12,13,14,15,16,17};
  for(unsigned i=0;i<fnIndex.size();++i){const auto address=base+found[fnIndex[i]];std::memcpy(expected.data()+48+i*8,&address,8);}
  if(std::memcmp(image.data+descriptor,expected.data(),expected.size()))return c;
  c.module=module;c.owner={base+owner,base+state+2*40+8,6,base+state+2*40};
  c.capacity={base+capacity,base+state+40+8,5,base+state+40};c.valid=true;return c;
 }
 // Exact observed v5.1.2 1A69 provider; complete contract in menu512b profiles.
 static Context FromMenu512b(const v3446::Image& image,const void* module){
  Context c{};using namespace menu512b;
  const std::array<const v3446::Profile*,24> profiles{&kFactory,&kOwner,&kCapacity,&kDispatch,&kInvoke,&kThunk,&kValidateApi,&kPin,&kCreateHook,&kBuiltin,&kCtor,&kCtorCall,&kCapacityCall,&kOwnerCall,&kQuery,&kDescribe,&kSubscribe,&kUnsubscribe,&kCreateHookCore,&kTrampoline,&kExecutable,&kAcquire,&kRead,&kDecode};
  std::array<std::size_t,24> found{};for(unsigned i=0;i<profiles.size();++i)if(!(found[i]=image.Find(*profiles[i])))return c;
  const auto factory=found[0],owner=found[1],capacity=found[2];
  const auto descriptor=Rip(image,factory+(0x5E009-0x5DD90),{0x48,0x8D,0x0D});
  const auto state=Rip(image,factory+(0x5E040-0x5DD90),{0x4C,0x8D,0x0D});
  if(!descriptor||descriptor!=DataExport(image,"DesertLinkInventoryCaptureApiV1")||
     !DataSection(image,descriptor,136,false)||!DataSection(image,state,120,true)||
     Rip(image,owner+0x56,{0x48,0x8B,0x05})!=state+2*40+8||
     Rip(image,capacity+0xC6,{0x48,0x8B,0x05})!=state+40+8||
     Rip(image,factory+(0x5E071-0x5DD90),{0x4C,0x8D,0x25})!=owner||
     Rip(image,factory+(0x5E078-0x5DD90),{0x48,0x8D,0x05})!=capacity)return c;
  // The v5.1.2 factory delegates to a bounded-retry wrapper, not the old
  // direct MH_CreateHook. Require its complete core/trampoline/decode owners
  // and all relevant direct call edges. No target-normalization assumption.
  if(image.Branch(factory+0x306)!=found[8] ||
     image.Branch(found[8]+0x1A3)!=found[18] ||
     image.Branch(found[18]+0x156)!=found[19] ||
     image.Branch(found[19]+0xA6)!=found[23] ||
     image.Branch(found[18]+0x3F)!=found[21] ||
     image.Branch(found[18]+0x54)!=found[20] ||
     image.Branch(found[18]+0x64)!=found[20] ||
     image.Branch(found[18]+0x120)!=found[22] ||
     image.Branch(found[8]+0x111)!=found[22])return c;
  // These two dispatch operands are image-relative displacements, not RIP
  // operands. Bind them to this exact state and the three subscriber lanes.
  // v5.1.2 places subscribers at state+192, independently of the120-byte state.
  uint32_t dispatchState=0,dispatchSubscribers=0;
  if(!image.Read(found[3]+0x8B,dispatchState)||dispatchState!=state+32||
     !image.Read(found[3]+0xA5,dispatchSubscribers)||dispatchSubscribers!=state+192||
     !DataSection(image,dispatchSubscribers,3*0xB90,true))return c;
  if(image.Branch(found[4]+4)!=found[5] ||
     image.Branch(owner+0x70)!=found[3] ||
     image.Branch(capacity+0xB9)!=found[3])return c;
  const auto base=reinterpret_cast<uintptr_t>(module);
  std::array<unsigned char,136> expected{};const uint64_t magic=0x31435041434C4444ULL,abi=0x31395F32315F4944ULL;
  const uint32_t version=1,size=136;std::memcpy(expected.data(),&magic,8);std::memcpy(expected.data()+8,&version,4);
  std::memcpy(expected.data()+12,&size,4);std::memcpy(expected.data()+24,&abi,8);
  std::memcpy(expected.data()+32,&base,8);const auto statePointer=base+state;std::memcpy(expected.data()+40,&statePointer,8);
  const std::array<unsigned,11> fnIndex{9,10,2,1,11,12,13,14,15,16,17};
  for(unsigned i=0;i<fnIndex.size();++i){const auto address=base+found[fnIndex[i]];std::memcpy(expected.data()+48+i*8,&address,8);}
  if(std::memcmp(image.data+descriptor,expected.data(),expected.size()))return c;
  c.module=module;c.owner={base+owner,base+state+2*40+8,6,base+state+2*40};
  c.capacity={base+capacity,base+state+40+8,5,base+state+40};
  c.capacity_call=base+found[12]; // Descriptor +0x58 points at kCapacityCall.
  c.valid=true;return c;
 }
 // Exact DesertLink 5.1.4 (6AE2). Validate the export and the producer's
 // installer, dispatch and MinHook paths before interpreting live records.
 static Context FromMenu514(const v3446::Image& image,const void* module){
  using namespace menu514;
  const std::pair<std::size_t,const v3446::Profile*> profiles[]={
   {kFactoryRva,&kFactory},{kOwnerRva,&kOwner},{kCapacityRva,&kCapacity},{kDispatchRva,&kDispatch},
   {kInvokeRva,&kInvoke},{kThunkRva,&kThunk},{kValidateApiRva,&kValidateApi},{kPinRva,&kPin},
   {kCreateHookRva,&kCreateHook},{kBuiltinRva,&kBuiltin},{kCtorRva,&kCtor},{kCtorCallRva,&kCtorCall},
   {kCapacityCallRva,&kCapacityCall},{kOwnerCallRva,&kOwnerCall},{kQueryRva,&kQuery},{kDescribeRva,&kDescribe},
   {kSubscribeRva,&kSubscribe},{kUnsubscribeRva,&kUnsubscribe},{kCreateHookCoreRva,&kCreateHookCore},
   {kTrampolineRva,&kTrampoline},{kExecutableRva,&kExecutable},{kAcquireRva,&kAcquire},
   {kReadRva,&kRead},{kDecodeRva,&kDecode}
  };
  for(const auto& [rva,profile]:profiles)
   if(!image.Exec(rva,profile->size)||!image.Fragments(rva,*profile)||
      std::memcmp(image.data+rva,profile->code,profile->size))return {};
  const auto descriptor=Rip(image,kFactoryRva+0x304,{0x48,0x8D,0x0D});
  const auto state=Rip(image,kFactoryRva+0x32D,{0x48,0x8D,0x15});
  if(descriptor!=0x30DE00||state!=0x4CC1B0||descriptor!=DataExport(image,"DesertLinkInventoryCaptureApiV1")||
     !DataSection(image,descriptor,136,false)||!DataSection(image,state,120,true)||
     Rip(image,kFactoryRva+0x357,{0x48,0x8D,0x05})!=kCapacityRva||
     Rip(image,kFactoryRva+0x36F,{0x48,0x8D,0x15})!=kOwnerRva||
     Rip(image,kCapacityRva+0xC6,{0x48,0x8B,0x05})!=state+48||
     Rip(image,kOwnerRva+0x56,{0x48,0x8B,0x05})!=state+88||
     Rip(image,kCapacityCallRva+0x151,{0x48,0x8B,0x05})!=state+48)return {};
  const std::pair<std::size_t,std::size_t> edges[]={
   {kFactoryRva+0x30B,kValidateApiRva},{kFactoryRva+0x388,kCreateHookRva},{kFactoryRva+0x653,kValidateApiRva},
   {kCreateHookRva+0x111,kReadRva},{kCreateHookRva+0x1A3,kCreateHookCoreRva},
   {kCreateHookCoreRva+0x3F,kAcquireRva},{kCreateHookCoreRva+0x54,kExecutableRva},{kCreateHookCoreRva+0x64,kExecutableRva},
   {kCreateHookCoreRva+0x120,kReadRva},{kCreateHookCoreRva+0x156,kTrampolineRva},{kTrampolineRva+0xA6,kDecodeRva},
   {kDispatchRva+0x1AB,kInvokeRva},{kInvokeRva+4,kThunkRva},
   {kOwnerRva+0x70,kDispatchRva},{kCapacityRva+0xB9,kDispatchRva}
  };
  for(const auto& [site,target]:edges)if(image.Branch(site,0xE8)!=target)return {};
  uint32_t dispatchState=0,dispatchSubscribers=0;
  if(!image.Read(kDispatchRva+0x8B,dispatchState)||dispatchState!=state+32||
     !image.Read(kDispatchRva+0xB5,dispatchSubscribers)||dispatchSubscribers!=state+192||
     !DataSection(image,dispatchSubscribers,3*0xB90,true))return {};
  const auto base=reinterpret_cast<uintptr_t>(module);
  const std::array<uint64_t,17> expected{
   0x31435041434C4444ULL,0x8800000001ULL,0,0x31395F32315F4944ULL,base,base+state,
   base+kBuiltinRva,base+kCtorRva,base+kCapacityRva,base+kOwnerRva,base+kCtorCallRva,base+kCapacityCallRva,
   base+kOwnerCallRva,base+kQueryRva,base+kDescribeRva,base+kSubscribeRva,base+kUnsubscribeRva
  };
  if(std::memcmp(image.data+descriptor,expected.data(),sizeof(expected)))return {};
  Context c{};
  c.module=module;c.owner={base+kOwnerRva,base+state+88,6,base+state+80};
  c.capacity={base+kCapacityRva,base+state+48,5,base+state+40};c.capacity_call=base+kCapacityCallRva;
  c.valid=true;return c;
 }
 // The 5.1.6 factory uses a revised capture installer and publishes
 // the same V1 record layout. Prove both before observing its detours.
 static Context FromMenu516(const v3446::Image& image,const void* module){
  using namespace menu516;
  const std::pair<std::size_t,const v3446::Profile*> profiles[]={
   {kFactoryRva,&kFactory},{kOwnerRva,&kOwner},{kCapacityRva,&kCapacity},{kDispatchRva,&kDispatch},
   {kInvokeRva,&kInvoke},{kThunkRva,&kThunk},{kValidateApiRva,&kValidateApi},{kPinRva,&kPin},
   {kCreateHookRva,&kCreateHook},{kBuiltinRva,&kBuiltin},{kCtorRva,&kCtor},{kCtorCallRva,&kCtorCall},
   {kCapacityCallRva,&kCapacityCall},{kOwnerCallRva,&kOwnerCall},{kQueryRva,&kQuery},{kDescribeRva,&kDescribe},
   {kSubscribeRva,&kSubscribe},{kUnsubscribeRva,&kUnsubscribe},{kCreateHookCoreRva,&kCreateHookCore},
   {kTrampolineRva,&kTrampoline},{kExecutableRva,&kExecutable},{kAcquireRva,&kAcquire},
   {kReadRva,&kRead},{kDecodeRva,&kDecode}
  };
  for(const auto& [rva,profile]:profiles)
   if(!image.Exec(rva,profile->size)||!image.Fragments(rva,*profile)||
      std::memcmp(image.data+rva,profile->code,profile->size))return {};
  const auto descriptor=Rip(image,0x6F347,{0x48,0x8D,0x0D});
  const auto state=Rip(image,0x6F36D,{0x48,0x8D,0x0D});
  if(descriptor!=0x31D530||state!=0x4DD7F0||descriptor!=DataExport(image,"DesertLinkInventoryCaptureApiV1")||
     !DataSection(image,descriptor,136,false)||!DataSection(image,state,120,true)||
     Rip(image,0x6F392,{0x48,0x8D,0x0D})!=kCapacityRva||
     Rip(image,0x6F3AF,{0x48,0x8D,0x15})!=kOwnerRva||
     Rip(image,kCapacityRva+0xC6,{0x48,0x8B,0x05})!=state+48||
     Rip(image,kOwnerRva+0x56,{0x48,0x8B,0x05})!=state+88||
     Rip(image,kCapacityCallRva+0x151,{0x48,0x8B,0x05})!=state+48)return {};
  const std::pair<std::size_t,std::size_t> edges[]={
   {0x6F34E,kValidateApiRva},{0x6F3C8,kCreateHookRva},{0x6F679,kValidateApiRva},
   {0x284CA1,kReadRva},{0x284D33,kCreateHookCoreRva},
   {0x283E8D,kAcquireRva},{0x283EB8,kExecutableRva},{0x283EC8,kExecutableRva},
   {0x283F6F,kReadRva},{0x283FA5,kTrampolineRva},{0x285596,kDecodeRva},
   {0x7B0C4,kInvokeRva},{kInvokeRva+4,kThunkRva},
   {kOwnerRva+0x70,kDispatchRva},{kCapacityRva+0xB9,kDispatchRva}
  };
  for(const auto& [site,target]:edges)if(image.Branch(site,0xE8)!=target)return {};
  uint32_t dispatchState=0,dispatchSubscribers=0;
  if(!image.Read(0x7AF9B,dispatchState)||dispatchState!=state+32||
     !image.Read(0x7AFB5,dispatchSubscribers)||dispatchSubscribers!=state+192||
     !DataSection(image,dispatchSubscribers,3*0xB90,true))return {};
  const auto base=reinterpret_cast<uintptr_t>(module);
  const std::array<uint64_t,17> expected{
   0x31435041434C4444ULL,0x8800000001ULL,0,0x31395F32315F4944ULL,base,base+state,
   base+kBuiltinRva,base+kCtorRva,base+kCapacityRva,base+kOwnerRva,base+kCtorCallRva,base+kCapacityCallRva,
   base+kOwnerCallRva,base+kQueryRva,base+kDescribeRva,base+kSubscribeRva,base+kUnsubscribeRva
  };
  if(std::memcmp(image.data+descriptor,expected.data(),sizeof(expected)))return {};
  Context c{};
  c.module=module;c.owner={base+kOwnerRva,base+state+88,6,base+state+80};
  c.capacity={base+kCapacityRva,base+state+48,5,base+state+40};c.capacity_call=base+kCapacityCallRva;
  c.valid=true;return c;
 }
 static Context From(const void* module){
  Context c{};if(!module)return c;v3446::Image image;
  if(!image.Init(module,v3446::ModuleSize(module)))return c;
  if(image.nt.FileHeader.TimeDateStamp==0x847F423Du&&image.size==0xA1E000&&image.nt.OptionalHeader.CheckSum==0)return FromMenu516(image,module);
  if(image.nt.FileHeader.TimeDateStamp==0x7CCDF8C0u&&image.size==0x990000&&image.nt.OptionalHeader.CheckSum==0)return FromMenu514(image,module);
  if(image.nt.FileHeader.TimeDateStamp==0x1BB5DE60u&&image.size==0x95E000&&image.nt.OptionalHeader.CheckSum==0)return FromMenu512b(image,module);
  if(image.nt.FileHeader.TimeDateStamp==0x8AD596CEu&&image.size==0x95C000&&image.nt.OptionalHeader.CheckSum==0)return FromMenu512(image,module);
  if(image.nt.FileHeader.TimeDateStamp==0xADDDE3E9u&&image.size==0x955000&&image.nt.OptionalHeader.CheckSum==0)return FromMenu510(image,module);
  if(image.nt.FileHeader.TimeDateStamp==0xC5F92811u&&image.size==0x934000&&image.nt.OptionalHeader.CheckSum==0)return FromMenu509(image,module);
  if(image.nt.FileHeader.TimeDateStamp==0xFC951675u&&image.size==0x920000&&image.nt.OptionalHeader.CheckSum==0)return FromMenu206(image,module);
  if(image.nt.FileHeader.TimeDateStamp!=0xC1E77D35u||
     image.size!=0x473000||image.nt.OptionalHeader.CheckSum!=0)return c;
  const auto installer=image.Find(kInstaller),owner=image.Find(kOwnerHandler),capacity=image.Find(kCapacityHandler);
  if(!installer||!owner||!capacity)return c;
  const auto owner_slot=Rip(image,installer+0x633,{0x4C,0x8D,0x05});
  const auto capacity_slot=Rip(image,installer+0x5B1,{0x4C,0x8D,0x05});
  const auto owner_target=Rip(image,installer+0x63A,{0x48,0x8D,0x15});
  const auto capacity_target=Rip(image,installer+0x5B8,{0x48,0x8D,0x15});
  if(!owner_slot||!capacity_slot||owner_target!=owner||capacity_target!=capacity||
     owner_slot%8||capacity_slot%8||image.Exec(owner_slot,8)||image.Exec(capacity_slot,8)||
     Rip(image,owner+0xD,{0x48,0x8B,0x05})!=owner_slot||
     Rip(image,capacity+0x180,{0x4C,0x8B,0x15})!=capacity_slot)return c;
  const auto base=reinterpret_cast<uintptr_t>(module);c.module=module;
  c.owner={base+owner,base+owner_slot,6};c.capacity={base+capacity,base+capacity_slot,5};c.valid=true;return c;
 }
 bool Same(const Context& other)const{
  return valid&&other.valid&&module==other.module&&capacity_call==other.capacity_call&&owner.handler==other.owner.handler&&
   owner.original_slot==other.owner.original_slot&&owner.record==other.owner.record&&capacity.record==other.capacity.record&&capacity.handler==other.capacity.handler&&
   capacity.original_slot==other.capacity.original_slot&&owner.span==other.owner.span&&capacity.span==other.capacity.span;
 }
};
struct Snapshot {
 std::array<unsigned char,4096> native{};
 std::array<unsigned char,64> bridge{};
 uintptr_t original=0,relay=0;
 std::array<uint64_t,5> record{};
 bool operator==(const Snapshot&)const=default;
};
struct NativeMatch {std::size_t rva=0;bool captured=false;Snapshot snapshot{};};
inline bool AbsoluteJump(const unsigned char* p,uintptr_t target){
 constexpr unsigned char prefix[6]={0xFF,0x25,0,0,0,0};uintptr_t got=0;
 std::memcpy(&got,p+6,sizeof(got));return !std::memcmp(p,prefix,6)&&got==target;
}
inline bool ObserveStrict(const v3446::Image& image,const v3446::Profile& profile,std::size_t rva,
                          Snapshot& out,const Reader& reader={}){
 if(!rva||profile.size>out.native.size()||!image.Exec(rva,profile.size)||!image.Fragments(rva,profile))return false;
 const auto entry=reinterpret_cast<uintptr_t>(image.data)+rva;
 return reader.Read(entry,out.native.data(),profile.size)&&!std::memcmp(out.native.data(),profile.code,profile.size)&&image.Fragments(rva,profile);
}
inline bool Observe(const v3446::Image& image,const v3446::Profile& profile,std::size_t rva,
                    const Capture& capture,Snapshot& out,const Reader& reader={}){
 if(!rva||profile.size>out.native.size()||profile.size<=capture.span||capture.span<5||capture.span+28>out.bridge.size()||
    !capture.original_slot||!capture.handler||!image.Exec(rva,profile.size)||!image.Fragments(rva,profile))return false;
 const auto entry=reinterpret_cast<uintptr_t>(image.data)+rva;
 if(!reader.Read(entry,out.native.data(),profile.size)||out.native[0]!=0xE9||
    std::memcmp(out.native.data()+5,profile.code+5,profile.size-5)||
    !reader.Read(capture.original_slot,&out.original,sizeof(out.original))||!out.original)return false;
 std::int32_t displacement=0;std::memcpy(&displacement,out.native.data()+1,4);
 const auto relative=std::int64_t(entry)+5+displacement;
 if(relative<=0||out.original>(std::numeric_limits<uintptr_t>::max)()-capture.span-28)return false;
 out.relay=std::size_t(relative);
 if(out.relay!=out.original+capture.span+14||!reader.Exec(out.original,capture.span+28)||
    !reader.Read(out.original,out.bridge.data(),capture.span+28)||
    std::memcmp(out.bridge.data(),profile.code,capture.span)||
    !AbsoluteJump(out.bridge.data()+capture.span,entry+capture.span)||
    !AbsoluteJump(out.bridge.data()+capture.span+14,capture.handler))return false;
 if(capture.record){
  if(!reader.Read(capture.record,out.record.data(),sizeof(out.record))||out.record[0]!=entry||out.record[1]!=out.original||
     out.record[2]!=out.relay||!out.record[3]||out.record[4]!=0x100000001ULL)return false;
 }
 return image.Fragments(rva,profile);
}
inline NativeMatch Find(const v3446::Image& image,const v3446::Profile& profile,
                        const Capture& capture,const Reader& reader={}){
 NativeMatch result{};
 if(const auto strict=image.Find(profile)){result.rva=strict;if(!ObserveStrict(image,profile,strict,result.snapshot,reader))return {};return result;}
 if(!capture.handler||profile.size<=5)return result;
 const v3446::Profile suffix{profile.code+5,profile.size-5,nullptr,0};
 const auto tail=image.Find(suffix,false);if(tail<5)return result;
 result.rva=tail-5;
 Snapshot second{};
 if(!Observe(image,profile,result.rva,capture,result.snapshot,reader)||
    !Observe(image,profile,result.rva,capture,second,reader)||!(second==result.snapshot))return {};
 result.captured=true;return result;
}
inline bool Stable(const v3446::Image& image,const v3446::Profile& profile,const NativeMatch& match,
                   const Capture& capture,const Reader& reader={}){
 Snapshot current{};
 const bool valid=match.captured?Observe(image,profile,match.rva,capture,current,reader):ObserveStrict(image,profile,match.rva,current,reader);
 return valid&&current==match.snapshot;
}
}
