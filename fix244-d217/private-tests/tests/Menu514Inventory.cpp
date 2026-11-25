#include <Windows.h>
#include "menu_item_gain_512.h"
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
}
int main(int argc,char**argv){
 if(argc!=3)return 2;Map game(argv[1]),menu(argv[2]);Check(game.p&&menu.p,"both current PE images loaded as nonexecuting DATA");if(!game.p||!menu.p)return 2;
 namespace m=psm::menu_gain512;
 auto resolve=[&]{return m::Resolve(game.p,menu.p);};
 const std::pair<unsigned,unsigned> slots[]={{0x4d0088,0x25fed70},{0x4d0090,0x25fcdb0},{0x4d0098,0x22f6680},{0x4d00a0,0x3d8090}};
 for(auto[slot,rva]:slots)*reinterpret_cast<uintptr_t*>(menu.p+slot)=reinterpret_cast<uintptr_t>(game.p)+rva;
 auto target=resolve();Check(target.state==m::State::Ready&&target.worker==reinterpret_cast<uintptr_t>(menu.p)+0x77c30&&target.caller==reinterpret_cast<uintptr_t>(menu.p)+0x72bcf,"exact current full ABI resolves typed worker and queue caller");
 Check(m::Resolve(game.p,nullptr).state==m::State::Absent,"absent producer is distinct from unsupported");
 auto*nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(menu.p+reinterpret_cast<IMAGE_DOS_HEADER*>(menu.p)->e_lfanew);const auto stamp=nt->FileHeader.TimeDateStamp;nt->FileHeader.TimeDateStamp^=1;Check(resolve().state!=m::State::Ready,"unknown provider identity refused");nt->FileHeader.TimeDateStamp=stamp;
 const auto workerHead=menu.p[0x77c30];menu.p[0x77c30]=0xe9;
 Check(resolve().state!=m::State::Ready,"already-hooked unknown worker is refused");
 menu.p[0x77c30]=workerHead;
 for(const auto at:{0x77c30+100,0x72870+100,0x776e0+100,0x6f930+100,0x67610+100}){
  menu.p[at]^=1;Check(resolve().state!=m::State::Ready,"worker and typed helper drift refused");menu.p[at]^=1;
 }
 for(auto[slot,rva]:slots){auto*p=reinterpret_cast<uintptr_t*>(menu.p+slot);const auto save=*p;*p=0;Check(resolve().state!=m::State::Ready,"producer native binding not yet ready cannot publish");*p=save+1;Check(resolve().state!=m::State::Ready,"wrong native typed binding refused");*p=save;}
 const auto save=game.p[0x22f6680+10];game.p[0x22f6680+10]^=1;Check(resolve().state!=m::State::Ready,"native commit ABI body drift refused");game.p[0x22f6680+10]=save;
 Check(resolve().state==m::State::Ready,"restored exact producer is usable without changing foreign code");

 auto* binding=reinterpret_cast<uintptr_t*>(menu.p+0x4d0090);
 const auto direct_capacity=*binding;
 const auto typed_capacity=reinterpret_cast<uintptr_t>(menu.p)+0x67610;
 *binding=typed_capacity;
 auto unavailable=resolve();
 Check(unavailable.state==m::State::NotReady&&unavailable.probe.rva==0x4d0090&&
       !std::strcmp(unavailable.probe.reason,"capacity-wrapper-unbound"),
       "a wrapper without its exact live capture remains pending");
 *binding=direct_capacity;

 {
  // The producer may arrive after PSM's own receipt hook is already installed.
  auto* bridge=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));Check(bridge!=nullptr,"private own receipt proof storage allocated, never executed");
  std::array<unsigned char,17> original{};std::memcpy(original.data(),game.p+0xd6ff310,17);
  const auto entry=reinterpret_cast<uintptr_t>(game.p)+0xd6ff310;const auto callback=reinterpret_cast<uintptr_t>(game.p)+0x4000;
  auto absolute=[](unsigned char*at,uintptr_t dst){const unsigned char op[6]={0xff,0x25,0,0,0,0};std::memcpy(at,op,6);std::memcpy(at+6,&dst,8);};
  if(bridge){
   std::memcpy(bridge,original.data(),17);absolute(bridge+17,entry+17);absolute(game.p+0xd6ff310,callback);std::memset(game.p+0xd6ff310+14,0x90,3);
   m::OwnedReceipt own{entry,callback,reinterpret_cast<uintptr_t>(bridge),original.data(),17,true};
   Check(resolve().state!=m::State::Ready,"late producer cannot accept unowned changed native receipt body");
   Check(m::Resolve(game.p,menu.p,own).state==m::State::Ready,"exact owned receipt allows native-hook-first late producer order");
   original[0]^=1;Check(m::Resolve(game.p,menu.p,own).state!=m::State::Ready,"changed owned original bytes refuse late native proof");original[0]^=1;
   bridge[0]^=1;Check(m::Resolve(game.p,menu.p,own).state!=m::State::Ready,"changed retained receipt trampoline refuses late proof");bridge[0]^=1;
   game.p[0xd6ff310+6]^=1;Check(m::Resolve(game.p,menu.p,own).state!=m::State::Ready,"unknown live receipt destination is not accepted as own");game.p[0xd6ff310+6]^=1;
   game.p[0xd6ff310+100]^=1;Check(m::Resolve(game.p,menu.p,own).state!=m::State::Ready,"entire native receipt suffix remains exact after own hook");game.p[0xd6ff310+100]^=1;
   own.enabled=false;Check(m::Resolve(game.p,menu.p,own).state!=m::State::Ready,"changed live receipt cannot use an inactive own descriptor");
   std::memcpy(game.p+0xd6ff310,original.data(),17);VirtualFree(bridge,0,MEM_RELEASE);
  }
 }

 {
  // Synthetic executable bridge DATA is never invoked. The actual current
  // prolog/continuation bytes are used to prove the typed old observer shape.
  unsigned char* blobs=nullptr;auto cursor=reinterpret_cast<uintptr_t>(game.p);const auto limit=cursor+0x60000000;
  for(unsigned i=0;i<128&&!blobs&&cursor<limit;++i){MEMORY_BASIC_INFORMATION region{};if(!VirtualQuery(reinterpret_cast<void*>(cursor),&region,sizeof(region)))break;
   const auto end=reinterpret_cast<uintptr_t>(region.BaseAddress)+region.RegionSize;if(end<=cursor)break;
   if(region.State==MEM_FREE){const auto at=(cursor+0xffff)&~uintptr_t{0xffff};if(at<=end&&0x10000<=end-at)blobs=static_cast<unsigned char*>(VirtualAlloc(reinterpret_cast<void*>(at),0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));}cursor=end;
  }
  Check(blobs!=nullptr,"near private bridge DATA allocated for exact E9 ownership");
  if(blobs){
   auto absolute=[](unsigned char*at,uintptr_t dst){const unsigned char op[6]={0xff,0x25,0,0,0,0};std::memcpy(at,op,6);std::memcpy(at+6,&dst,8);};
   auto relative=[](unsigned char*at,uintptr_t dst){const auto delta=std::int64_t(dst)-std::int64_t(reinterpret_cast<uintptr_t>(at)+5);if(delta<INT32_MIN||delta>INT32_MAX)return false;at[0]=0xe9;const auto d=static_cast<int32_t>(delta);std::memcpy(at+1,&d,4);return true;};
   std::array<unsigned char,5> receipt{};std::memcpy(receipt.data(),game.p+0xd6ff310,5);auto*rt=blobs;
   const auto entry=reinterpret_cast<uintptr_t>(game.p)+0xd6ff310;const auto callback=reinterpret_cast<uintptr_t>(game.p)+0x4000;
   std::memcpy(rt,receipt.data(),5);Check(relative(rt+5,entry+5)&&relative(game.p+0xd6ff310,reinterpret_cast<uintptr_t>(rt)+10),"E9 owned entry and exact native continuation are representable");absolute(rt+10,callback);
   m::OwnedReceipt owned{entry,callback,reinterpret_cast<uintptr_t>(rt),receipt.data(),5,true};
   Check(m::Resolve(game.p,menu.p,owned).state==m::State::Ready,"native E9 receipt hook may precede late producer arrival");
   rt[10+6]^=1;Check(m::Resolve(game.p,menu.p,owned).state!=m::State::Ready,"E9 retained callback destination mutation refused");rt[16]^=1;
   std::memcpy(game.p+0xd6ff310,receipt.data(),5);
   std::array<unsigned char,5> capacity{};std::memcpy(capacity.data(),game.p+0x25fcdb0,5);auto*ct=blobs+256;const auto capentry=reinterpret_cast<uintptr_t>(game.p)+0x25fcdb0;
   std::memcpy(ct,capacity.data(),5);absolute(ct+5,capentry+5);absolute(ct+19,reinterpret_cast<uintptr_t>(menu.p)+0x67290);Check(relative(game.p+0x25fcdb0,reinterpret_cast<uintptr_t>(ct)+19),"known Menu capacity relay lies within rel32");
   *reinterpret_cast<uintptr_t*>(menu.p+0x4cc1e0)=reinterpret_cast<uintptr_t>(ct);auto*record=reinterpret_cast<uintptr_t*>(menu.p+0x4cc1d8);
   record[0]=capentry;record[1]=reinterpret_cast<uintptr_t>(ct);record[2]=reinterpret_cast<uintptr_t>(ct)+19;record[3]=1;record[4]=0x100000001ULL;
   Check(resolve().state==m::State::Ready,"known exact Menu capacity observer remains supported by new adapter");
   unsigned mask=0;
   auto ordinary=psm::items3446::Resolve(game.p,menu.p,&mask);
   Check(ordinary.address==entry&&mask==2,"ordinary item group accepts the exact current Menu capacity detour");
   std::array<unsigned char,6> owner{};
   auto* ownerEntry=game.p+0x22f1dc0;
   auto* ot=blobs+512;
   const auto ownerAddress=reinterpret_cast<uintptr_t>(ownerEntry);
   std::memcpy(owner.data(),ownerEntry,owner.size());
   std::memcpy(ot,owner.data(),owner.size());
   absolute(ot+6,ownerAddress+6);
   absolute(ot+20,reinterpret_cast<uintptr_t>(menu.p)+0x673b0);
   Check(relative(ownerEntry,reinterpret_cast<uintptr_t>(ot)+20),"current Menu owner relay lies within rel32");
   auto* ownerRecord=reinterpret_cast<uintptr_t*>(menu.p+0x4cc200);
   ownerRecord[0]=ownerAddress;ownerRecord[1]=reinterpret_cast<uintptr_t>(ot);
   ownerRecord[2]=reinterpret_cast<uintptr_t>(ot)+20;ownerRecord[3]=1;ownerRecord[4]=0x100000001ULL;
   ordinary=psm::items3446::Resolve(game.p,menu.p,&mask);
   Check(ordinary.address==entry&&mask==3,"ordinary item group accepts both exact current Menu detours");
   ownerRecord[4]^=1;
   Check(!psm::items3446::Resolve(game.p,menu.p,&mask).address,"ordinary item group refuses changed owner lease");
   std::memcpy(ownerEntry,owner.data(),owner.size());std::memset(ownerRecord,0,40);
   // The exact final Menu initializer publishes the exported CapacityCall
   // wrapper, not the native entry it logs alongside the constructor.
   *binding=0;
   auto waiting=resolve();
   Check(waiting.state==m::State::NotReady&&waiting.probe.rva==0x4d0090,
         "capture hook before typed capacity slot publication remains pending");
   *binding=typed_capacity;
   auto ready=resolve();
   Check(ready.state==m::State::Ready&&ready.capacity_wrapper,
         "live Menu publishes exported typed capacity wrapper and becomes ready");
   *binding=reinterpret_cast<uintptr_t>(ct);
   Check(resolve().state==m::State::Unsupported,"bare original trampoline is not the typed wrapper");
   *binding=reinterpret_cast<uintptr_t>(menu.p)+0x67290;
   Check(resolve().state==m::State::Unsupported,"Menu capture detour is not the typed wrapper");
   *binding=typed_capacity;
   auto* descriptor=reinterpret_cast<uintptr_t*>(menu.p+0x30de58);
   const auto old_descriptor=*descriptor;
   *descriptor=typed_capacity+1;
   Check(resolve().state==m::State::Unsupported,"mutated relocated capture descriptor refuses wrapper");
   *descriptor=old_descriptor;
   auto* commit_binding=reinterpret_cast<uintptr_t*>(menu.p+0x4d0098);
   const auto old_commit=*commit_binding;
   *commit_binding=typed_capacity;
   auto bad_commit=resolve();
   Check(bad_commit.state==m::State::Unsupported&&bad_commit.probe.rva==0x4d0098&&
         !std::strcmp(bad_commit.probe.reason,"binding-target"),"wrapper is accepted only in the capacity slot");
   *commit_binding=old_commit;
   ct[0]^=1;Check(resolve().state!=m::State::Ready,"unknown capacity replay refuses startup publication");ct[0]^=1;
   record[3]=0;Check(resolve().state==m::State::NotReady,"incomplete known capacity publication waits within bounded startup");record[3]=1;
   *binding=direct_capacity;
   std::memcpy(game.p+0x25fcdb0,capacity.data(),5);*reinterpret_cast<uintptr_t*>(menu.p+0x4cc1e0)=0;VirtualFree(blobs,0,MEM_RELEASE);
  }
 }
 {
  m::StartupRetry retry{};retry.Begin(100);
  Check(retry.Due(100)&&!retry.Due(100)&&!retry.Due(1099)&&retry.Due(1100),"startup producer checks are at most once per second");
  Check(!retry.Due(30100)&&!retry.active&&!retry.Due(999999),"startup is bounded30s and cannot retry later without a new start");
  retry.Begin(500);Check(retry.Due(500),"fresh owned startup admits a first attempt");retry.Stop();Check(!retry.Due(1500),"ready/refused/shutdown stop prevents later producer polling");
 }
 std::printf("menu-gain-resolver: %u checks, %u failures; PE DATA never executed\n",checks,failures);return failures?1:0;
}

