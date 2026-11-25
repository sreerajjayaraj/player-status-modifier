#pragma once
#include "../scanner.h"
#include <cstring>
namespace psm::items {
struct MetadataProfile {
    ItemMetadataKind kind=ItemMetadataKind::PointerRcx;
    std::size_t stack_size=0x18,weighted=0x3F0,item_id=0;
    bool Known() const {
        return (kind==ItemMetadataKind::PointerRcx && stack_size==0x18 && weighted==0x3F0 && item_id==0) ||
               ((kind==ItemMetadataKind::KeyEdx3446 || kind==ItemMetadataKind::PointerRcx3663) &&
                stack_size==0x10 && weighted==0x1D && item_id==8) ||
               (kind==ItemMetadataKind::PointerRcx3663 &&
                stack_size==0x10 && weighted==0x1E && item_id==8);
    }
};
struct MetadataSnapshot { int64_t stack_size=0; uint32_t item_id=0; uint8_t weighted=0; };
inline uintptr_t LookupMetadata(uintptr_t function,uint16_t key,const MetadataProfile& profile) {
    if(!function || !profile.Known()) return 0;
    if(profile.kind==ItemMetadataKind::KeyEdx3446)
        return reinterpret_cast<uintptr_t(__fastcall*)(uintptr_t,uint32_t)>(function)(0,key);
    return reinterpret_cast<uintptr_t(__fastcall*)(const uint16_t*)>(function)(&key);
}
inline bool ReadMetadataSnapshot(uintptr_t info,const MetadataProfile& profile,MetadataSnapshot& out) {
    if(!info||!profile.Known()) return false;
    std::memcpy(&out.stack_size,reinterpret_cast<const void*>(info+profile.stack_size),sizeof(out.stack_size));
    std::memcpy(&out.item_id,reinterpret_cast<const void*>(info+profile.item_id),sizeof(out.item_id));
    std::memcpy(&out.weighted,reinterpret_cast<const void*>(info+profile.weighted),sizeof(out.weighted));
    return true;
}
}
