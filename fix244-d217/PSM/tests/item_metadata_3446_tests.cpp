#include "runtime/item_metadata.h"
#include <array>
#include <cstdio>
using namespace psm::items;
std::array<unsigned char,0x500> info{};
uint32_t received=0;
uintptr_t __fastcall Legacy(const uint16_t* key) { received=*key;return reinterpret_cast<uintptr_t>(info.data()); }
uintptr_t __fastcall Current(uintptr_t unused,uint32_t key) { received=key;return unused==0?reinterpret_cast<uintptr_t>(info.data()):0; }
int main() {
    int failures=0,checks=0;
    const auto check=[&](bool value,const char* name){++checks;if(!value){++failures;std::printf("FAIL: %s\n",name);}};
    MetadataProfile old{},current{ItemMetadataKind::KeyEdx3446,0x10,0x1D,8};
    const int64_t stack=300,legacy_stack=12;const uint32_t id=12345,legacy_id=999;const uint8_t weighted=1;
    std::memcpy(info.data()+0x10,&stack,8);std::memcpy(info.data()+0x18,&legacy_stack,8);
    std::memcpy(info.data()+8,&id,4);std::memcpy(info.data(),&legacy_id,4);info[0x1D]=weighted;
    auto result=LookupMetadata(reinterpret_cast<uintptr_t>(Current),42,current);
    check(result==reinterpret_cast<uintptr_t>(info.data())&&received==42,"3446 lookup receives item key in EDX and zero unused RCX");
    MetadataSnapshot metadata{};
    check(ReadMetadataSnapshot(result,current,metadata)&&metadata.stack_size==300&&metadata.item_id==12345&&metadata.weighted==1,
          "3446 compact metadata reads native stack weight and debt ID fields");
    info[0x1D]=0;
    check(ReadMetadataSnapshot(result,current,metadata)&&metadata.weighted==0,"3446 unweighted items retain existing eligibility");
    result=LookupMetadata(reinterpret_cast<uintptr_t>(Legacy),77,old);
    check(received==77&&result!=0,"legacy metadata pointer ABI preserved");
    check(ReadMetadataSnapshot(result,old,metadata)&&metadata.stack_size==12&&metadata.item_id==999&&metadata.weighted==0,
          "legacy metadata field profile preserved");
    MetadataProfile native3663{ItemMetadataKind::PointerRcx3663,0x10,0x1D,8};
    result=LookupMetadata(reinterpret_cast<uintptr_t>(Legacy),3663,native3663);
    check(received==3663&&result==reinterpret_cast<uintptr_t>(info.data()),
          "3663 metadata receives a pointer to the item key in RCX");
    check(ReadMetadataSnapshot(result,native3663,metadata)&&metadata.stack_size==300&&metadata.item_id==12345,
          "3663 RCX calling convention retains the compact row layout");
    for(auto profile:{MetadataProfile{ItemMetadataKind::KeyEdx3446,0x18,0x1D,8},
                      MetadataProfile{ItemMetadataKind::KeyEdx3446,0x10,0x3F0,8},
                      MetadataProfile{ItemMetadataKind::KeyEdx3446,0x10,0x1D,0},
                      MetadataProfile{ItemMetadataKind::PointerRcx,0x10,0x1D,8}}) {
        received=0;
        check(LookupMetadata(reinterpret_cast<uintptr_t>(Current),42,profile)==0&&received==0,
              "mixed metadata ABI or field tuple fails closed before native call");
    }
    check(LookupMetadata(0,42,current)==0&&!ReadMetadataSnapshot(0,current,metadata),"missing metadata pointer fails closed");
    std::printf("item metadata checks=%d failures=%d\n",checks,failures);
    return failures?1:0;
}
