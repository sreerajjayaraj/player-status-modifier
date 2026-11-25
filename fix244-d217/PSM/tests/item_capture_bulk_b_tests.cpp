#include <Windows.h>
#include "item_3446.h"
#include "item_capture_menu512_profiles.h"
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

struct BulkFixture:Fixture {
 Map bulk;
 BulkFixture(const char*g,const char*m,const char*b):Fixture(g,m),bulk(b){}
 unsigned char* bt(){return blobs+1024;}
 unsigned char* br(bool nested){return bt()+(nested?14:20);}
 uintptr_t* bs(){return reinterpret_cast<uintptr_t*>(bulk.p+0x22d700);}
 unsigned char* rec(){return blobs+4096;}
 void installBulk(bool nested){
  auto*e=entry(0);auto*t=bt();auto*r=br(nested);auto*record=rec();std::memset(record,0,56);
  std::memcpy(record+24,e,5);
  if(nested)absolute(t,reinterpret_cast<uintptr_t>(relay(0)));else {std::memcpy(t,owner.data(),6);absolute(t+6,reinterpret_cast<uintptr_t>(e+6));}
  absolute(r,reinterpret_cast<uintptr_t>(bulk.p+0x1acf0));
  *reinterpret_cast<uintptr_t*>(record)=reinterpret_cast<uintptr_t>(e);*reinterpret_cast<uintptr_t*>(record+8)=reinterpret_cast<uintptr_t>(r);*reinterpret_cast<uintptr_t*>(record+16)=reinterpret_cast<uintptr_t>(t);record[32]=6;record[36]=nested?1:3;
  if(!nested){record[41]=record[49]=2;record[42]=record[50]=6;}
  *reinterpret_cast<uintptr_t*>(bulk.p+0x27b120)=reinterpret_cast<uintptr_t>(record);*reinterpret_cast<unsigned*>(bulk.p+0x27b128)=1;*reinterpret_cast<unsigned*>(bulk.p+0x27b12c)=1;
  *bs()=reinterpret_cast<uintptr_t>(t);const auto disp=static_cast<int32_t>(r-(e+5));e[0]=0xe9;std::memcpy(e+1,&disp,4);
 }
 void installMenuOverBulk(){
  installBulk(false);auto*e=entry(0);auto*t=trampoline(0);auto*r=t+14;
  absolute(t,reinterpret_cast<uintptr_t>(br(false)));absolute(r,reinterpret_cast<uintptr_t>(foreign.p+0x65b90));
  *original_slot(0)=reinterpret_cast<uintptr_t>(t);auto*record=reinterpret_cast<uintptr_t*>(foreign.p+0x49d6c0+80);record[0]=reinterpret_cast<uintptr_t>(e);record[1]=reinterpret_cast<uintptr_t>(t);record[2]=reinterpret_cast<uintptr_t>(r);record[3]=1;record[4]=0x100000001ULL;
  const auto disp=static_cast<int32_t>(r-(e+5));e[0]=0xe9;std::memcpy(e+1,&disp,4);
 }
 void reset(){restore();*bs()=0;*reinterpret_cast<uintptr_t*>(bulk.p+0x27b120)=0;*reinterpret_cast<unsigned*>(bulk.p+0x27b12c)=0;}
};
template<class F>ItemGainCaptureTarget ResolveBoth(F fn,BulkFixture&f,unsigned*mask){
 if constexpr(std::is_invocable_v<F,const void*,const void*,unsigned*,const void*>)return fn(f.game.p,f.foreign.p,mask,f.bulk.p);
 else return fn(f.game.p,f.foreign.p,mask);
}
int main(int argc,char**argv){
 if(argc!=4)return 2;BulkFixture f(argv[1],argv[2],argv[3]);Check(f.good()&&f.bulk.p,"inert Menu/Bulk/game DATA maps allocated");if(!f.good()||!f.bulk.p)return 2;
 unsigned mask=0;const auto pristine=ResolveBoth(psm::items3446::ResolveWithBulk,f,&mask);Check(pristine.address&&mask==0,"pristine native owner accepted with both providers present");
 auto test=[&](const char*label){mask=0;auto got=ResolveBoth(psm::items3446::ResolveWithBulk,f,&mask);Check(got.address==pristine.address&&got.address,label);Check(mask==1,"known captured owner mask remains exact");Check(SameTarget(got,pristine),"all19 item ABI/address/layout fields preserved through exact chain");};
 f.installBulk(false);test("direct exact Bulk native owner accepted");f.reset();
 f.patch(0);f.installBulk(true);test("exact Bulk outer over Menu512 inner accepted");f.reset();
 f.installMenuOverBulk();test("exact Menu512 outer over Bulk inner accepted");f.reset();

 {
  namespace cc=psm::capture_compat;psm::v3446::Image image;Check(image.Init(f.game.p,f.game.size),"native chain mutation image initialized");
  const auto menu=cc::Context::From(f.foreign.p);const auto bulk=cc::BulkContext::From(f.bulk.p);Check(menu.valid&&bulk.valid,"both exact provider contracts accepted");
  auto setup=[&](unsigned route){f.reset();if(route==2)f.installBulk(false);if(route==3)f.installMenuOverBulk();if(route==4){f.patch(0);f.installBulk(true);}};
  for(unsigned route=2;route<=4;++route){
   setup(route);auto observe=[&](const cc::Reader&r=cc::Reader{}){cc::ChainSnapshot snap{};return cc::ObserveOwnerChain(image,psm::items3446::kOwner,0x22f1dc0,menu,bulk,true,route,snap,r);};
   Check(observe(),"each exact three-route capture is independently observable");
   auto bad=[&](unsigned char*at,const char*label){const auto save=*at;*at^=1;Check(!observe(),label);*at=save;};
   for(unsigned off:{0u,1u,5u,20u,114u})bad(f.entry(0)+off,"every native entry and whole suffix mutation is rejected");
   for(unsigned off:{0u,1u,6u,13u,14u,20u,27u})bad(f.bt()+off,"Bulk original bridge replay/jumps and relay are exact");
   if(route!=4)for(unsigned off:{28u,33u})bad(f.bt()+off,"direct Bulk relay destination is exact through its last byte");
   bad(reinterpret_cast<unsigned char*>(f.bs()),"Bulk original-pointer slot mutation rejected");
   for(unsigned off:{0u,8u,16u,24u,32u,36u,40u,48u})bad(f.rec()+off,"Bulk hook target/relay/original/backup/flags/instruction state remains exact");
   if(route!=4)for(unsigned off:{41u,42u,49u,50u})bad(f.rec()+off,"native old/new instruction-boundary arrays remain exact");
   if(route>=3){
    auto*mt=f.trampoline(0);for(unsigned off:{0u,1u,6u,13u,14u,20u,27u})bad(mt+off,"Menu bridge replay/jumps and relay remain exact in both nested orders");
    if(route==4)for(unsigned off:{28u,33u})bad(mt+off,"inner Menu native relay remains exact through its last byte");
    bad(reinterpret_cast<unsigned char*>(f.original_slot(0)),"Menu original-pointer slot mutation rejected in pair");
    for(unsigned off:{0u,8u,16u,24u,32u,36u})bad(f.foreign.p+0x49d710+off,"Menu live hook state record is exact in pair");
   }
   const auto savedOriginal=*f.bs();*f.bs()=~uintptr_t{0}-4;Check(!observe(),"overflowed Bulk original bridge pointer rejected");*f.bs()=savedOriginal;
   auto*table=reinterpret_cast<uintptr_t*>(f.bulk.p+0x27b120);const auto savedTable=*table;*table=~uintptr_t{0}-7;Check(!observe(),"overflowed dynamic Bulk record table rejected");*table=savedTable;
   auto*count=reinterpret_cast<unsigned*>(f.bulk.p+0x27b12c);auto*capacity=reinterpret_cast<unsigned*>(f.bulk.p+0x27b128);
   *count=0;Check(!observe(),"missing Bulk hook record rejected");*count=1;*capacity=0;Check(!observe(),"count over capacity rejected");*capacity=4097;Check(!observe(),"unbounded Bulk record table rejected");*capacity=1;
   std::memcpy(f.rec()+56,f.rec(),56);*count=*capacity=2;Check(!observe(),"duplicate records for same native target are rejected");*count=*capacity=1;
   const auto flags=f.rec()[32],ip=f.rec()[36];f.rec()[32]|=0xf8;f.rec()[36]|=0xf0;Check(observe(),"uninitialized high flag and instruction-count bits are ignored by typed ABI");f.rec()[32]=flags;f.rec()[36]=ip;
   const auto match=cc::FindOwner(image,psm::items3446::kOwner,menu,bulk);Check(match.rva&&match.route==route&&match.captured,"paired observations choose exact explicit route");
   *capacity=2;Check(observe(),"record-table growth may be individually valid");Check(!cc::StableOwner(image,psm::items3446::kOwner,match,menu,bulk),"record-table growth invalidates retained snapshot");*capacity=1;
   f.bt()[0]^=1;Check(!cc::StableOwner(image,psm::items3446::kOwner,match,menu,bulk),"later bridge drift invalidates retained proof");f.bt()[0]^=1;
   // A valid same-record table relocation between observations must fail snapshot equality.
   std::memcpy(f.blobs+8192,f.rec(),56);struct Torn {uintptr_t watched,alternate;unsigned reads=0;} torn{reinterpret_cast<uintptr_t>(table),reinterpret_cast<uintptr_t>(f.blobs+8192),0};
   cc::Reader moving{};moving.opaque=&torn;moving.copy=[](void*opaque,uintptr_t at,void*out,std::size_t n){auto&t=*static_cast<Torn*>(opaque);if(!cc::SafeCopy(at,out,n))return false;if(at==t.watched&&++t.reads>=2)std::memcpy(out,&t.alternate,8);return true;};
   Check(!cc::FindOwner(image,psm::items3446::kOwner,menu,bulk,moving).rva,"two valid relocated Bulk table snapshots refuse a torn capture");
   cc::Reader badRead{};badRead.copy=[](void*,uintptr_t,void*,std::size_t){return false;};Check(!observe(badRead),"unreadable chain proof rejected");
   cc::Reader badExec{};badExec.executable=[](void*,uintptr_t,std::size_t){return false;};Check(!observe(badExec),"nonprivate/nonexecutable chain bridge rejected");
   f.absolute(f.bt(),reinterpret_cast<uintptr_t>(f.blobs+3000));Check(!observe(),"unknown third/predecessor chain refused");setup(route);
   f.restore();Check(!cc::StableOwner(image,psm::items3446::kOwner,match,menu,bulk),"late unhook invalidates captured startup proof");
  }
  f.reset();
 }
 {
  namespace cc=psm::capture_compat;namespace b=cc::bulk0413;
  const psm::v3446::Profile* profiles[]={&b::kInstaller,&b::kWrapper,&b::kCallback,&b::kQueue,&b::kSafeRead,&b::kCreate,&b::kEnableLeaf,&b::kEnable,&b::kPatch,&b::kAllocWrapper,&b::kAllocator,&b::kFreeLeaf,&b::kExecutable,&b::kTrampoline,&b::kDecoder,&b::kRemove,&b::kInitialize,&b::kApplyQueued};
  const std::size_t rvas[]={0x1a900,0x31b0,0x1acf0,0x1a6c0,0x6a30,0x51ae0,0x51d60,0x51490,0x515b0,0x52410,0x524c0,0x52440,0x52720,0x52760,0x52b60,0x51e20,0x51d70,0x51930};
  for(unsigned i=0;i<std::size(profiles);++i){const auto at=rvas[i]+profiles[i]->size-1;f.bulk.p[at]^=1;Check(!cc::BulkContext::From(f.bulk.p).valid,"every complete Bulk provider owner is byte-exact through final byte");f.bulk.p[at]^=1;
   auto*nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(f.bulk.p+reinterpret_cast<IMAGE_DOS_HEADER*>(f.bulk.p)->e_lfanew);const auto d=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
   bool changed=false;for(unsigned pos=d.VirtualAddress;pos<d.VirtualAddress+d.Size;pos+=12){auto*row=reinterpret_cast<RUNTIME_FUNCTION*>(f.bulk.p+pos);if(row->BeginAddress!=rvas[i])continue;const auto save=row->EndAddress;row->EndAddress^=1;Check(!cc::BulkContext::From(f.bulk.p).valid,"every Bulk nonleaf requires exact full-span pdata");row->EndAddress=save;changed=true;break;}
   if(!changed){auto*row=reinterpret_cast<RUNTIME_FUNCTION*>(f.bulk.p+d.VirtualAddress);const auto save=*row;row->BeginAddress=static_cast<DWORD>(rvas[i]);row->EndAddress=static_cast<DWORD>(rvas[i]+1);Check(!cc::BulkContext::From(f.bulk.p).valid,"Bulk leaf requires zero overlapping pdata rows");*row=save;}
  }
  for(unsigned off:{0u,264u,528u}){f.bulk.p[0x227ee0+off]^=1;Check(!cc::BulkContext::From(f.bulk.p).valid,"writable decoder lookup DATA is pinned");f.bulk.p[0x227ee0+off]^=1;}
  const auto bst= reinterpret_cast<IMAGE_DOS_HEADER*>(f.bulk.p)->e_lfanew+8;f.bulk.p[bst]^=1;
  mask=0;Check(ResolveBoth(psm::items3446::ResolveWithBulk,f,&mask).address==pristine.address&&mask==0,"unsupported Bulk present with pristine native owner remains acceptable");
  f.installBulk(false);Check(!ResolveBoth(psm::items3446::ResolveWithBulk,f,&mask).address,"unsupported Bulk with changed native owner is refused");f.reset();f.bulk.p[bst]^=1;
  const auto mst=reinterpret_cast<IMAGE_DOS_HEADER*>(f.foreign.p)->e_lfanew+8;f.foreign.p[mst]^=1;
  Check(ResolveBoth(psm::items3446::ResolveWithBulk,f,&mask).address==pristine.address,"unsupported Menu present with pristine native owner remains acceptable");
  f.patch(0);Check(!ResolveBoth(psm::items3446::ResolveWithBulk,f,&mask).address,"unsupported Menu with changed native owner is refused");f.reset();f.foreign.p[mst]^=1;
 }
 std::printf("item-capture-bulk-b: %u checks, %u failures; no foreign/game bytes executed\n",checks,failures);return failures?1:0;
}
