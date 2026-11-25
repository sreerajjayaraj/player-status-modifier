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
 check(sites.breath_before_call==base+0x12184EE&&sites.stats_after_lookup==base+0x194CDAB,
       "breath gauge and native entry graph linked");
 check(sites.catalog.actors == base + 0x7246310 && sites.catalog.groups == base + 0x724DE20 &&
       sites.catalog.statuses == base + 0x72463C8, "current exact getters resolve all native manager slots");
 for (const auto at : {0x3DB338u, 0x725398u, 0x3DAA68u}) {
  image[at] ^= 1;
  const auto damaged = scan();
  check(!damaged.catalog && damaged.breath_before_call == sites.breath_before_call,
        "a changed catalog getter leaves only the verified gauge path");
  image[at] ^= 1;
 }
 image[0x19534D5] ^= 1;
 check(bool(scan().catalog) && !scan().breath_before_call,
       "a changed curve reader keeps the independent catalog path");
 image[0x19534D5] ^= 1;
 for (const auto at : {0x186979u,0x18698Au,0x12184EEu,0x1216999u,0x121EC8Au,0x10531B5u,0x1053202u,
                       0x194C84Fu,0x194C862u,0x194C8ADu,0x194CDA9u,0x19534D5u,0x5AED2A8u}) {
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
 const std::size_t duplicate=0x4000, source=0x1053120,length=0x1BE;
 std::vector<unsigned char> saved(image.begin()+duplicate,image.begin()+duplicate+length);
 std::memcpy(image.data()+duplicate,image.data()+source,length);
 {const auto ambiguous=scan();check(!ambiguous.breath_before_call,"ambiguous gauge rejected");}
 std::memcpy(image.data()+duplicate,saved.data(),length);
 // Relocate all three registration references with the leaf.
 const std::size_t reg_duplicate=0x5000,reg_source=0x186970,reg_length=31;
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
