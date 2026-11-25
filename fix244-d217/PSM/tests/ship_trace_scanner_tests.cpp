#include "ship_trace_scanner.h"
#include <Windows.h>
#include <fstream>
#include <vector>
#include <iterator>
#include <cstring>
#include <iostream>
int main(int argc,char**argv){if(argc!=2)return 2;std::ifstream f(argv[1],std::ios::binary);std::vector<char>b((std::istreambuf_iterator<char>(f)),{});if(b.size()<4096)return 2;auto*d=reinterpret_cast<IMAGE_DOS_HEADER*>(b.data());auto*n=reinterpret_cast<IMAGE_NT_HEADERS64*>(b.data()+d->e_lfanew);std::vector<unsigned char>m(n->OptionalHeader.SizeOfImage);std::memcpy(m.data(),b.data(),n->OptionalHeader.SizeOfHeaders);auto*s=IMAGE_FIRST_SECTION(n);for(unsigned j=0;j<n->FileHeader.NumberOfSections;++j)std::memcpy(m.data()+s[j].VirtualAddress,b.data()+s[j].PointerToRawData,s[j].SizeOfRawData);int checks=0;auto check=[&](bool ok,const char*name){++checks;if(!ok){std::cerr<<"FAIL "<<name<<'\n';std::exit(1);}};auto scan=[&]{return ShipTraceVehicleSite(m.data(),m.size(),0x1217ED9);};
check(scan()==0x1217F2C,"real current full-owner typed VehicleInfo observer contract");
for(size_t a:{size_t{0x1217F27},size_t{0x12181CD},size_t{0x3DB3B8},size_t{0x1795172},size_t{0x198F8A9},size_t{0x198F8DA}}){auto old=m[a];m[a]^=1;check(!scan(),"changed native owner/field/call rejects extra observer");m[a]=old;}
check(!ShipTraceVehicleSite(m.data(),m.size(),0x1217ED8),"wrong mid-instruction capture rejected");auto*mn=reinterpret_cast<IMAGE_NT_HEADERS64*>(m.data()+d->e_lfanew);auto ts=mn->FileHeader.TimeDateStamp;mn->FileHeader.TimeDateStamp^=1;check(bool(scan()),"validated ship route does not depend on PE timestamp");mn->FileHeader.TimeDateStamp=ts;
auto ex=mn->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];auto*rows=reinterpret_cast<RUNTIME_FUNCTION*>(m.data()+ex.VirtualAddress);for(unsigned j=0;j<ex.Size/sizeof(RUNTIME_FUNCTION);++j)if(rows[j].BeginAddress==0x198F860){auto end=rows[j].EndAddress;--rows[j].EndAddress;check(!scan(),"whole owner end mismatch rejects observer");rows[j].EndAddress=end;break;}
const std::array<std::size_t,3> expectedManagers = {0x7246310, 0x7246398, 0x7246308};
auto catalogScan = [&] { return ShipTraceCatalogGlobals(m.data(), m.size(), 0x1217ED9); };
check(catalogScan() == expectedManagers, "3663 native character, gimmick and vehicle manager slots");
for (size_t at : {0x3DB338u, 0x3DAC78u, 0x3DB3B8u}) {
 m[at] ^= 1;
 check(catalogScan() == std::array<std::size_t,3>{}, "a changed getter refuses all metadata authority");
 m[at] ^= 1;
}
check(catalogScan() == expectedManagers, "restored native metadata ownership");
const auto imageBase = static_cast<uintptr_t>(n->OptionalHeader.ImageBase);
const auto catalog = catalogScan();
check(RelocateShipTraceCatalogGlobals(imageBase, catalog) ==
      std::array<uintptr_t,3>{imageBase + expectedManagers[0], imageBase + expectedManagers[1],
                             imageBase + expectedManagers[2]},
      "validated current catalog relocates all three manager slots");
check(RelocateShipTraceCatalogGlobals(imageBase, {}) == std::array<uintptr_t,3>{},
      "refused catalog remains zero after relocation");
for (std::size_t index = 0; index < catalog.size(); ++index) {
 auto incomplete = catalog;
 incomplete[index] = 0;
 check(RelocateShipTraceCatalogGlobals(imageBase, incomplete) == std::array<uintptr_t,3>{},
       "incomplete catalog cannot arm healing");
}
check(RelocateShipTraceCatalogGlobals(0, catalog) == std::array<uintptr_t,3>{},
      "missing image base cannot arm healing");
check(RelocateShipTraceCatalogGlobals(UINTPTR_MAX, catalog) == std::array<uintptr_t,3>{},
      "catalog relocation overflow refuses all manager addresses");
auto serverScan=[&]{return ShipTraceServerActorVtables(m.data(),m.size(),n->OptionalHeader.ImageBase);};
check(serverScan()==std::array<std::size_t,2>{0x5F60BB0,0x5F610C8},"real current server actor primary RTTI and methods");
for(size_t a:{size_t{0x5F60BA8},size_t{0x5F610C8},size_t{0x63E320C},size_t{0x70B3398}}){
 auto old=m[a];m[a]^=1;
 check(serverScan()==std::array<std::size_t,2>{},"mutated server actor class contract fails closed");
 m[a]=old;
}
check(scan()==0x1217F2C,"valid observer restored after all private mutations");std::cout<<"PASS "<<checks<<" exact ship diagnostic scanner checks; inert image only\n";}
