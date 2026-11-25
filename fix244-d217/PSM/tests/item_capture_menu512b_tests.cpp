#include <Windows.h>
#include "item_3446.h"
#include "item_capture_menu512b_profiles.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <type_traits>
#include <tuple>
#include <vector>
namespace {
unsigned checks=0,failures=0;
void Check(bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAIL: %s\n",name);}}
struct Map {
 unsigned char* p=nullptr;size_t size=0;
 explicit Map(const char* path){
  std::ifstream in(path,std::ios::binary);std::vector<char>b((std::istreambuf_iterator<char>(in)),{});
  if(b.size()<4096)return;auto*d=reinterpret_cast<const IMAGE_DOS_HEADER*>(b.data());auto*n=reinterpret_cast<const IMAGE_NT_HEADERS64*>(b.data()+d->e_lfanew);
  size=n->OptionalHeader.SizeOfImage;p=static_cast<unsigned char*>(VirtualAlloc(nullptr,size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));if(!p)return;
  std::memcpy(p,b.data(),n->OptionalHeader.SizeOfHeaders);auto*s=IMAGE_FIRST_SECTION(n);
  for(unsigned i=0;i<n->FileHeader.NumberOfSections;++i)std::memcpy(p+s[i].VirtualAddress,b.data()+s[i].PointerToRawData,s[i].SizeOfRawData);
  const auto reloc=n->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
  const auto delta=reinterpret_cast<uintptr_t>(p)-n->OptionalHeader.ImageBase;
  for(size_t at=reloc.VirtualAddress;reloc.Size&&at<reloc.VirtualAddress+reloc.Size;){
   auto* block=reinterpret_cast<IMAGE_BASE_RELOCATION*>(p+at);if(block->SizeOfBlock<8)break;
   auto* fix=reinterpret_cast<uint16_t*>(block+1);
   for(unsigned j=0;j<(block->SizeOfBlock-8)/2;++j)if((fix[j]>>12)==IMAGE_REL_BASED_DIR64)*reinterpret_cast<uintptr_t*>(p+block->VirtualAddress+(fix[j]&0xfff))+=delta;
   at+=block->SizeOfBlock;
  }
 }
 ~Map(){if(p)VirtualFree(p,0,MEM_RELEASE);}
};
struct Fixture {
 Map game,foreign;unsigned char* blobs=nullptr;
 std::array<unsigned char,6> owner{};std::array<unsigned char,5> capacity{};
 Fixture(const char*g,const char*f):game(g),foreign(f){
  if(!game.p||!foreign.p)return;
  std::memcpy(owner.data(),game.p+0x22f1dc0,owner.size());std::memcpy(capacity.data(),game.p+0x25fcdb0,capacity.size());
  auto cursor=reinterpret_cast<uintptr_t>(game.p);
  const auto limit=cursor+0x60000000;
  for(unsigned i=0;i<128&&!blobs&&cursor<limit;++i){
   MEMORY_BASIC_INFORMATION region{};if(!VirtualQuery(reinterpret_cast<void*>(cursor),&region,sizeof(region)))break;
   const auto begin=reinterpret_cast<uintptr_t>(region.BaseAddress);const auto end=begin+region.RegionSize;if(end<=cursor)break;
   if(region.State==MEM_FREE){const auto at=(cursor+0xffff)&~uintptr_t{0xffff};if(at<=end&&0x10000<=end-at)blobs=static_cast<unsigned char*>(VirtualAlloc(reinterpret_cast<void*>(at),0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));}
   cursor=end;
  }

 }
 ~Fixture(){if(blobs)VirtualFree(blobs,0,MEM_RELEASE);}
 bool good(){return game.p&&foreign.p&&blobs;}
 unsigned char* entry(unsigned role){return game.p+(role?0x25fcdb0:0x22f1dc0);}
 unsigned span(unsigned role){return role?5:6;}
 unsigned char* trampoline(unsigned role){return blobs+role*64;}
 unsigned char* relay(unsigned role){return trampoline(role)+span(role)+14;}
 uintptr_t* original_slot(unsigned role){return reinterpret_cast<uintptr_t*>(foreign.p+(role?0x49d6f0:0x49d718));}
 void absolute(unsigned char* at,uintptr_t dst){const unsigned char op[6]={0xff,0x25,0,0,0,0};std::memcpy(at,op,6);std::memcpy(at+6,&dst,8);}
 void patch(unsigned role){
  auto*e=entry(role);auto*t=trampoline(role);auto*r=relay(role);const auto n=span(role);
  std::memcpy(t,role?capacity.data():owner.data(),n);absolute(t+n,reinterpret_cast<uintptr_t>(e+n));absolute(r,reinterpret_cast<uintptr_t>(foreign.p+(role?0x65a70:0x65b90)));
  const auto delta=reinterpret_cast<intptr_t>(r)-reinterpret_cast<intptr_t>(e+5);Check(delta>=INT32_MIN&&delta<=INT32_MAX,"private relay lies within rel32 range");
  const auto disp=static_cast<int32_t>(delta);e[0]=0xe9;std::memcpy(e+1,&disp,4); // MinHook writes5, not stolen-span padding.
  *original_slot(role)=reinterpret_cast<uintptr_t>(t);
  auto*record=reinterpret_cast<uintptr_t*>(foreign.p+0x49d6c0+(role?1:2)*40);
  record[0]=reinterpret_cast<uintptr_t>(e);record[1]=reinterpret_cast<uintptr_t>(t);record[2]=reinterpret_cast<uintptr_t>(r);record[3]=1;record[4]=0x100000001ULL;
 }
 void restore(){std::memcpy(entry(0),owner.data(),owner.size());std::memcpy(entry(1),capacity.data(),capacity.size());*original_slot(0)=*original_slot(1)=0;}
};
bool SameTarget(const ItemGainCaptureTarget& a,const ItemGainCaptureTarget& b){return std::tie(a.address,a.capture_kind,a.caller_return,a.item_info_lookup,a.push_function,a.standalone_caller_return,a.inventory_type_table_global,a.plan_site,a.plan_caller_return,a.container_mapper,a.metadata_kind,a.stack_size_offset,a.weighted_offset,a.item_id_offset,a.native_plan_function,a.native_inventory_owner_lookup,a.native_move_parent,a.move_parent_caller_return,a.move_parent_plan_return)==std::tie(b.address,b.capture_kind,b.caller_return,b.item_info_lookup,b.push_function,b.standalone_caller_return,b.inventory_type_table_global,b.plan_site,b.plan_caller_return,b.container_mapper,b.metadata_kind,b.stack_size_offset,b.weighted_offset,b.item_id_offset,b.native_plan_function,b.native_inventory_owner_lookup,b.native_move_parent,b.move_parent_caller_return,b.move_parent_plan_return);}
unsigned observed_mask=0;
template<class F> ItemGainCaptureTarget Resolve(F fn,Fixture& x){
 if constexpr(std::is_invocable_v<F,const void*,const void*,unsigned*>) {observed_mask=0;return fn(x.game.p,x.foreign.p,&observed_mask);}
 else return fn(x.game.p);
}
}
int main(int argc,char**argv){
 if(argc!=3)return 2;Fixture f(argv[1],argv[2]);Check(f.good(),"private inert PE maps and synthetic MinHook-shaped data allocated");if(!f.good()){std::printf("maps game=%p size=%zu foreign=%p size=%zu blobs=%p last-error=%lu\n",f.game.p,f.game.size,f.foreign.p,f.foreign.size,f.blobs,GetLastError());return 2;}
 const auto pristine=Resolve(psm::items3446::Resolve,f);
 auto test=[&](const char* label,unsigned mask){auto got=Resolve(psm::items3446::Resolve,f);Check(got.address==reinterpret_cast<uintptr_t>(f.game.p)+0xd6ff310,label);Check(observed_mask==mask,"capture mask exactly describes accepted observers");Check(SameTarget(got,pristine),"all19 returned ABI/address/layout fields match pristine target");if(got.address)Check(got.native_inventory_owner_lookup==reinterpret_cast<uintptr_t>(f.game.p)+0x22f1dc0,"returned owner stays native entry, never foreign trampoline");};
 test("pristine exact item group accepted",0);
 f.patch(0);test("owner capture preserves proven item contracts",1);f.restore();
 f.patch(1);test("capacity capture preserves proven item contracts",2);f.restore();
 f.patch(0);f.patch(1);test("both existing captures preserve proven item contracts",3);
 Check(f.entry(0)[5]==0x20,"owner sixth byte remains original, not NOP");
 f.restore();test("restoring original code still uses strict path",0);
 {
  psm::v3446::Image image;Check(image.Init(f.game.p,f.game.size),"strict revalidation fixture initialized");
  const auto original=psm::capture_compat::Find(image,psm::items3446::kOwner,{});
  Check(original.rva==0x22f1dc0&&!original.captured,"strict native owner found before mutation");
  const auto op=f.entry(0)[0];f.entry(0)[0]^=1;
  Check(!psm::capture_compat::Stable(image,psm::items3446::kOwner,original,{}),"strict entry mutation rejected at final validation");f.entry(0)[0]=op;
  f.entry(0)[20]^=1;
  Check(!psm::capture_compat::Stable(image,psm::items3446::kOwner,original,{}),"strict body mutation rejected at final validation");f.entry(0)[20]^=1;
  for(std::size_t at=image.exception.VirtualAddress;at<image.exception.VirtualAddress+image.exception.Size;at+=sizeof(RUNTIME_FUNCTION)){
   RUNTIME_FUNCTION row{};image.Read(at,row);if(row.BeginAddress==original.rva){
    f.game.p[at+4]^=1;Check(!psm::capture_compat::Stable(image,psm::items3446::kOwner,original,{}),"strict pdata mutation rejected at final validation");f.game.p[at+4]^=1;break;
   }
  }
 }
 {
  namespace cc=psm::capture_compat;using namespace psm::items3446;
  psm::v3446::Image image;Check(image.Init(f.game.p,f.game.size),"capture mutation fixture initialized");
  f.patch(0);f.patch(1);const auto context=cc::Context::From(f.foreign.p);Check(context.valid,"pinned foreign full profiles and RIP bindings accepted");
  auto observe=[&](unsigned role,const cc::Reader& reader=cc::Reader{}){cc::Snapshot snap{};return cc::Observe(image,role?kBorrowCapacityPlanner:kOwner,role?0x25fcdb0:0x22f1dc0,role?context.capacity:context.owner,snap,reader);};
  Check(observe(0)&&observe(1),"both exact live capture proofs accepted");
  for(unsigned role=0;role<2;++role){
   auto*record=f.foreign.p+0x49d6c0+(role?1:2)*40;
   for(unsigned offset:{0u,8u,16u,24u,32u,36u}){const auto save=record[offset];record[offset]^=1;cc::Snapshot snap{};Check(!cc::Observe(image,role?kBorrowCapacityPlanner:kOwner,role?0x25fcdb0:0x22f1dc0,role?context.capacity:context.owner,snap),"changed instance/state record refuses captured route");record[offset]=save;}
  }
  for(unsigned offset:{0u,8u,12u,16u,20u,24u,32u,40u,48u,56u,64u,72u,80u,88u,96u,104u,112u,120u,128u}){
   auto*field=f.foreign.p+0x2e84d0+offset;const auto save=*field;*field^=1;Check(!cc::Context::From(f.foreign.p).valid,"changed rebased DATA descriptor refuses provider");*field=save;
  }
  auto*provider=reinterpret_cast<uintptr_t*>(f.foreign.p+0x2e84d0+32);const auto savedProvider=*provider;
  *provider=0x180000000ULL;Check(!cc::Context::From(f.foreign.p).valid,"preferred image base is not accepted as actual loaded provider");*provider=savedProvider;
  auto mutate=[&](unsigned char* at,unsigned role,const char* label){const auto saved=*at;*at^=1;Check(!observe(role),label);*at=saved;};
  for(unsigned role=0;role<2;++role){
   mutate(f.entry(role),role,"unknown entry opcode rejected");
   mutate(f.entry(role)+1,role,"changed relay displacement rejected");
   mutate(f.entry(role)+5,role,"every original native suffix byte remains exact");
   mutate(f.entry(role)+20,role,"native body drift rejected");
   mutate(reinterpret_cast<unsigned char*>(f.original_slot(role)),role,"changed foreign original-pointer slot rejected");
   mutate(f.trampoline(role),role,"changed original trampoline prefix rejected");
   mutate(f.trampoline(role)+f.span(role),role,"changed trampoline jump opcode rejected");
   mutate(f.trampoline(role)+f.span(role)+6,role,"changed native continuation rejected");
   mutate(f.relay(role),role,"changed relay jump opcode rejected");
   mutate(f.relay(role)+6,role,"unknown foreign handler rejected");
   const auto saved=*f.original_slot(role);*f.original_slot(role)=~uintptr_t{0}-4;Check(!observe(role),"overflowed original trampoline pointer rejected");*f.original_slot(role)=saved;
  }
  DWORD old=0;Check(VirtualProtect(f.blobs,0x10000,PAGE_READWRITE,&old)!=0,"private bridge execute permission removed");
  Check(!observe(0)&&!observe(1),"nonexecutable bridge pages rejected");DWORD unused=0;VirtualProtect(f.blobs,0x10000,old,&unused);
  cc::Reader badread{};badread.copy=[](void*,uintptr_t,void*,std::size_t){return false;};Check(!observe(0,badread),"unreadable capture proof rejected");
  cc::Reader badquery{};badquery.executable=[](void*,uintptr_t,std::size_t){return false;};Check(!observe(1,badquery),"failed bridge page query rejected");
  const auto stamp_at=static_cast<IMAGE_DOS_HEADER*>(static_cast<void*>(f.foreign.p))->e_lfanew+8;
  f.foreign.p[stamp_at]^=1;Check(!cc::Context::From(f.foreign.p).valid,"unknown module timestamp rejected");f.foreign.p[stamp_at]^=1;
  for(const auto at: {std::size_t{0x698f0+50},std::size_t{0x65b90+40},std::size_t{0x65a70+40},std::size_t{0x75990+40},std::size_t{0x75c50+5}}){f.foreign.p[at]^=1;Check(!cc::Context::From(f.foreign.p).valid,"foreign installer/handler body mutation rejected");f.foreign.p[at]^=1;}
  struct Torn {uintptr_t watched=0;unsigned reads=0;};
  for(unsigned role=0;role<2;++role){
   Torn torn{reinterpret_cast<uintptr_t>(f.original_slot(role)),0};cc::Reader reader{};reader.opaque=&torn;
   reader.copy=[](void* opaque,uintptr_t at,void*out,std::size_t n){auto&t=*static_cast<Torn*>(opaque);if(!cc::SafeCopy(at,out,n))return false;if(at==t.watched&&++t.reads==2)static_cast<unsigned char*>(out)[0]^=1;return true;};
   const auto match=cc::Find(image,role?kBorrowCapacityPlanner:kOwner,role?context.capacity:context.owner,reader);
   Check(!match.rva,"torn original-pointer observation rejected");
  }
  struct GenerationTorn {uintptr_t record;unsigned reads=0;} generationTorn{context.owner.record,0};
  cc::Reader recordReader{};recordReader.opaque=&generationTorn;recordReader.copy=[](void*opaque,uintptr_t at,void*out,std::size_t n){auto&t=*static_cast<GenerationTorn*>(opaque);if(!cc::SafeCopy(at,out,n))return false;if(at==t.record&&n==40&&++t.reads==2)static_cast<uint64_t*>(out)[3]=2;return true;};
  Check(!cc::Find(image,kOwner,context.owner,recordReader).rva,"two valid distinct provider generations refuse a torn capture");
  const auto captured=cc::Find(image,kOwner,context.owner);Check(captured.captured,"complete capture snapshot retained");
  mutate(f.entry(0)+30,0,"later native suffix mutation rejected");
  f.relay(0)[6]^=1;Check(!cc::Stable(image,kOwner,captured,context.owner),"later foreign chain change rejected");f.relay(0)[6]^=1;
  // Two individually valid chains observed on consecutive snapshots must fail equality.
  auto* alternate=f.blobs+128;std::memcpy(alternate,f.owner.data(),6);f.absolute(alternate+6,reinterpret_cast<uintptr_t>(f.entry(0)+6));f.absolute(alternate+20,context.owner.handler);
  struct Switch {uintptr_t entry,slot,alternate;unsigned reads=0;} switched{reinterpret_cast<uintptr_t>(f.entry(0)),reinterpret_cast<uintptr_t>(f.original_slot(0)),reinterpret_cast<uintptr_t>(alternate),0};
  cc::Reader moving{};moving.opaque=&switched;
  moving.copy=[](void* opaque,uintptr_t at,void*out,std::size_t n){auto&t=*static_cast<Switch*>(opaque);if(!cc::SafeCopy(at,out,n))return false;if(at==t.entry){++t.reads;if(t.reads>=2){const auto disp=static_cast<int32_t>(t.alternate+20-(t.entry+5));std::memcpy(static_cast<unsigned char*>(out)+1,&disp,4);}}else if(at==t.slot&&t.reads>=2)std::memcpy(out,&t.alternate,8);return true;};
  Check(!cc::Find(image,kOwner,context.owner,moving).rva,"two different valid chain snapshots are rejected");
  f.restore();
 }

 {
  namespace cc=psm::capture_compat;namespace m=cc::menu512b;
  struct Owner { const psm::v3446::Profile* profile;std::size_t rva; };
  const Owner owners[]={{&m::kFactory,0x698f0},{&m::kOwner,0x65b90},{&m::kCapacity,0x65a70},{&m::kDispatch,0x75990},{&m::kInvoke,0x75c50},{&m::kThunk,0x34830},{&m::kValidateApi,0x7cab0},{&m::kPin,0x758d0},{&m::kCreateHook,0x254a30},{&m::kBuiltin,0x660a0},{&m::kCtor,0x658e0},{&m::kCtorCall,0x65c30},{&m::kCapacityCall,0x65df0},{&m::kOwnerCall,0x65f90},{&m::kQuery,0x65330},{&m::kDescribe,0x65550},{&m::kSubscribe,0x65700},{&m::kUnsubscribe,0x65800},{&m::kCreateHookCore,0x253d70},{&m::kTrampoline,0x255340},{&m::kExecutable,0x253ce0},{&m::kAcquire,0x254430},{&m::kRead,0x254fa0},{&m::kDecode,0x255740}};
  Check(cc::Context::From(f.foreign.p).valid,"new exact Menu recognized with native entries still pristine");
  for(const auto& owner:owners){
   const auto at=owner.rva+owner.profile->size-1;f.foreign.p[at]^=1;
   Check(!cc::Context::From(f.foreign.p).valid,"every whole required provider owner is byte-exact through its final byte");f.foreign.p[at]^=1;
   psm::v3446::Image provider;Check(provider.Init(f.foreign.p,f.foreign.size),"provider pdata mutation fixture initialized");
   bool changed=false;
   for(std::size_t pos=provider.exception.VirtualAddress;pos<provider.exception.VirtualAddress+provider.exception.Size;pos+=sizeof(RUNTIME_FUNCTION)){
    RUNTIME_FUNCTION row{};provider.Read(pos,row);if(row.BeginAddress!=owner.rva)continue;
    f.foreign.p[pos+4]^=1;Check(!cc::Context::From(f.foreign.p).valid,"every provider owner requires exact pdata span");f.foreign.p[pos+4]^=1;changed=true;break;
   }Check(changed,"every required provider owner has pdata row");
  }
  auto*nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(f.foreign.p+reinterpret_cast<IMAGE_DOS_HEADER*>(f.foreign.p)->e_lfanew);
  const auto checksum=nt->OptionalHeader.CheckSum;nt->OptionalHeader.CheckSum=1;Check(!cc::Context::From(f.foreign.p).valid,"unknown checksum refuses capture contract");nt->OptionalHeader.CheckSum=checksum;
  const auto stamp=nt->FileHeader.TimeDateStamp;nt->FileHeader.TimeDateStamp=0xc5f92811;Check(!cc::Context::From(f.foreign.p).valid,"old509 timestamp cannot authorize current512 image");nt->FileHeader.TimeDateStamp=stamp;

  // TLS guard operand and all decoder inline DATA lanes are part of the exact profile.
  for(const auto at: {std::size_t{0x65330+0x25},std::size_t{0x65550+0x1e},std::size_t{0x65700+0x21},std::size_t{0x65800+0x1d},std::size_t{0x698f0+0x56},std::size_t{0x75990+0x5d},std::size_t{0x75990+0x1fc}}){f.foreign.p[at+1]^=1;Check(!cc::Context::From(f.foreign.p).valid,"each TLS64 guard instruction operand is pinned");f.foreign.p[at+1]^=1;}
  for(unsigned off=0xb18;off<0xb34;off+=4){f.foreign.p[0x255740+off]^=1;Check(!cc::Context::From(f.foreign.p).valid,"each decoder inline switch RVA remains exact");f.foreign.p[0x255740+off]^=1;}
  {
   const auto e=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
   auto*exp=reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(f.foreign.p+e.VirtualAddress);
   for(unsigned i=0;i<exp->NumberOfNames;++i){auto name=*reinterpret_cast<DWORD*>(f.foreign.p+exp->AddressOfNames+i*4);if(std::strcmp(reinterpret_cast<char*>(f.foreign.p+name),"DesertLinkInventoryCaptureApiV1"))continue;
    const auto ord=*reinterpret_cast<WORD*>(f.foreign.p+exp->AddressOfNameOrdinals+i*2);auto*slot=reinterpret_cast<DWORD*>(f.foreign.p+exp->AddressOfFunctions+ord*4);const auto save=*slot;*slot+=8;Check(!cc::Context::From(f.foreign.p).valid,"export name must bind exact typed DATA descriptor");*slot=save;
   }
  }
  auto*sections=IMAGE_FIRST_SECTION(nt);
  for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i){auto&s=sections[i];const auto extent=(std::max)(s.Misc.VirtualSize,s.SizeOfRawData);
   if(0x49d6c0>=s.VirtualAddress&&0x49d6c0-s.VirtualAddress<extent){const auto flags=s.Characteristics;s.Characteristics&=~IMAGE_SCN_MEM_WRITE;Check(!cc::Context::From(f.foreign.p).valid,"state and subscribers must be in writable DATA");s.Characteristics=flags;}
   if(0x2e84d0>=s.VirtualAddress&&0x2e84d0-s.VirtualAddress<extent){const auto flags=s.Characteristics;s.Characteristics|=IMAGE_SCN_MEM_WRITE;Check(!cc::Context::From(f.foreign.p).valid,"exported descriptor must be in readonly DATA");s.Characteristics=flags;}
  }
  {
   Map second(argv[2]);Check(second.p&&second.p!=f.foreign.p,"second inert provider map has a distinct actual base");
   const auto rebased=cc::Context::From(second.p);Check(rebased.valid&&rebased.module==second.p&&rebased.owner.original_slot==reinterpret_cast<uintptr_t>(second.p)+0x49d718,"descriptor and function/data addresses bind to independently rebased actual map");
  }
  psm::v3446::Image game;Check(game.Init(f.game.p,f.game.size),"late capture fixture initialized");
  const auto context=cc::Context::From(f.foreign.p);
  const auto before=cc::Find(game,psm::items3446::kOwner,context.owner);Check(before.rva&&!before.captured,"late provider starts from strict native entry");
  f.patch(0);Check(!cc::Stable(game,psm::items3446::kOwner,before,context.owner),"late capture invalidates the strict startup snapshot");
  const auto after=cc::Find(game,psm::items3446::kOwner,context.owner);Check(after.rva&&after.captured,"exact direct Menu capture is provable when observed after native startup");
  std::array<unsigned char,14> save{};std::memcpy(save.data(),f.trampoline(0),save.size());
  f.absolute(f.trampoline(0),reinterpret_cast<uintptr_t>(f.blobs+512));
  Check(!cc::Find(game,psm::items3446::kOwner,context.owner).rva,"late Menu hook with unknown preceding native jump chain is refused");
  std::memcpy(f.trampoline(0),save.data(),save.size());f.restore();
 }

 std::printf("item-capture-menu512b: %u checks, %u failures; no game/DLL bytes executed\n",checks,failures);return failures?1:0;
}
