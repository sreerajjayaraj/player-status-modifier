#include "oxygen_scanner.h"
#include "native_3446.h"
#include "oxygen_native_profiles.h"
#include "oxygen_catalog_profiles.h"
#include "oxygen_3732_profiles.h"
#include "native_catalog_scanner.h"
#include <cstring>

namespace {
using psm::v3446::Image;

constexpr bool NonCode(const IMAGE_SECTION_HEADER& section) {
 return (section.Characteristics & (IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE)) == IMAGE_SCN_MEM_READ;
}

std::size_t SectionSize(const IMAGE_SECTION_HEADER& section) {
 return (std::max)(section.Misc.VirtualSize, section.SizeOfRawData);
}

bool ReadOnlyData(const Image& image, std::size_t at, std::size_t count) {
 for (unsigned index=0; index<image.nt.FileHeader.NumberOfSections; ++index) {
  const auto& section=image.sections[index];
  const auto length=SectionSize(section);
  if (NonCode(section) && !(section.Characteristics & IMAGE_SCN_MEM_WRITE) &&
      at>=section.VirtualAddress && at-section.VirtualAddress<length &&
      count<=length-(at-section.VirtualAddress)) return image.Range(at,count);
 }
 return false;
}

// A named MSVC x64 descriptor is only a starting point. Verify the primary
// locator, its hierarchy and the executable vtable methods in the game.
template<std::size_t N>
std::size_t TypedVtable(const Image& image, uintptr_t loaded_base, const char (&class_name)[N]) {
 std::size_t descriptor=0;
 for (unsigned index=0; index<image.nt.FileHeader.NumberOfSections; ++index) {
  const auto& section=image.sections[index];
  const auto start=section.VirtualAddress;
  const auto length=SectionSize(section);
  if (!NonCode(section) || length<16+sizeof(class_name) || !image.Range(start,length)) continue;
  const auto* end=image.data+start+length;
  auto* cursor=image.data+start+16;
  while (cursor<end) {
   const auto* match=std::search(cursor,end,std::begin(class_name),std::end(class_name));
   if (match==end) break;
   const auto candidate=static_cast<std::size_t>(match-image.data)-16;
   if (candidate%alignof(uintptr_t)==0) {
    if (descriptor) return 0;
    descriptor=candidate;
   }
   cursor=match+1;
  }
 }
 if (!descriptor) return 0;

 std::size_t locator=0;
 for (unsigned index=0; index<image.nt.FileHeader.NumberOfSections; ++index) {
  const auto& section=image.sections[index];
  const auto start=section.VirtualAddress;
  const auto length=SectionSize(section);
  if (!ReadOnlyData(image,start,length) || length<24) continue;
  for (std::size_t at=start; at<=start+length-24; at+=alignof(uint32_t)) {
   uint32_t type=0;
   if (!image.Read(at+12,type) || type!=descriptor) continue;
   std::array<uint32_t,6> record{};
   if (!image.Read(at,record) || record[0]!=1 || record[1]!=0 || record[2]!=0 || record[5]!=at ||
       !ReadOnlyData(image,record[4],16)) continue;
   std::array<uint32_t,4> hierarchy{};
   if (!image.Read(record[4],hierarchy) || hierarchy[0]!=0 || hierarchy[2]<2 || hierarchy[2]>128 ||
       !ReadOnlyData(image,hierarchy[3],hierarchy[2]*sizeof(uint32_t))) continue;
   uint32_t base_descriptor=0, declared_type=0;
   if (!image.Read(hierarchy[3],base_descriptor) || !ReadOnlyData(image,base_descriptor,sizeof(uint32_t)) ||
       !image.Read(base_descriptor,declared_type) || declared_type!=descriptor) continue;
   if (locator) return 0;
   locator=at;
  }
 }
 if (!locator) return 0;

 std::size_t result=0;
 const auto preferred=static_cast<uintptr_t>(image.nt.OptionalHeader.ImageBase);
 for (unsigned index=0; index<image.nt.FileHeader.NumberOfSections; ++index) {
  const auto& section=image.sections[index];
  const auto start=section.VirtualAddress;
  const auto length=SectionSize(section);
  if (!ReadOnlyData(image,start,length) || length<4*sizeof(uintptr_t)) continue;
  for (std::size_t at=start; at<=start+length-4*sizeof(uintptr_t); at+=alignof(uintptr_t)) {
   uintptr_t pointer=0;
   if (!image.Read(at,pointer)) continue;
   const auto base=pointer==loaded_base+locator ? loaded_base :
                   pointer==preferred+locator ? preferred : 0;
   if (!base) continue;
   std::array<uintptr_t,3> methods{};
   const auto table=at+sizeof(uintptr_t);
   if (!image.Read(table,methods) ||
       !std::all_of(methods.begin(),methods.end(),[&](uintptr_t target) {
        return target>=base && image.Exec(target-base);
       })) continue;
   if (result) return 0;
   result=table;
  }
 }
 return result;
}

bool HasServerMinimumRule(const Image& image, std::size_t table, uintptr_t loaded_base) {
 // The status component's server dispatch slot calls the shared passive
 // transition with old/new values and the typed status selector.
 constexpr std::size_t dispatch_slot=0x330;
 constexpr unsigned char dispatch[]{
  0x4C,0x8B,0xCD,0x48,0x89,0x5C,0x24,0x20,0x4C,0x8B,0xC7,0x0F,0xB7,0xD6,0x49,0x8B,0xCF
 };
 constexpr unsigned char minimum[]{
  0x44,0x38,0x70,0x04,0x0F,0x85,0xC4,0,0,0,0x48,0x3B,0xFB,0x0F,0x8F,0x33,0x01,0,0,
  0x4D,0x8B,0xB7,0x88,0,0,0,0x41,0x8B,0x8F,0x90,0,0,0,0x4D,0x8D,0x3C,0xCE
 };
 uintptr_t absolute=0;
 if (!ReadOnlyData(image,table,dispatch_slot+sizeof(absolute)) ||
     !image.Read(table+dispatch_slot,absolute)) return false;
 const auto preferred=static_cast<uintptr_t>(image.nt.OptionalHeader.ImageBase);
 const auto target=absolute>=loaded_base && image.Exec(absolute-loaded_base) ? absolute-loaded_base :
                   absolute>=preferred && image.Exec(absolute-preferred) ? absolute-preferred : 0;
 RUNTIME_FUNCTION owner{};
 if (!target || !image.UniqueRecord(target,owner) || owner.BeginAddress!=target ||
     target+0x79+sizeof(dispatch)+5>owner.EndAddress ||
     std::memcmp(image.data+target+0x79,dispatch,sizeof(dispatch))) return false;
 const auto transition=image.Branch(target+0x79+sizeof(dispatch));
 RUNTIME_FUNCTION entry{}, body{}, canonical{};
 if (!transition || !image.UniqueRecord(transition,entry) || entry.BeginAddress!=transition ||
     !image.UniqueRecord(transition+0x70,body) ||
     transition+0x70+sizeof(minimum)>body.EndAddress ||
     !image.Canonical(body,canonical) || canonical.BeginAddress!=transition ||
     std::memcmp(image.data+transition+0x70,minimum,sizeof(minimum))) return false;
 const auto apply=image.Branch(transition+0x114);
 return apply && image.UniqueRecord(apply,owner) && owner.BeginAddress==apply;
}
}
psm::oxygen::Sites psm::oxygen::Scan(const void* module,std::size_t size) {
 using namespace psm::v3446;
 Image i;
 if (!i.Init(module, size)) return {};
 const auto prior_slot = i.Find(kSlot);
 const auto current_slot = i.Find(layout3732::kSlot);
 if (bool(prior_slot) == bool(current_slot)) return {};
 const auto base = reinterpret_cast<uintptr_t>(module);
 const auto slot = prior_slot ? prior_slot : current_slot;
 const auto profile = [&](const Profile& prior, const Profile& current) -> const Profile& {
     return prior_slot ? prior : current;
 };
 Sites sites{0,base+slot+0x7B,{}};
 const auto rip=[&](std::size_t at){
  std::int32_t displacement=0;
  if(!i.Exec(at,7)||!i.Read(at+3,displacement))return std::size_t{0};
  const auto target=static_cast<std::int64_t>(at)+7+displacement;
  return target>0&&static_cast<std::uint64_t>(target)<i.size?static_cast<std::size_t>(target):0;
 };
 const auto readable=[&](std::size_t at,std::size_t length,DWORD flags=IMAGE_SCN_MEM_READ){
  if(!i.Range(at,length))return false;
  for(unsigned n=0;n<i.nt.FileHeader.NumberOfSections;++n){const auto& section=i.sections[n];
   const auto extent=(std::max)(section.Misc.VirtualSize,section.SizeOfRawData);
   if((section.Characteristics&flags)==flags&&at>=section.VirtualAddress&&
      at-section.VirtualAddress<=extent&&length<=extent-(at-section.VirtualAddress))return true;
  }return false;
 };
 const auto actor_manager = native_catalog::ManagerGlobal(i, i.Branch(slot + 0x1C),
                                                          profile(catalog_profiles::kActorInfo, layout3732::kActorInfo));
 const auto group_manager = native_catalog::ManagerGlobal(i, i.Branch(slot + 0x32),
                                                          profile(catalog_profiles::kStatusGroup, layout3732::kStatusGroup));
 const auto status_manager = native_catalog::ManagerGlobal(i, i.Branch(slot + 0x44),
                                                           profile(catalog_profiles::kStatusInfo, layout3732::kStatusInfo));
 if (actor_manager && group_manager && status_manager &&
     actor_manager != group_manager && actor_manager != status_manager && group_manager != status_manager) {
    sites.catalog = {base + actor_manager, base + group_manager, base + status_manager};
    if (const auto server=TypedVtable(i,base,".?AVServerNormalInGameActor@pa@@"))
     sites.server_actor_vtable=base+server;
    if (current_slot) {
     const auto status=TypedVtable(i,base,".?AVServerStatusActorComponent@pa@@");
     if (status && HasServerMinimumRule(i,status,base)) sites.server_status_vtable=base+status;
    }
 }
 const auto root = i.Find(profile(kRoot, layout3732::kRoot));
 const auto init = i.Find(profile(kBreathInit, layout3732::kBreathInit));
 const auto resolver = i.Find(profile(kBreathResolver, layout3732::kBreathResolver));
 const auto gauge = i.Find(profile(kBreathGauge, layout3732::kBreathGauge));
 const auto query = i.Find(profile(kQuery, layout3732::kQuery));
 const auto maximum = i.Find(profile(kBreathMaximum, layout3732::kBreathMaximum));
 const auto reader = i.Find(profile(kReader, layout3732::kReader));

 // Bind the named native registration through the resolver's actual token
 // cache. Relative addresses may move while the role and owner stay the same.
 constexpr char name[] = "selector-breath-status-gauge";
 constexpr unsigned char prefix[] = {0x41,0xB9,0xFF,0xFF,0x02,0x00,0x48,0x8D,0x15};
 constexpr unsigned char operands[] = {0x41,0xB8,0x01,0x00,0x00,0x00,0x48,0x8D,0x0D};
 constexpr unsigned char registrar_entry[] = {
     0x48,0x89,0x5C,0x24,0x20,0x44,0x89,0x44,0x24,0x18,0x48,0x89,0x54,0x24,0x10,
     0x48,0x89,0x4C,0x24,0x08,0x55,0x56,0x57,0x41
 };
 const auto cache = resolver ? rip(resolver + 0x3A) : 0;
 if (!cache || !readable(cache, sizeof(std::uint32_t), IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE))
     return sites;
 std::size_t registration = 0;
 for (unsigned section_index = 0; section_index < i.nt.FileHeader.NumberOfSections; ++section_index) {
     const auto& section = i.sections[section_index];
     const auto length = (std::max)(section.Misc.VirtualSize, section.SizeOfRawData);
     if (length < 31 || !i.Exec(section.VirtualAddress, length)) continue;
     const auto* end = i.data + section.VirtualAddress + length;
     for (auto* cursor = i.data + section.VirtualAddress; end - cursor >= 31;) {
         const auto* match = std::search(cursor, end, std::begin(prefix), std::end(prefix));
         if (end - match < 31) break;
         cursor = match + 1;
         if (std::memcmp(match + 13, operands, sizeof(operands)) || match[26] != 0xE9) continue;
         const auto at = static_cast<std::size_t>(match - i.data);
         if (rip(at + 0x13) != cache) continue;
         const auto literal = rip(at + 6);
         if (!literal || !readable(literal, sizeof(name)) || std::memcmp(i.data + literal, name, sizeof(name))) continue;
         const auto registrar = i.Branch(at + 0x1A, 0xE9);
         RUNTIME_FUNCTION owner{};
         if (!registrar || !i.UniqueRecord(registrar, owner) || owner.BeginAddress != registrar ||
             owner.EndAddress - owner.BeginAddress != 0x84C ||
             std::memcmp(i.data + registrar, registrar_entry, sizeof(registrar_entry))) continue;
         if (registration) return sites;
         registration = at;
     }
 }
 if (!registration) return sites;
 if(!root||!init||!resolver||!gauge||!query||!maximum||!reader)return sites;
 if(i.Branch(root+0x84E)!=gauge||i.Branch(init+0x759)!=resolver||
    i.Branch(gauge+0x95)!=query||i.Branch(gauge+0xE2)!=maximum||
    i.Branch(query+0x15F)!=slot||i.Branch(query+0x172)!=reader||
    i.Branch(maximum+0x2D)!=slot)return sites;
 sites.breath_before_call=base+root+0x84E;
 // Both the normal gauge call and its cached-data bypass converge here.
 // RBX holds the actor and RSI owns the registered Breath gauge at +0x1B0.
 constexpr unsigned char actor_args[]{0x48,0x8B,0xD3,0x49,0x8B,0xC8};
 constexpr unsigned char branch_into_sample[]{0x76,0x13};
 const auto sample=root+0x85B;
 if (current_slot && i.Exec(root+0x846,0x15+kGaugeSampleWitness.size()) &&
     !std::memcmp(i.data+root+0x846,branch_into_sample,sizeof(branch_into_sample)) &&
     !std::memcmp(i.data+root+0x848,actor_args,sizeof(actor_args)) &&
     !std::memcmp(i.data+sample,kGaugeSampleWitness.data(),kGaugeSampleWitness.size()))
    sites.gauge_sample=base+sample;
 if (sites.gauge_sample) {
  // The native client path compares the actor's local-control token with
  // the current world's token and also requires the local-control flag.
  constexpr unsigned char compare[]{
   0x49,0x8B,0x10,0x0F,0xB6,0x42,0x48,0x48,0x8B,0x54,0xC2,0x10,0x80,0x7A,0x09,0x00,
   0x0F,0x84,0x45,0x01,0x00,0x00,0x48,0x8B,0x49,0x08,0x49,0x8B,0x50,0x30,
   0x48,0x8B,0x81,0xA0,0x00,0x00,0x00,0x48,0x39,0x42,0x58,0x0F,0x85,0x2C,0x01,0x00,0x00,
   0x80,0xB9,0x94,0x00,0x00,0x00,0x00,0x0F,0x84,0x1F,0x01,0x00,0x00
  };
  const auto matches=i.Find({compare,sizeof(compare),nullptr,0},false);
  constexpr unsigned char load[]{0x4C,0x8B,0x05};
  RUNTIME_FUNCTION owner{};
  if (matches>=7 && i.Exec(matches-7,sizeof(compare)+7) &&
      i.UniqueRecord(matches-7,owner) && owner.EndAddress>=matches+sizeof(compare) &&
      !std::memcmp(i.data+matches-7,load,sizeof(load))) {
   const auto control=rip(matches-7);
   if (control && control%alignof(uintptr_t)==0 && !i.Exec(control,sizeof(uintptr_t)) &&
       readable(control,sizeof(uintptr_t),IMAGE_SCN_MEM_READ|IMAGE_SCN_MEM_WRITE))
    sites.client_control_slot=base+control;
  }
 }
 return sites;
}
