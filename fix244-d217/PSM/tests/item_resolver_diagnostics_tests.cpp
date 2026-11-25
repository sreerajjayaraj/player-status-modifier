#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "item_3446.h"
#include <cstdio>
#include <cstdarg>
#include <vector>
#include <fstream>
#include <iterator>
#include <array>
void Log(const char*f,...){va_list a;va_start(a,f);std::vprintf(f,a);std::puts("");va_end(a);}
unsigned checks=0,failures=0;void Check(bool b,const char*n){++checks;if(!b){++failures;std::printf("FAIL %s\n",n);}}
struct Map{unsigned char*p=nullptr;size_t size=0;Map(const char*path){std::ifstream in(path,std::ios::binary);std::vector<char>b((std::istreambuf_iterator<char>(in)),{});if(b.size()<4096)return;auto*d=reinterpret_cast<const IMAGE_DOS_HEADER*>(b.data());auto*n=reinterpret_cast<const IMAGE_NT_HEADERS64*>(b.data()+d->e_lfanew);size=n->OptionalHeader.SizeOfImage;p=static_cast<unsigned char*>(VirtualAlloc(nullptr,size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));if(!p)return;memcpy(p,b.data(),n->OptionalHeader.SizeOfHeaders);auto*s=IMAGE_FIRST_SECTION(n);for(unsigned i=0;i<n->FileHeader.NumberOfSections;++i)memcpy(p+s[i].VirtualAddress,b.data()+s[i].PointerToRawData,s[i].SizeOfRawData);
 const auto reloc=n->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
 const auto delta=reinterpret_cast<uintptr_t>(p)-n->OptionalHeader.ImageBase;
 for(size_t at=reloc.VirtualAddress;reloc.Size&&at<reloc.VirtualAddress+reloc.Size;){auto*block=reinterpret_cast<IMAGE_BASE_RELOCATION*>(p+at);if(block->SizeOfBlock<8)break;auto*fix=reinterpret_cast<uint16_t*>(block+1);for(unsigned j=0;j<(block->SizeOfBlock-8)/2;++j)if((fix[j]>>12)==IMAGE_REL_BASED_DIR64)*reinterpret_cast<uintptr_t*>(p+block->VirtualAddress+(fix[j]&0xFFF))+=delta;at+=block->SizeOfBlock;}}~Map(){if(p)VirtualFree(p,0,MEM_RELEASE);}};
int main(int argc,char**argv){
 if(argc!=3)return 2;Map game(argv[1]),observer(argv[2]);Check(game.p&&observer.p,"owned inert native/current-observer images map as nonexecutable data");if(!game.p||!observer.p)return 2;
using namespace psm::items3446;ResolveTrace trace{};unsigned captures=0;auto pristine=ResolveDetailed(game.p,observer.p,&captures,&trace);
 std::printf("TRACE stage=%u missing=%X call=%llX expected=%llX actual=%llX observer=%d\n",unsigned(trace.stage),trace.missing,static_cast<unsigned long long>(trace.failed_call),static_cast<unsigned long long>(trace.expected_target),static_cast<unsigned long long>(trace.observed_target),trace.observer_supported);
 Check(pristine.address&&trace.stage==ResolveStage::Complete&&!trace.missing&&!captures,"exact untouched native group succeeds with exact new observer loaded as data");
 if(!pristine.address)return 1;
 Check(trace.observer_present&&trace.observer_supported&&trace.observer_stamp==0xFC951675&&trace.observer_size==0x920000,"new observer identity is recorded with exact supported profiles; unknown captures still refuse");
 std::array<unsigned char,32> owner{},capacity{};memcpy(owner.data(),game.p+0x22F1DC0,owner.size());memcpy(capacity.data(),game.p+0x25FCDB0,capacity.size());
 game.p[0x22F1DC0]=0xe9;game.p[0x25FCDB0]=0xe9;auto bad=ResolveDetailed(game.p,observer.p,&captures,&trace);
 Check(!bad.address&&trace.stage==ResolveStage::RequiredProfiles&&(trace.missing&3)==3,"unsupported mutated owner/capacity hooks are rejected with exact missing-member bits");memcpy(game.p+0x22F1DC0,owner.data(),owner.size());memcpy(game.p+0x25FCDB0,capacity.data(),capacity.size());
 game.p[0x22F1DC0+20]^=1;bad=ResolveDetailed(game.p,observer.p,&captures,&trace);Check(!bad.address&&trace.missing==1,"native owner body drift is named separately from legacy caller fallback");game.p[0x22F1DC0+20]^=1;
 // Conservative32B patch spans cover every d179-before-items installed site
 // in the fresh log, including both ship observer boundaries. No code runs.
 const size_t sites[]={0x2B64261,0x18EDDBE,0xB619BDB,0x18E7679,0xB61BC53,0x18ECF9E,0x11E3D8C,0x10153EB,0xB619C45,0x11DE57C,0x11DE81D,0x11DE529,0x18E7192,0x11DEB3E,0x18E6D80};
 psm::v3446::Image view;Check(view.Init(game.p,game.size),"native owner/profile overlap image initializes");ResolveDetailed(game.p,nullptr,nullptr,&trace);
 const psm::v3446::Profile*profiles[]={&kOwner,&kBorrowCapacityPlanner,&kReceipt,&kPush,&kPlan,&kStandalone,&kMetadata,&kCapacity,&kMerge,&kCopy,&kRows,&kSourceInit,&kSourcePlan,&kSourceGroup,&kSourceConsumer,&kPop,&kSqlGate,&kMapper,&kSourceRemove,&kDestinationGroup,&kBorrowConstructor,&kBorrowAppend};
 for(const auto site:sites){uint32_t overlap=0;for(size_t i=0;i<22;++i){const auto start=trace.profiles[i];if(site<start+profiles[i]->size&&start<site+32)overlap|=uint32_t{1}<<i;}std::printf("self-hook siteRVA=%llX span=32 item-profile-overlap=%06X\n",static_cast<unsigned long long>(site),overlap);Check(!overlap,"observed d179 own hook span is disjoint from all22 required item profiles");std::array<unsigned char,32> bytes{};memcpy(bytes.data(),game.p+site,bytes.size());memset(game.p+site,0xcc,bytes.size());ResolveTrace after{};const auto still=ResolveDetailed(game.p,observer.p,&captures,&after);Check(still.address==pristine.address&&after.stage==ResolveStage::Complete,"actual primary resolver remains valid after each synthetic own hook patch");memcpy(game.p+site,bytes.data(),bytes.size());}
 auto copy=std::vector<unsigned char>(observer.p,observer.p+observer.size);auto*d=reinterpret_cast<const IMAGE_DOS_HEADER*>(observer.p);observer.p[d->e_lfanew+8]^=1;bad=ResolveDetailed(game.p,observer.p,&captures,&trace);Check(bad.address==pristine.address,"unknown observer fingerprint alone cannot disable an independently exact native group");memcpy(observer.p,copy.data(),copy.size());
 std::printf("primary item resolver diagnostics: %u checks, %u failures; native/foreign bytes never executed\n",checks,failures);return failures?1:0;
}
