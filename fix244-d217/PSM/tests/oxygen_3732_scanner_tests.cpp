#include <Windows.h>
#include "oxygen_scanner.h"
#include <vector>
#include <array>
#include <fstream>
#include <iterator>
#include <cstdio>
#include <cstring>
int main(int argc,char** argv){
 if(argc!=2)return 2;
 std::ifstream f(argv[1],std::ios::binary);std::vector<unsigned char> raw((std::istreambuf_iterator<char>(f)),{});
 if(raw.size()<4096)return 2;
 const auto*d=reinterpret_cast<const IMAGE_DOS_HEADER*>(raw.data());
 const auto*n=reinterpret_cast<const IMAGE_NT_HEADERS64*>(raw.data()+d->e_lfanew);
 if(n->OptionalHeader.SizeOfImage>512*1024*1024)return 2;
 std::vector<unsigned char> image(n->OptionalHeader.SizeOfImage);
 std::memcpy(image.data(),raw.data(),n->OptionalHeader.SizeOfHeaders);
 for(unsigned j=0;j<n->FileHeader.NumberOfSections;++j){const auto&s=IMAGE_FIRST_SECTION(n)[j];
  if(s.PointerToRawData>raw.size()||s.SizeOfRawData>raw.size()-s.PointerToRawData||
     s.VirtualAddress>image.size()||s.SizeOfRawData>image.size()-s.VirtualAddress)return 2;
  std::memcpy(image.data()+s.VirtualAddress,raw.data()+s.PointerToRawData,s.SizeOfRawData);}
 int checks=0,failures=0;auto check=[&](bool ok,const char*label){++checks;if(!ok){++failures;std::printf("FAIL %s\n",label);}};
 const auto base=reinterpret_cast<uintptr_t>(image.data());
 auto scan=[&](){return psm::oxygen::Scan(image.data(),image.size());};
 const auto sites=scan();
 check(sites.gauge_sample==base+0x121a07b,"typed HUD Breath sample converges after call and bypass");
 for (const auto at : {0x121a066u,0x121a068u,0x121a07bu,0x121a08bu}) {
  image[at]^=1;
  const auto unsafe=scan();
  check(!unsafe.gauge_sample && bool(unsafe.catalog),"modified HUD witness cannot install a sample");
  image[at]^=1;
 }
 check(sites.client_control_slot==base+0x724B350,"native local-control global resolves from its executable witness");
 image[0x674B54]^=1;
 const auto missing_control=scan();
 check(!missing_control.client_control_slot && missing_control.gauge_sample==sites.gauge_sample,
       "changed control witness leaves only the read-only gauge path");
 image[0x674B54]^=1;
 image[0x674B2F]^=1;
 check(!scan().client_control_slot,"unaligned controller slot is rejected");
 image[0x674B2F]^=1;
 check(sites.server_actor_vtable==base+0x5F65F38,"unique native normal server actor RTTI");
 check(sites.server_status_vtable==base+0x5F687B0,"unique server status component owns the minimum passive rule");
 for (const auto at : {0x70b97a8u, 0x5f68ae0u, 0x2c3379au, 0x1938643u}) {
  image[at]^=1;
  const auto refused=scan();
  check(!refused.server_status_vtable && refused.client_control_slot==sites.client_control_slot,
        "invalid server minimum evidence preserves the independent client protection");
  image[at]^=1;
 }
 auto poison=[&](std::size_t at) {
  image[at]^=1;
  const auto result=scan();
  check(!result.server_actor_vtable && bool(result.catalog),"invalid server class evidence leaves client Breath catalog intact");
  image[at]^=1;
 };
 poison(0x70B9418); // type name
 poison(0x63E851C); // complete object locator type reference
 poison(0x5F65F30); // vtable[-1] locator pointer
 image[0x5F65F3F]^=0x40; // non-game first method
 const auto broken_method=scan();
 check(!broken_method.server_actor_vtable && bool(broken_method.catalog),"vtable methods must be in executable game regions");
 image[0x5F65F3F]^=0x40;
 check(sites.breath_before_call==base+0x121a06e&&sites.stats_after_lookup==base+0x194ec3b,
       "breath gauge and native entry graph linked");
 check(sites.catalog.actors == base + 0x724c4b0 && sites.catalog.groups == base + 0x7254000 &&
       sites.catalog.statuses == base + 0x724c568, "current exact getters resolve all native manager slots");
 for (const auto at : {0x3db748u, 0x725728u, 0x3dae78u}) {
  image[at] ^= 1;
  const auto damaged = scan();
  check(!damaged.catalog && damaged.breath_before_call == sites.breath_before_call,
        "a changed catalog getter leaves only the verified gauge path");
  image[at] ^= 1;
 }
 image[0x1955365] ^= 1;
 check(bool(scan().catalog) && !scan().breath_before_call,
       "a changed curve reader keeps the independent catalog path");
 image[0x1955365] ^= 1;
 for (const auto at : {0x186c99u,0x186caau,0x121a06eu,0x1218519u,0x122080au,0x1054c65u,0x1054cb2u,
                       0x194e6dfu,0x194e6f2u,0x194e73du,0x194ec39u,0x1955365u,0x5af21c0u}) {
  image[at]^=1;
  check(!scan().breath_before_call,"damaged native breath proof refuses gauge");
  image[at]^=1;
 }
 auto*nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(image.data()+d->e_lfanew);
 nt->FileHeader.TimeDateStamp^=1;check(bool(scan()),"validated oxygen layout does not depend on PE timestamp");nt->FileHeader.TimeDateStamp^=1;
 auto*sections=IMAGE_FIRST_SECTION(nt);
 const auto flags=sections[0].Characteristics;sections[0].Characteristics&=~IMAGE_SCN_MEM_EXECUTE;
 check(!scan(),"binding requires executable section");sections[0].Characteristics=flags;
 // A second complete breath evaluator with identical bytes is ambiguous even before link checks.
 const std::size_t duplicate=0x4000, source=0x1054bd0,length=0x1BE;
 std::vector<unsigned char> saved(image.begin()+duplicate,image.begin()+duplicate+length);
 std::memcpy(image.data()+duplicate,image.data()+source,length);
 {const auto ambiguous=scan();check(!ambiguous.breath_before_call,"ambiguous gauge rejected");}
 std::memcpy(image.data()+duplicate,saved.data(),length);
 // Relocate all three registration references with the leaf.
 const std::size_t reg_duplicate=0x5000,reg_source=0x186c90,reg_length=31;
 std::vector<unsigned char> reg_saved(image.begin()+reg_duplicate,image.begin()+reg_duplicate+reg_length);
 std::memcpy(image.data()+reg_duplicate,image.data()+reg_source,reg_length);
 for (const std::size_t field : {9u, 22u, 27u}) {
  int32_t displacement=0;
  std::memcpy(&displacement,image.data()+reg_source+field,sizeof(displacement));
  displacement+=static_cast<int32_t>(reg_source-reg_duplicate);
  std::memcpy(image.data()+reg_duplicate+field,&displacement,sizeof(displacement));
 }
 check(!scan().breath_before_call,"two registrations linked to the same token are refused");
 image[reg_source]^=1;
 check(scan().breath_before_call==sites.breath_before_call,"relocated registration keeps the same linked breath authority");
 image[reg_source]^=1;
 std::memcpy(image.data()+reg_duplicate,reg_saved.data(),reg_length);
 check(bool(scan()),"original image restored and accepted");
 std::printf("oxygen scanner checks=%d failures=%d\n",checks,failures);return failures?1:0;
}
