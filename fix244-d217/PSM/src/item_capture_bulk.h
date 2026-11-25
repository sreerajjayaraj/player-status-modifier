#pragma once
#include "item_capture_compat.h"
#include "item_capture_bulk0413_profiles.h"
namespace psm::capture_compat {
struct BulkContext {
 const void* module=nullptr;Capture owner{};uintptr_t hooks=0,count=0,capacity=0;bool valid=false;
 static BulkContext From(const void* module){
  BulkContext c{};if(!module)return c;v3446::Image image;if(!image.Init(module,v3446::ModuleSize(module)))return c;
  if(image.nt.FileHeader.TimeDateStamp!=0x6AEDD8DEu||image.size!=0x289000||image.nt.OptionalHeader.CheckSum)return c;
  using namespace bulk0413;
  const std::array<const v3446::Profile*,18> profiles{&bulk0413::kInstaller,&bulk0413::kWrapper,&bulk0413::kCallback,&bulk0413::kQueue,&bulk0413::kSafeRead,&bulk0413::kCreate,&bulk0413::kEnableLeaf,&bulk0413::kEnable,&bulk0413::kPatch,&bulk0413::kAllocWrapper,&bulk0413::kAllocator,&bulk0413::kFreeLeaf,&bulk0413::kExecutable,&bulk0413::kTrampoline,&bulk0413::kDecoder,&bulk0413::kRemove,&bulk0413::kInitialize,&bulk0413::kApplyQueued};
  std::array<std::size_t,18> found{};
  for(unsigned i=0;i<profiles.size();++i)if(!(found[i]=image.Find(*profiles[i])))return c;
  if(image.Branch(found[0]+0x1CC)!=found[1]||
     image.Branch(found[1]+0x4D)!=found[5]||
     image.Branch(found[1]+0x5B)!=found[6]||
     image.Branch(found[1]+0x69)!=found[15]||
     image.Branch(found[2]+0x164)!=found[4]||
     image.Branch(found[2]+0x1F2)!=found[3]||
     image.Branch(found[5]+0x73)!=found[12]||
     image.Branch(found[5]+0x83)!=found[12]||
     image.Branch(found[5]+0xB9)!=found[9]||
     image.Branch(found[5]+0xDE)!=found[13]||
     image.Branch(found[5]+0x12B)!=found[11]||
     image.Branch(found[5]+0x242)!=found[11]||
     image.Branch(found[6]+0x5,0xE9)!=found[7]||
     image.Branch(found[7]+0xD8)!=found[8]||
     image.Branch(found[9]+0x4)!=found[10]||
     image.Branch(found[13]+0xA6)!=found[14]||
     image.Branch(found[13]+0x317)!=found[12]||
     image.Branch(found[15]+0xC8)!=found[8]||
     image.Branch(found[15]+0xED)!=found[11]||
     image.Branch(found[17]+0xFA)!=found[8])return c;
  const auto slot=Context::Rip(image,found[0]+0x1B7,{0x4C,0x8D,0x0D});
  if(!slot||Context::Rip(image,found[0]+0x1BE,{0x4C,0x8D,0x05})!=found[2]||
     Context::Rip(image,found[2]+0x34,{0x48,0x8B,0x05})!=slot||
     slot!=0x22D700||!Context::DataSection(image,slot,8,true)||
     !Context::DataSection(image,0x27B120,16,true)||!Context::DataSection(image,0x227EE0,sizeof(kDecodeTable),true)||
     std::memcmp(image.data+0x227EE0,kDecodeTable,sizeof(kDecodeTable)))return c;
  const auto base=reinterpret_cast<uintptr_t>(module);c.module=module;c.owner={base+found[2],base+slot,6};
  c.hooks=base+0x27B120;c.count=base+0x27B12C;c.capacity=base+0x27B128;c.valid=true;return c;
 }
 bool Same(const BulkContext& o)const{return valid&&o.valid&&module==o.module&&owner.handler==o.owner.handler&&owner.original_slot==o.owner.original_slot&&hooks==o.hooks&&count==o.count&&capacity==o.capacity;}
};
struct BulkRecordSnapshot {
 uintptr_t table=0,address=0;uint32_t count=0,capacity=0;std::array<unsigned char,56> record{};
 bool operator==(const BulkRecordSnapshot&)const=default;
};
inline bool BulkRecord(const BulkContext& context,uintptr_t entry,uintptr_t original,uintptr_t relay,
                       bool nested,uintptr_t predecessor,const v3446::Profile& profile,
                       BulkRecordSnapshot& out,const Reader& reader){
 if(!context.valid||!reader.Read(context.hooks,&out.table,8)||!reader.Read(context.count,&out.count,4)||
    !reader.Read(context.capacity,&out.capacity,4)||!out.table||(out.table&7)||!out.count||out.count>out.capacity||out.capacity>4096||
    out.table>(std::numeric_limits<uintptr_t>::max)()-std::size_t(out.count)*56)return false;
 unsigned matches=0;
 for(unsigned i=0;i<out.count;++i){std::array<unsigned char,56> row{};const auto at=out.table+i*56;
  if(!reader.Read(at,row.data(),row.size()))return false;uintptr_t target=0;std::memcpy(&target,row.data(),8);
  if(target==entry){++matches;out.address=at;out.record=row;}
 }
 if(matches!=1)return false;uintptr_t targetRelay=0,targetOriginal=0;std::memcpy(&targetRelay,out.record.data()+8,8);std::memcpy(&targetOriginal,out.record.data()+16,8);
 if(targetRelay!=relay||targetOriginal!=original||(out.record[32]&7)!=6||(out.record[36]&15)!=(nested?1:3))return false;
 std::array<unsigned char,5> backup{};
 if(nested){const auto delta=std::int64_t(predecessor)-std::int64_t(entry+5);if(delta<INT32_MIN||delta>INT32_MAX)return false;backup[0]=0xE9;const auto d=static_cast<int32_t>(delta);std::memcpy(backup.data()+1,&d,4);}
 else std::memcpy(backup.data(),profile.code,5);
 if(std::memcmp(out.record.data()+24,backup.data(),5)||out.record[40]||out.record[48])return false;
 if(!nested&&(out.record[41]!=2||out.record[42]!=6||out.record[49]!=2||out.record[50]!=6))return false;
 return true;
}
struct ChainSnapshot {
 std::array<unsigned char,4096> native{};std::array<unsigned char,64> menu_bridge{},bulk_bridge{};
 uintptr_t menu_original=0,menu_relay=0,bulk_original=0,bulk_relay=0;
 std::array<uint64_t,5> menu_record{};BulkRecordSnapshot bulk_record{};
 bool operator==(const ChainSnapshot&)const=default;
};
struct OwnerMatch {
 std::size_t rva=0;bool captured=false;unsigned route=0;NativeMatch ordinary{};ChainSnapshot snapshot{};
};
// Routes are explicit: 0 native,1 direct Menu,2 direct Bulk,3 Menu over Bulk,4 Bulk over Menu.
inline bool ObserveOwnerChain(const v3446::Image& image,const v3446::Profile& profile,std::size_t rva,
                              const Context& menu,const BulkContext& bulk,bool menu512,unsigned route,
                              ChainSnapshot& out,const Reader& reader={}){
 if(route<2||route>4||!bulk.valid||!rva||profile.size>out.native.size()||profile.size<=6||
    !image.Exec(rva,profile.size)||!image.Fragments(rva,profile))return false;
 const bool pair=route>=3;if(pair&&(!menu512||!menu.valid||!menu.owner.record))return false;
 const auto entry=reinterpret_cast<uintptr_t>(image.data)+rva;
 if(!reader.Read(entry,out.native.data(),profile.size)||out.native[0]!=0xE9||std::memcmp(out.native.data()+5,profile.code+5,profile.size-5)||
    !reader.Read(bulk.owner.original_slot,&out.bulk_original,8)||!out.bulk_original)return false;
 const bool bulkNested=route==4;const auto bulkSize=bulkNested?28u:34u;
 if(out.bulk_original>(std::numeric_limits<uintptr_t>::max)()-bulkSize||!reader.Exec(out.bulk_original,bulkSize)||!reader.Read(out.bulk_original,out.bulk_bridge.data(),bulkSize))return false;
 out.bulk_relay=out.bulk_original+(bulkNested?14:20);
 if(!AbsoluteJump(out.bulk_bridge.data()+(bulkNested?14:20),bulk.owner.handler))return false;
 if(!bulkNested&&(std::memcmp(out.bulk_bridge.data(),profile.code,6)||!AbsoluteJump(out.bulk_bridge.data()+6,entry+6)))return false;
 if(pair){
  if(!reader.Read(menu.owner.original_slot,&out.menu_original,8)||!out.menu_original)return false;
  const bool menuNested=route==3;const auto menuSize=menuNested?28u:34u;
  if(out.menu_original>(std::numeric_limits<uintptr_t>::max)()-menuSize||!reader.Exec(out.menu_original,menuSize)||!reader.Read(out.menu_original,out.menu_bridge.data(),menuSize))return false;
  out.menu_relay=out.menu_original+(menuNested?14:20);
  if(!AbsoluteJump(out.menu_bridge.data()+(menuNested?14:20),menu.owner.handler))return false;
  if(menuNested){if(!AbsoluteJump(out.menu_bridge.data(),out.bulk_relay))return false;}
  else if(std::memcmp(out.menu_bridge.data(),profile.code,6)||!AbsoluteJump(out.menu_bridge.data()+6,entry+6))return false;
  if(bulkNested&&!AbsoluteJump(out.bulk_bridge.data(),out.menu_relay))return false;
  if(!reader.Read(menu.owner.record,out.menu_record.data(),sizeof(out.menu_record))||out.menu_record[0]!=entry||out.menu_record[1]!=out.menu_original||
     out.menu_record[2]!=out.menu_relay||!out.menu_record[3]||out.menu_record[4]!=0x100000001ULL)return false;
 }
 int32_t displacement=0;std::memcpy(&displacement,out.native.data()+1,4);const auto relay=std::int64_t(entry)+5+displacement;
 if(relay<=0||uintptr_t(relay)!=(route==3?out.menu_relay:out.bulk_relay)||
    !BulkRecord(bulk,entry,out.bulk_original,out.bulk_relay,bulkNested,out.menu_relay,profile,out.bulk_record,reader))return false;
 return image.Fragments(rva,profile);
}
inline bool IsMenu512(const Context& menu){
 if(!menu.valid)return false;v3446::Image image;return image.Init(menu.module,v3446::ModuleSize(menu.module))&&((image.nt.FileHeader.TimeDateStamp==0x8AD596CEu&&image.size==0x95C000)||(image.nt.FileHeader.TimeDateStamp==0x1BB5DE60u&&image.size==0x95E000));
}
inline OwnerMatch FindOwner(const v3446::Image& image,const v3446::Profile& profile,const Context& menu,const BulkContext& bulk,const Reader& reader={}){
 OwnerMatch result{};const auto ordinary=Find(image,profile,menu.owner,reader);
 if(ordinary.rva){result.rva=ordinary.rva;result.captured=ordinary.captured;result.route=ordinary.captured?1:0;result.ordinary=ordinary;return result;}
 if(!bulk.valid)return {};const v3446::Profile suffix{profile.code+5,profile.size-5,nullptr,0};const auto tail=image.Find(suffix,false);if(tail<5)return {};
 const bool menu512=IsMenu512(menu);
 for(unsigned route=2;route<=4;++route){ChainSnapshot a{},b{};
  if(ObserveOwnerChain(image,profile,tail-5,menu,bulk,menu512,route,a,reader)&&ObserveOwnerChain(image,profile,tail-5,menu,bulk,menu512,route,b,reader)&&a==b){
   result.rva=tail-5;result.captured=true;result.route=route;result.snapshot=a;return result;
  }
 }return {};
}
inline bool StableOwner(const v3446::Image& image,const v3446::Profile& profile,const OwnerMatch& match,const Context& menu,const BulkContext& bulk,const Reader& reader={}){
 if(match.route<=1)return Stable(image,profile,match.ordinary,menu.owner,reader);
 ChainSnapshot now{};return ObserveOwnerChain(image,profile,match.rva,menu,bulk,IsMenu512(menu),match.route,now,reader)&&now==match.snapshot;
}
}



