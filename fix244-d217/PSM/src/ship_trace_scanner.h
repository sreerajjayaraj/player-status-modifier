#pragma once
#include "native_3446.h"
#include "ship_trace_profiles.h"
#include "ship_metadata_profiles.h"
#include "native_catalog_scanner.h"
#include <array>
#include <cstddef>
inline std::size_t ShipTraceVehicleSite(const void*data,std::size_t size,std::size_t capture){
 psm::v3446::Image i;if(!i.Init(data,size)||!psm::v3446::HasCurrentResourceLayout(i))return 0;
 const auto root=i.Find(psm::v3446::kRoot),key=i.Find(psm::v3446::kShipActorVehicleKey),getter=i.Find(psm::v3446::kShipVehicleInfoGetter),reader=i.Find(psm::v3446::kShipVehicleInfoReader);
 if(!root||!key||!getter||!reader||capture!=root+0x239||i.Branch(root+0x27F)!=key||i.Branch(root+0x287)!=getter)return 0;
 // The exact native getter has returned VehicleInfo* in RAX at this boundary.
 return root+0x28C;
}

inline std::array<std::size_t,3> ShipTraceCatalogGlobals(const void* data, std::size_t size,
                                                      std::size_t capture) {
 using namespace psm::v3446;
 Image image;
 if (!image.Init(data, size) || !ShipTraceVehicleSite(data, size, capture)) return {};
 const auto root = capture - 0x239;
 const auto key = image.Branch(root + 0x27F);
 const auto character = image.Branch(key + 0x44);
 const auto gimmick = image.Branch(key + 0x77);
 const auto vehicle = image.Branch(root + 0x287);
 const std::array<std::size_t,3> globals = {
     psm::native_catalog::ManagerGlobal(image, character, kShipCharacterInfoGetter),
     psm::native_catalog::ManagerGlobal(image, gimmick, kShipGimmickInfoGetter),
     psm::native_catalog::ManagerGlobal(image, vehicle, kShipVehicleInfoGetter)
 };
 if (!globals[0] || !globals[1] || !globals[2] ||
     globals[0] == globals[1] || globals[0] == globals[2] || globals[1] == globals[2]) return {};
 return globals;
}

inline std::array<uintptr_t,3> RelocateShipTraceCatalogGlobals(
    uintptr_t image_base, const std::array<std::size_t,3>& globals) {
 if (!image_base) return {};
 std::array<uintptr_t,3> addresses{};
 for (std::size_t index = 0; index < globals.size(); ++index) {
  if (!globals[index] || globals[index] > UINTPTR_MAX - image_base) return {};
  addresses[index] = image_base + globals[index];
 }
 return addresses;
}

inline std::array<std::size_t,2> ShipTraceServerActorVtables(const void* data, std::size_t size, uintptr_t image_base) {
 using namespace psm::v3446;
 Image image;
 if (!image.Init(data,size) || !HasCurrentResourceLayout(image)) return {};
 constexpr std::array<std::size_t,2> tables={0x5F60BB0,0x5F610C8};
 constexpr std::array<std::size_t,2> locators={0x63E3208,0x63E3230};
 constexpr std::array<std::size_t,2> descriptors={0x70B3350,0x70B3388};
 constexpr std::array<std::size_t,2> methods={0x2BCE860,0x2BCE960};
 constexpr const char* names[]={".?AVServerNormalInGameActor@pa@@",".?AVServerChildOnlyInGameActor@pa@@"};
 for (std::size_t index=0;index<tables.size();++index) {
  uintptr_t locator=0,method=0;
  std::array<uint32_t,6> record{};
  const auto name_length=std::strlen(names[index])+1;
  if (!image.Read(tables[index]-8,locator) || locator!=image_base+locators[index] ||
      !image.Read(tables[index],method) || method!=image_base+methods[index] || !image.Exec(methods[index]) ||
      !image.Read(locators[index],record) || record[0]!=1 || record[1]!=0 ||
      record[3]!=descriptors[index] || record[5]!=locators[index] ||
      !image.Range(descriptors[index]+16,name_length) ||
      std::memcmp(image.data+descriptors[index]+16,names[index],name_length)) return {};
 }
 return tables;
}
