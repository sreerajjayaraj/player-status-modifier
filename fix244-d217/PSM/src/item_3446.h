#pragma once
#include "scanner.h"
#include "item_3446_profiles.h"
#include "item_capture_compat.h"
#include "item_capture_bulk.h"
#include "item_insert_3663.h"
#include "item_3732.h"
namespace psm::items3446 {
#include "item_move_180_profiles.h"
enum class ResolveStage { Image,RequiredProfiles,NativeCallLinks,EncodedValues,InventoryTable,ObserverStability,FinalSnapshot,Complete };
inline constexpr const char* kResolveNames[]={"owner","borrowed-capacity","receipt","push","plan","standalone-transfer","metadata","stack-capacity","merge","copy","rows","source-init","source-plan","source-group","source-consumer","pop","sql-gate","mapper","source-remove","destination-group","borrowed-constructor","borrowed-append","inventory-move-request","inventory-move-wrapper","inventory-move-parent"};
struct ResolveTrace {
 ResolveStage stage=ResolveStage::Image;std::array<std::size_t,25> profiles{};uint32_t missing=0;
 bool profile3732=false;
 bool bulk_present=false,bulk_supported=false;unsigned owner_route=0;
 item_insert_3663::Route insert_route=item_insert_3663::Route::Refused;
 bool observer_present=false,observer_supported=false;uint32_t observer_stamp=0,observer_size=0;
 std::size_t failed_call=0,expected_target=0,observed_target=0;
};

inline ItemGainCaptureTarget ResolveDetailed(const void* module,const void* observer_module=nullptr,unsigned* capture_mask=nullptr,ResolveTrace* trace=nullptr,const void* bulk_module=nullptr) {
    if(trace)*trace={};
    if(capture_mask)*capture_mask=0;
    v3446::Image image;
    if (!image.Init(module,v3446::ModuleSize(module))) return {};
    if (const auto plan = image.Find(items3732::kPlan)) {
        ResolveTrace state{};
        state.profile3732 = true;
        state.observer_present = observer_module != nullptr;
        state.bulk_present = bulk_module != nullptr;
        state.stage = ResolveStage::RequiredProfiles;
        unsigned mask = 0;
        const auto result = items3732::Resolve(image, plan, observer_module, state.profiles, mask,
            state.failed_call, state.expected_target, state.observed_target);
        for (std::size_t index = 0; index < state.profiles.size(); ++index)
            if (!state.profiles[index]) state.missing |= std::uint32_t{1} << index;
        if (result.address) {
            state.stage = ResolveStage::Complete;
            state.owner_route = mask & 1u;
            state.insert_route = (mask & 2u) ? item_insert_3663::Route::Menu : item_insert_3663::Route::Native;
        } else if (!state.missing) {
            state.stage = state.failed_call ? ResolveStage::NativeCallLinks : ResolveStage::EncodedValues;
        }
        if (trace) *trace = state;
        if (capture_mask) *capture_mask = mask;
        return result;
    }
    const auto observer=capture_compat::Context::From(observer_module);
    const auto bulk=capture_compat::BulkContext::From(bulk_module);
    if(trace){trace->observer_present=observer_module!=nullptr;trace->observer_supported=observer.valid;v3446::Image other;if(observer_module&&other.Init(observer_module,v3446::ModuleSize(observer_module))){trace->observer_stamp=other.nt.FileHeader.TimeDateStamp;trace->observer_size=static_cast<uint32_t>(other.size);}trace->stage=ResolveStage::RequiredProfiles;}

    const bool current = v3446::HasCurrentResourceLayout(image);
    const auto owner_match = current ? capture_compat::OwnerMatch{} :
        capture_compat::FindOwner(image,kOwner,observer,bulk);
    const auto capacity_match = current ? capture_compat::NativeMatch{} :
        capture_compat::Find(image,kBorrowCapacityPlanner,observer.capacity);
    if(trace){trace->bulk_present=bulk_module!=nullptr;trace->bulk_supported=bulk.valid;trace->owner_route=owner_match.route;}
    auto current_owner = current
        ? item_insert_3663::ObserveStable(image, observer_module, item_insert_3663::Lane::OwnerLookup)
        : item_insert_3663::Proof{};
    const auto owner = current && current_owner ? item_insert_3663::kNativeOwnerRva : owner_match.rva;
    const auto receipt=image.Find(kReceipt),push=image.Find(kPush),plan=image.Find(kPlan),
        transfer=image.Find(kStandalone),metadata=image.Find(kMetadata),
        capacity=image.Find(kCapacity,false),merge=image.Find(kMerge),copy=image.Find(kCopy),
        rows=image.Find(kRows),source_init=image.Find(kSourceInit),source_plan=image.Find(kSourcePlan),
        source_group=image.Find(kSourceGroup),source_consumer=image.Find(kSourceConsumer),
        pop=image.Find(kPop),sql=image.Find(kSqlGate),mapper=image.Find(kMapper,false),
        source_remove=image.Find(kSourceRemove),destination_group=image.Find(kDestinationGroup);
    const auto borrowed_copy=image.Find(kBorrowConstructor),
        borrowed_capacity=current ? item_insert_3663::kNativeInsertRva : capacity_match.rva,
        borrowed_append=image.Find(kBorrowAppend);
    const auto move_request=image.Find(move180::kMoveRequest),move_wrapper=image.Find(move180::kMoveWrapper),move_parent=image.Find(move180::kMoveParent);
    if(trace){trace->profiles={owner,borrowed_capacity,receipt,push,plan,transfer,metadata,capacity,merge,copy,rows,source_init,source_plan,source_group,source_consumer,pop,sql,mapper,source_remove,destination_group,borrowed_copy,borrowed_append,move_request,move_wrapper,move_parent};for(size_t i=0;i<trace->profiles.size();++i)if(!trace->profiles[i])trace->missing|=uint32_t{1}<<i;}
    if(!borrowed_copy||!borrowed_capacity||!borrowed_append||!move_request||!move_wrapper||!move_parent) return {};
    if(!receipt||!push||!plan||!transfer||!owner||!metadata||!capacity||!merge||!copy||!rows||
       !source_init||!source_plan||!source_group||!source_consumer||!pop||!sql||!mapper||
       !source_remove||!destination_group) return {};
    const auto target=[&](std::size_t at){
        const auto entry=image.Branch(at);
        if((current_owner || owner_match.captured)&&entry==owner)return entry;
        if(capacity_match.captured&&entry==borrowed_capacity)return entry;
        return image.Follow(entry);
    };
    if(trace)trace->stage=ResolveStage::NativeCallLinks;
    const auto matches=[&](std::size_t at,std::size_t expected){const auto observed=target(at);if(observed==expected)return true;if(trace&&!trace->failed_call){trace->failed_call=at;trace->expected_target=expected;trace->observed_target=observed;}return false;};
    // All offsets are within uniquely found exact code profiles, never RVAs.
    // Destination ABI: RCX container, RDX error, R8 borrowed C8/E0 receipt,
    // R9W signed slot, fifth native flag; merge and new-stack paths both proven.
    if(!matches(push+0x4D1,receipt)||!matches(push+0x34,owner)||!matches(push+0x53,owner)||
       !matches(push+0x1CB,metadata)||!matches(push+0x3C5,metadata)||
       !matches(push+0x346,mapper)||!matches(push+0x4F6,mapper)||
       !matches(receipt+0xDD,metadata)||!matches(receipt+0xED,capacity)||
       !matches(receipt+0x124,metadata)||!matches(receipt+0x194,merge)||
       !matches(receipt+0x1D1,metadata)||!matches(receipt+0x1E1,capacity)||
       !matches(receipt+0x208,copy)||!matches(receipt+0x21B,metadata)||
       !matches(receipt+0x239,metadata)||!matches(receipt+0x249,capacity)) return {};
    // Native source requests remain 0x28 bytes, destination receipts/SQL rows
    // 0xE0 bytes. Plan hook is before SQL, RDI push/R14 receipts/R15 owner,
    // native return at RSP+0x78. Only source-success gated committed push qualifies.
    if(!matches(plan+0x2A,owner)||!matches(plan+0xC8,destination_group)||!matches(plan+0xF5,rows)||
       !matches(transfer+0x1451,source_init)||!matches(transfer+0x14AB,plan)||
       !matches(transfer+0x14C8,source_plan)||!matches(transfer+0x1528,sql)||
       !matches(transfer+0x1572,pop)||!matches(transfer+0x158C,push)||
       !matches(source_init+0x86,metadata)||!matches(source_plan+0x49,owner)||
       !matches(source_plan+0x99,source_group)||!matches(source_group+0x311,source_consumer)||
       !matches(source_consumer+0x2F4,metadata)||!matches(source_consumer+0x300,metadata)||
       !matches(rows+0xB9,metadata)||!matches(rows+0x1C9,metadata)||
       !matches(pop+0x1C,owner)||!matches(pop+0xD3,source_remove)||
       !matches(destination_group+0xB3,metadata)||!matches(destination_group+0xD8,metadata)) return {};
    // d173 borrowed inputs cannot escape: native constructor/assignment copy
    // C8 values into owned arrays before the native capacity planner emits E0
    // receipts. Keep these complete owners exact, in addition to the 44 profiles.
    if(!matches(destination_group+0x1A7,copy) || !matches(destination_group+0x3EF,borrowed_copy) ||
       !matches(destination_group+0x4CC,borrowed_append)) return {};
    const auto insert_call = destination_group + 0x4B5;
    item_insert_3663::Proof insert{};
    if (current) {
        // The 3663 destination path also uses the native insertion entry.
        // Accept only its exact code or Menu's verified capture of its first
        // instruction. PSM never invokes or hooks the provider trampoline.
        if (insert_call == item_insert_3663::kCallRva)
            insert = item_insert_3663::ObserveStable(image, observer_module, item_insert_3663::Lane::Insert);
        if (!insert) {
            if (trace) {
                trace->failed_call = insert_call;
                trace->expected_target = item_insert_3663::kNativeInsertRva;
                trace->observed_target = image.Branch(insert_call);
            }
            return {};
        }
        if (trace) trace->insert_route = insert.route;
    } else if (!matches(insert_call, borrowed_capacity)) {
        return {};
    }
    // Positive native InventoryMove family and exact zero-receipt fallback.
    if(!matches(move_request+0x2A2,move_wrapper)||
       !matches(move_wrapper+0x38A,transfer)||
       !matches(transfer+0x1230,move_parent)||
       !matches(move_parent+0x328,plan))return {};
    // The request's RTTI virtual slot is a DATA binding, rebased by the loader.
    // Current executable has IMAGE_FILE_RELOCS_STRIPPED and no relocation
    // directory. Its absolute DATA vtable/COL addresses use the fixed PE base;
    // inert inspection maps need not occupy that address.
    const auto requestBase=static_cast<uintptr_t>(image.nt.OptionalHeader.ImageBase);uintptr_t requestMethod=0,col=0;
    uint32_t type=0,self=0,signature=0;
    constexpr char requestName[]=".?AVTrocTrMoveItemInventoryToOtherInventoryReq@pa@@";
    if(!(image.nt.FileHeader.Characteristics&IMAGE_FILE_RELOCS_STRIPPED)||requestBase!=0x140000000ULL||image.nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size||!image.Read(0x5F65EB8+16,requestMethod)||requestMethod!=requestBase+move_request||
       !image.Read(0x5F65EB8-8,col)||col!=requestBase+0x63F2008||
       !image.Read(0x63F2008,signature)||signature!=1||
       !image.Read(0x63F2008+12,type)||type!=0x70B74A0||
       !image.Read(0x63F2008+20,self)||self!=0x63F2008||
       !image.Range(type+16,sizeof(requestName))||std::memcmp(image.data+type+16,requestName,sizeof(requestName)))return {};
    //3663 source requests28, borrowedC8 and destinationE0 layouts remain native.
    // External encoded operands below are actually consumed by these owners;
    // merge stack60 and source sentinelFFFF are literals in exact current code.
    if(trace)trace->stage=ResolveStage::EncodedValues;
    // Divide and receipt strides now use direct literals in exact code.
    // The remaining merge encoding must still decode to the native zero.
    std::uint32_t merge_zero=0;
    if(!image.RipDword(merge+0x5E,2,6,merge_zero)||merge_zero!=0x8D482414u)return {};
    if(trace)trace->stage=ResolveStage::InventoryTable;
    std::int32_t displacement=0;
    if(!image.Read(pop+0x53,displacement)) return {};
    const auto table=std::int64_t(pop)+0x57+displacement;
    if(table<0||!image.Range(std::size_t(table),sizeof(uintptr_t))||image.Exec(std::size_t(table))||
       std::size_t(table)%alignof(uintptr_t)) return {};
    if(trace)trace->stage=ResolveStage::ObserverStability;
    if(((owner_match.captured&&owner_match.route!=2)||capacity_match.captured)&&!observer.Same(capture_compat::Context::From(observer_module)))return {};
    if(owner_match.route>=2&&!bulk.Same(capture_compat::BulkContext::From(bulk_module)))return {};
    if(trace)trace->stage=ResolveStage::FinalSnapshot;
    if (current) {
        // Either proved form forwards to the same native owner. A valid
        // publication between the initial and final snapshots is equivalent.
        insert = item_insert_3663::ObserveStable(image, observer_module, item_insert_3663::Lane::Insert);
        current_owner = item_insert_3663::ObserveStable(image, observer_module, item_insert_3663::Lane::OwnerLookup);
        if (!insert || !current_owner) return {};
        if (trace) {
            trace->insert_route = insert.route;
            trace->owner_route = current_owner.route == item_insert_3663::Route::Menu ? 1 : 0;
        }
    } else if (!capture_compat::StableOwner(image,kOwner,owner_match,observer,bulk) ||
               !capture_compat::Stable(image,kBorrowCapacityPlanner,capacity_match,observer.capacity)) {
        return {};
    }
    if(trace)trace->stage=ResolveStage::Complete;
    const bool captured_insert = insert.route == item_insert_3663::Route::Menu || capacity_match.captured;
    const bool captured_owner = current_owner.route == item_insert_3663::Route::Menu || owner_match.captured;
    if(capture_mask)*capture_mask=(captured_owner?1u:0u)|(captured_insert?2u:0u);
    const auto base=reinterpret_cast<uintptr_t>(module);
    return {base+receipt,ItemGainCaptureKind::NativeItemReceipt,base+push+0x4D6,base+metadata,
            base+push,base+transfer+0x1591,base+std::size_t(table),
            base+plan+0xE6,base+transfer+0x14B0,base+mapper,
            ItemMetadataKind::PointerRcx3663,0x10,0x1D,0x08,base+plan,base+owner,base+move_parent,base+transfer+0x1235,base+move_parent+0x32D};
}
inline ItemGainCaptureTarget Resolve(const void* module,const void* observer_module=nullptr,unsigned* capture_mask=nullptr){return ResolveDetailed(module,observer_module,capture_mask,nullptr);}
inline ItemGainCaptureTarget ResolveWithBulk(const void* module,const void* observer_module,unsigned* capture_mask,const void* bulk_module){return ResolveDetailed(module,observer_module,capture_mask,nullptr,bulk_module);}

}
