#include "hooks/game_hooks.h"
#include "runtime/ship_trace.h"
#include "runtime/item_timing.h"
#include "runtime/player_identity_publication.h"
#include "hooks/hooks_internal.h"

#include "config.h"
#include "logger.h"
#include "mod_logic.h"
#include "position_control.h"
#include "scanner.h"
#include "runtime/item_receipt.h"
#include "runtime/item_metadata.h"
#include "runtime/item_gain_math.h"
#include "menu_item_gain_512.h"
#include "menu_item_policy_v1.h"
#include "damage_menu514_contract.h"
#include <new>

#include <Windows.h>
#include <intrin.h>

#include <atomic>
#include <cmath>
#include <limits>
#include <cstdint>
#include <utility>

namespace {

constexpr int32_t kHealthStatusId = 0;
namespace damage514 = psm::damage_menu514;
damage514::Binding g_damage_binding{};
std::atomic<bool> g_damage_binding_ready{false};
std::atomic<bool> g_damage_stat_writer{false};
std::atomic<std::uint32_t> g_item_gain_affinity_diagnostic_samples{0};
std::atomic<std::uint32_t> g_item_gain_aggressive_samples{0};
std::atomic<std::uint32_t> g_damage_aggressive_samples{0};
std::atomic<std::uint32_t> g_damage_trace_samples{0};
ItemGainCaptureKind g_item_gain_capture_kind =
    ItemGainCaptureKind::LegacyAmountRcxTargetR8Rdi;
uintptr_t g_item_receipt_caller = 0;
uintptr_t g_item_info_lookup = 0;
psm::items::MetadataProfile g_item_metadata_profile{};
uintptr_t g_item_standalone_caller = 0;
uintptr_t g_inventory_type_table_global = 0;
uintptr_t g_item_plan_caller = 0;
uintptr_t g_item_container_mapper = 0;
uintptr_t g_item_inventory_owner_lookup = 0;
SafetyHookMid g_item_plan_hook{}; // Legacy profiles only; never co-enabled with the current preplanner.
SafetyHookInline g_item_preplan_hook{};
DWORD g_item_plan_tls = TLS_OUT_OF_INDEXES;
SafetyHookInline g_item_move_parent_hook{};
DWORD g_item_move_tls = TLS_OUT_OF_INDEXES;
uintptr_t g_item_move_caller = 0,g_item_move_plan_caller = 0;
using NativeMoveParent=uintptr_t(__fastcall*)(uintptr_t,uint32_t*,uintptr_t,uintptr_t,uint16_t,uintptr_t,uint8_t,uintptr_t);
struct MoveScope {uintptr_t parent=0,player=0,source=0,destination=0,requests=0,data=0;uint32_t count=0;uint16_t container=0xFFFF;bool active=true;};
std::atomic<uint32_t> g_item_plan_samples{0};
std::atomic<uint32_t> g_item_receipt_samples{0};
SafetyHookInline g_item_push_hook{};
DWORD g_item_scope_tls = TLS_OUT_OF_INDEXES;

struct ItemReceiptScope {
    const void* receipt = nullptr;
    int64_t amount = 0;
    bool used = false;
    const char* reason = "invalid-push";
    uintptr_t caller_rva = 0;
    uintptr_t parent_table_rva = 0;
    uintptr_t push_table_rva = 0;
    uintptr_t source_actor = 0;
    uintptr_t destination_actor = 0;
    uint32_t source_count = 0;
    uint32_t destination_count = 0;
    uint16_t mode = 0xFFFF;
    uint16_t source_inventory = 0xFFFF;
    uint16_t destination_inventory = 0xFFFF;
    uint16_t source_container = 0xFFFF;
    bool standalone = false;
    bool timing_transfer = false;
    uint64_t timing_push_id = 0;
    uint64_t timing_epoch = 0;
};

uintptr_t NativeRva(uintptr_t address, uintptr_t base) {
    __try {
        if (!base || address < base) return 0;
        const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        return address - base < nt->OptionalHeader.SizeOfImage ? address - base : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

uintptr_t NativeTableRva(uintptr_t object, uintptr_t base) {
    __try {
        return NativeRva(*reinterpret_cast<const uintptr_t*>(object), base);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

template <size_t N>
bool HasNativeType(uintptr_t object, uintptr_t base, const char (&name)[N]) {
    __try {
        if (!base || object < kMinimumPointerAddress) return false;
        const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        const auto size = nt->OptionalHeader.SizeOfImage;
        const auto inside = [base, size](uintptr_t p, size_t n) {
            return p >= base && n <= size && p - base <= size - n;
        };
        const auto table = *reinterpret_cast<const uintptr_t*>(object);
        if (table < sizeof(uintptr_t) || !inside(table - 8, 16)) return false;
        const auto col = *reinterpret_cast<const uintptr_t*>(table - 8);
        if (!inside(col, 24) || *reinterpret_cast<const uint32_t*>(col) != 1 ||
            *reinterpret_cast<const uint32_t*>(col + 4) != 0) return false;
        const auto type = base + *reinterpret_cast<const uint32_t*>(col + 12) + 16;
        return inside(type, N) && std::memcmp(reinterpret_cast<const void*>(type), name, N) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool ReadMoveScope(MoveScope& scope,uintptr_t parent,uintptr_t player,uintptr_t source,uintptr_t destination,
                   uint16_t container,uintptr_t requests,uintptr_t base,uintptr_t caller){
 if(!caller||caller!=g_item_move_caller||player<kMinimumPointerAddress||source!=player||
    destination<kMinimumPointerAddress||destination==player||container==0xFFFF||
    requests<kMinimumPointerAddress||!HasNativeType(parent,base,".?AVMoveItemBehavior_InventoryToOtherInventory@pa@@"))return false;
 __try {
  const auto data=*reinterpret_cast<const uintptr_t*>(requests);const auto count=*reinterpret_cast<const uint32_t*>(requests+8);
  if(data<kMinimumPointerAddress||!count||count>256)return false;
  scope={parent,player,source,destination,requests,data,count,container};return true;
 }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
uintptr_t RunMoveParent(uintptr_t parent,uint32_t* error,uintptr_t source,uintptr_t destination,uint16_t container,
                        uintptr_t requests,uint8_t option,uintptr_t context,uintptr_t base,uintptr_t player,
                        uintptr_t caller,NativeMoveParent native){
 if(g_item_move_tls==TLS_OUT_OF_INDEXES)return native(parent,error,source,destination,container,requests,option,context);
 const auto incomingError=GetLastError();const auto previous=TlsGetValue(g_item_move_tls);auto* previousScope=static_cast<MoveScope*>(previous);const bool previousActive=previousScope&&previousScope->active;if(previousScope)previousScope->active=false;MoveScope scope{};
 const bool valid=ReadMoveScope(scope,parent,player,source,destination,container,requests,base,caller);
 // Shadow an outer token even for an unqualified nested parent call.
 const bool published=TlsSetValue(g_item_move_tls,valid?&scope:nullptr)!=0;
 uintptr_t result=0;
 __try {SetLastError(incomingError);result=native(parent,error,source,destination,container,requests,option,context);}
 __finally {const auto nativeError=GetLastError();if(published)TlsSetValue(g_item_move_tls,previous);if(previousScope)previousScope->active=previousActive;SetLastError(nativeError);}
 return result;
}
__declspec(noinline) uintptr_t __fastcall ItemMoveParentCallback(uintptr_t parent,uint32_t* error,uintptr_t source,
 uintptr_t destination,uint16_t container,uintptr_t requests,uint8_t option,uintptr_t context){
 const auto incomingError=GetLastError();
 const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(L"CrimsonDesert.exe"));
 const auto player=GetTrackedPlayerActor();const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
 const auto native=g_item_move_parent_hook.original<NativeMoveParent>();SetLastError(incomingError);
 return RunMoveParent(parent,error,source,destination,container,requests,option,context,base,player,caller,native);
}

bool ReadSourceContainer(uintptr_t table_global, uint16_t key, uint16_t* container) {
    // Match ItemPopInventory's metadata-key -> canonical container lookup.
    // Receipt +C8 is a routing discriminator, not the container's +10 ID.
    const bool current = g_item_metadata_profile.kind == ItemMetadataKind::PointerRcx3663;
    const uintptr_t bucket_count_offset = current ? 0x50 : 0x68;
    const uintptr_t entry_count_offset = current ? 0x54 : 0x6C;
    const uintptr_t buckets_offset = current ? 0x60 : 0x78;
    const uintptr_t rows_offset = current ? 0x68 : 0x80;
    __try {
        if (table_global < kMinimumPointerAddress || key == 0xFFFF) return false;
        const auto table = *reinterpret_cast<const uintptr_t*>(table_global);
        if (table < kMinimumPointerAddress) return false;
        const auto buckets = *reinterpret_cast<const uint32_t*>(table + bucket_count_offset);
        if (!buckets || buckets > 0x10000 || !*reinterpret_cast<const uint32_t*>(table + entry_count_offset)) return false;
        const auto storage = *reinterpret_cast<const uintptr_t*>(table + buckets_offset);
        const auto entries = *reinterpret_cast<const uintptr_t*>(table + rows_offset);
        if (storage < kMinimumPointerAddress || entries < kMinimumPointerAddress) return false;
        const auto bucket = storage + (key % buckets) * uintptr_t{0x100};
        const auto count = *reinterpret_cast<const uint32_t*>(bucket);
        if (count > 31) return false;
        for (uint32_t i = 0; i < count; ++i) {
            if (*reinterpret_cast<const uint32_t*>(bucket + 8 + i * 8) != key) continue;
            const auto index = *reinterpret_cast<const uint32_t*>(bucket + 12 + i * 8);
            if (index > 0x100000) return false;
            const auto entry = *reinterpret_cast<const uintptr_t*>(entries + uintptr_t{index} * 8);
            if (entry < kMinimumPointerAddress || *reinterpret_cast<const uint16_t*>(entry + 4) != key) return false;
            *container = *reinterpret_cast<const uint16_t*>(entry + 6);
            return *container != 0xFFFF;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}

#ifdef PSM_ITEM_RECEIPT_TESTING
uintptr_t g_item_timing_test_base=0,g_item_timing_test_player=0;
#endif
ItemReceiptScope ReadSingleStorageScope(uintptr_t push, uintptr_t base, uintptr_t caller = 0,
                                       uintptr_t standalone_caller = 0, uintptr_t table_global = 0) {
    ItemReceiptScope scope{};
    if (push < kMinimumPointerAddress + 0x98) return scope;
    scope.standalone = caller && caller == standalone_caller;
    const auto parent = push - 0x98;
    const auto pop = scope.standalone ? push + 0x80 : parent + 0x30;
    scope.parent_table_rva = NativeTableRva(parent, base);
    scope.push_table_rva = NativeTableRva(push, base);
    const bool parent_type = scope.standalone || HasNativeType(parent, base, ".?AVMoveItemBehavior_InventoryToOtherInventory@pa@@");
    const bool pop_type = HasNativeType(pop, base, ".?AVItemPopInventory@pa@@");
    const bool push_type = HasNativeType(push, base, ".?AVItemPushInventory@pa@@");
    __try {
        scope.reason = "scope-read-exception";
        // Diagnostic reads stay within individually type-checked native objects.
        if (push_type) {
            scope.destination_actor = *reinterpret_cast<const uintptr_t*>(push + 0x10);
            scope.destination_count = *reinterpret_cast<const uint32_t*>(push + 0x60);
            scope.mode = *reinterpret_cast<const uint16_t*>(push + 0x68);
            const auto record = *reinterpret_cast<const uintptr_t*>(push + 0x58);
            if (scope.destination_count && record >= kMinimumPointerAddress)
                scope.destination_inventory = *reinterpret_cast<const uint16_t*>(record + 0xC8);
        }
        if (parent_type && pop_type) {
            scope.source_actor = *reinterpret_cast<const uintptr_t*>(pop + 0x10);
            scope.source_count = *reinterpret_cast<const uint32_t*>(pop + 0x20);
            const auto record = *reinterpret_cast<const uintptr_t*>(pop + 0x18);
            if (scope.source_count && record >= kMinimumPointerAddress)
                scope.source_inventory = *reinterpret_cast<const uint16_t*>(record + 0xC8);
        }
        if (!parent_type) { scope.reason = "parent-type"; return scope; }
        if (!pop_type) { scope.reason = "pop-type"; return scope; }
        if (!push_type) { scope.reason = "push-type"; return scope; }
        if (scope.source_actor < kMinimumPointerAddress || scope.destination_actor < kMinimumPointerAddress) {
            scope.reason = "actor-pointer"; return scope;
        }
        if (!scope.standalone && scope.source_actor == scope.destination_actor) { scope.reason = "same-actor"; return scope; }
        if (scope.standalone ? (scope.mode != 0x37 && scope.mode != 0x38) : scope.mode != 0x18) {
            scope.reason = "transfer-mode"; return scope;
        }
        scope.timing_transfer = true; // all native types/actors/mode above were validated
        if (scope.source_count != 1) { scope.reason = "source-count"; return scope; }
        if (scope.destination_count != 1) { scope.reason = "destination-count"; return scope; }
        const auto source = *reinterpret_cast<const uintptr_t*>(pop + 0x18);
        const auto destination = *reinterpret_cast<const uintptr_t*>(push + 0x58);
        if (source < kMinimumPointerAddress || destination < kMinimumPointerAddress) {
            scope.reason = "receipt-pointer"; return scope;
        }
        if (*reinterpret_cast<const uint16_t*>(source + 8) != *reinterpret_cast<const uint16_t*>(destination + 8)) {
            scope.reason = "item-mismatch"; return scope;
        }
        const auto amount = *reinterpret_cast<const int64_t*>(destination + 0x10);
        if (amount <= 0) { scope.reason = "nonpositive-quantity"; return scope; }
        if (!scope.standalone && amount != *reinterpret_cast<const int64_t*>(source + 0x10)) {
            scope.reason = "quantity-mismatch"; return scope;
        }
        if (scope.standalone && !ReadSourceContainer(table_global,
            *reinterpret_cast<const uint16_t*>(source + 0xDA), &scope.source_container)) {
            scope.reason = "source-container-unresolved"; return scope;
        }
        scope.receipt = reinterpret_cast<const void*>(destination);
        scope.amount = *reinterpret_cast<const int64_t*>(source + 0x10);
        scope.reason = "qualified";
    } __except (EXCEPTION_EXECUTE_HANDLER) { scope.reason = "scope-read-exception"; }
    return scope;
}

__declspec(noinline) uintptr_t __fastcall ItemPushCallback(uintptr_t push, uint32_t* error) {
    const auto incoming_error=GetLastError();
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    auto timing=psm::item_timing::Begin(psm::item_timing::Kind::Push,push,caller);
    auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(L"CrimsonDesert.exe"));
    uintptr_t player=0;
#ifdef PSM_ITEM_RECEIPT_TESTING
    if(g_item_timing_test_base){base=g_item_timing_test_base;player=g_item_timing_test_player;}
    else
#endif
    if(g_state_mutex.try_lock()){player=g_player_resolve.actor;g_state_mutex.unlock();}
    // A contended authoritative snapshot leaves this diagnostic unqualified.
    auto scope = ReadSingleStorageScope(push, base, caller, g_item_standalone_caller, g_inventory_type_table_global);
    scope.caller_rva = NativeRva(caller, base);
    const bool eligible=scope.timing_transfer&&scope.source_count&&scope.destination_count&&
        player>=kMinimumPointerAddress&&(scope.source_actor==player||scope.destination_actor==player);
    if(!eligible)timing.active=false;
    scope.timing_push_id=timing.active?timing.sample.id:0;
    scope.timing_epoch=timing.active?timing.sample.epoch:0;
    const auto previous = TlsGetValue(g_item_scope_tls);
    auto* previous_scope=static_cast<ItemReceiptScope*>(previous);
    const auto previous_timing=previous_scope?previous_scope->timing_push_id:0;
    if(previous_scope)previous_scope->timing_push_id=0;
    const bool published=TlsSetValue(g_item_scope_tls,&scope)!=0;
    uintptr_t result=0;bool completed=false;
    __try {
        psm::item_timing::Mark(timing,psm::item_timing::Phase::NativeBegin);
        SetLastError(incoming_error);
        result=g_item_push_hook.unsafe_call<uintptr_t>(push,error);completed=true;
        psm::item_timing::Mark(timing,psm::item_timing::Phase::NativeEnd);
    } __finally {
        const auto native_error=GetLastError();
        if(published)TlsSetValue(g_item_scope_tls,previous);
        if(previous_scope)previous_scope->timing_push_id=previous_timing;
        psm::item_timing::End(timing,false,completed,AbnormalTermination()!=0,result);
        SetLastError(native_error);
    }
    return result;
}

bool ReadReceiptIdentity(const void* incoming, uint16_t* key, int64_t* quantity) {
    if (reinterpret_cast<uintptr_t>(incoming) < kMinimumPointerAddress) return false;
    __try {
        const auto bytes = static_cast<const std::byte*>(incoming);
        std::memcpy(key, bytes + 8, sizeof(*key));
        std::memcpy(quantity, bytes + 0x10, sizeof(*quantity));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool ReadContainerQuantity(uintptr_t container, uint16_t slot, uint16_t key, int64_t* quantity) {
    if (container < kMinimumPointerAddress || slot >= 0x8000) return false;
    __try {
        const auto count = *reinterpret_cast<const int16_t*>(container + 8);
        if (count < 0) return false;
        *quantity = 0;
        if (slot >= count) return true;
        const auto entries = *reinterpret_cast<const uintptr_t*>(container);
        uint16_t current_key = 0xFFFF;
        if (!ReadReceiptIdentity(reinterpret_cast<const void*>(entries + uintptr_t{slot} * 0xC8),
                                 &current_key, quantity)) return false;
        if (current_key == 0xFFFF || *quantity == 0) { *quantity = 0; return true; }
        return current_key == key;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool ReadNativeReceipt(uintptr_t container, const void* incoming, uint16_t slot,
                       psm::items::Receipt* copy, int64_t* current, int64_t* stack_size,
                       const char*& reason) {
    reason = "container-pointer";
    if (container < kMinimumPointerAddress) return false;
    reason = "incoming-pointer";
    if (reinterpret_cast<uintptr_t>(incoming) < kMinimumPointerAddress) return false;
    reason = "invalid-slot";
    if (slot >= 0x8000) return false;
    __try {
        std::memcpy(copy->data(), incoming, copy->size());
        uint16_t key = 0;
        std::memcpy(&key, copy->data() + 8, sizeof(key));
        if (key == 0xFFFF) { reason = "invalid-item-key"; return false; }
        const auto info = psm::items::LookupMetadata(g_item_info_lookup,key,g_item_metadata_profile);
        if (info < kMinimumPointerAddress) { reason = "metadata-pointer"; return false; }
        psm::items::MetadataSnapshot metadata{};
        if (!psm::items::ReadMetadataSnapshot(info,g_item_metadata_profile,metadata)) { reason = "metadata-profile"; return false; }
        *stack_size = metadata.stack_size;
        // Singletons and weighted metadata need the game's full transaction planner.
        if (*stack_size <= 1) { reason = "nonstackable-item"; return false; }
        if (metadata.weighted != 0) { reason = "weighted-item"; return false; }
        const auto count = *reinterpret_cast<const int16_t*>(container + 8);
        if (count < 0) { reason = "invalid-container-count"; return false; }
        *current = 0;
        if (slot < count) {
            const auto entries = *reinterpret_cast<const uintptr_t*>(container);
            const auto entry = entries + static_cast<uintptr_t>(slot) * 0xC8;
            const auto old_key = *reinterpret_cast<const uint16_t*>(entry + 8);
            const auto old_amount = *reinterpret_cast<const int64_t*>(entry + 0x10);
            if (old_key != 0xFFFF && old_amount > 0) {
                if (old_key != key) { reason = "destination-item-mismatch"; return false; }
                *current = old_amount;
            }
        }
        reason = "readable";
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        reason = "receipt-read-exception";
        return false;
    }
}

bool IsCrossContainer(const ItemReceiptScope& scope, uintptr_t container, const char*& reason) {
    if (!scope.standalone) return true;
    __try {
        if (container < kMinimumPointerAddress) { reason = "container-pointer"; return false; }
        const auto destination = *reinterpret_cast<const uint16_t*>(container + 0x10);
        if (destination == 0xFFFF) { reason = "destination-container-unresolved"; return false; }
        if (scope.source_actor == scope.destination_actor && scope.source_container == destination) {
            reason = "same-container"; return false;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { reason = "container-read-exception"; return false; }
}

bool PrepareStorageTransaction(uintptr_t push, uintptr_t owner, uintptr_t base,
                               uintptr_t caller, uintptr_t valid_caller,
                               uintptr_t mapper, const char*& reason,
                               int64_t& requested, int64_t& delivered) {
    reason = "plan-caller";
    requested = delivered = 0;
    if (!valid_caller || caller != valid_caller) return false;
    reason = "plan-object-type";
    if (!HasNativeType(push, base, ".?AVItemPushInventory@pa@@") ||
        !HasNativeType(push + 0x80, base, ".?AVItemPopInventory@pa@@")) return false;
    __try {
        const auto pop = push + 0x80;
        reason = "plan-mode";
        const auto mode = *reinterpret_cast<const uint16_t*>(push + 0x68);
        if (mode != 0x37 && mode != 0x38) return false;
        reason = "plan-count";
        if (*reinterpret_cast<const uint32_t*>(pop + 0x58) != 1 ||
            *reinterpret_cast<const uint32_t*>(push + 0x50) != 1 ||
            *reinterpret_cast<const uint32_t*>(push + 0x60) != 1) return false;
        const auto source = *reinterpret_cast<const uintptr_t*>(pop + 0x50);
        const auto destination = *reinterpret_cast<const uintptr_t*>(push + 0x58);
        const auto from_actor = *reinterpret_cast<const uintptr_t*>(pop + 0x10);
        const auto to_actor = *reinterpret_cast<const uintptr_t*>(push + 0x10);
        reason = "plan-pointer";
        if (source < kMinimumPointerAddress || destination < kMinimumPointerAddress ||
            owner < kMinimumPointerAddress || from_actor < kMinimumPointerAddress ||
            to_actor < kMinimumPointerAddress || !mapper) return false;
        reason = "plan-record-alias";
        if (source <= destination ? destination - source < 0x28 : source - destination < 0xE0) return false;
        reason = "plan-item";
        const auto key = *reinterpret_cast<const uint16_t*>(destination + 8);
        if (key == 0xFFFF || key != *reinterpret_cast<const uint16_t*>(source)) return false;
        requested = *reinterpret_cast<const int64_t*>(source + 0x18);
        delivered = *reinterpret_cast<const int64_t*>(destination + 0x10);
        reason = "plan-quantity-changed";
        if (requested <= 0 || delivered != requested) return false;
        uint16_t destination_id = 0xFFFF;
        const auto route = *reinterpret_cast<const uint16_t*>(destination + 0xC8);
        reinterpret_cast<uintptr_t(__fastcall*)(uint16_t*, uint16_t, uint16_t)>(mapper)(
            &destination_id, route, *reinterpret_cast<const uint16_t*>(destination + 0xCA));
        const auto source_id = *reinterpret_cast<const uint16_t*>(source + 0x10);
        reason = "plan-container-id";
        if (source_id == 0xFFFF || destination_id == 0xFFFF) return false;
        reason = "same-container";
        if (from_actor == to_actor && source_id == destination_id) return false;
        const auto count = *reinterpret_cast<const uint32_t*>(owner + 0x20);
        const auto containers = *reinterpret_cast<const uintptr_t*>(owner + 0x18);
        reason = "plan-container-list";
        if (!count || count > 256 || containers < kMinimumPointerAddress) return false;
        uintptr_t container = 0;
        for (uint32_t i = 0; i < count; ++i) {
            const auto candidate = *reinterpret_cast<const uintptr_t*>(containers + i * sizeof(uintptr_t));
            if (candidate < kMinimumPointerAddress) return false;
            if (*reinterpret_cast<const uint16_t*>(candidate + 0x10) != destination_id) continue;
            if (container) { reason = "plan-container-ambiguous"; return false; }
            container = candidate;
        }
        psm::items::Receipt original{}, adjusted{};
        int64_t current = 0, stack_size = 0;
        const auto slot = *reinterpret_cast<const uint16_t*>(destination + 0xD8);
        if (!ReadNativeReceipt(container, reinterpret_cast<const void*>(destination), slot,
                               &original, &current, &stack_size, reason)) return false;
        // ItemPushInventory can absorb receipts into an outstanding item debt.
        // Leave that native path unchanged instead of promising a delivered bonus.
        const auto debts_count = *reinterpret_cast<const uint32_t*>(owner + 0x100);
        reason = "plan-debt-state";
        if (debts_count > 0x10000) return false;
        if (debts_count) {
            const auto debts = *reinterpret_cast<const uintptr_t*>(owner + 0xF8);
            if (debts < kMinimumPointerAddress) return false;
            const auto info = psm::items::LookupMetadata(g_item_info_lookup,key,g_item_metadata_profile);
            if (info < kMinimumPointerAddress) return false;
            psm::items::MetadataSnapshot metadata{};
            if (!psm::items::ReadMetadataSnapshot(info,g_item_metadata_profile,metadata)) return false;
            const auto item_id = metadata.item_id;
            for (uint32_t i = 0; i < debts_count; ++i)
                if (*reinterpret_cast<const uint32_t*>(debts + uintptr_t{i} * 0x10) == item_id) return false;
        }
        if (!psm::items::PrepareReceipt(original, current, adjusted, &reason)) return false;
        const auto target = psm::items::Quantity(adjusted);
        reason = "planned-occupancy";
        if (!psm::items::FitsPlannedOccupancy(current, requested, target, stack_size)) return false;
        reason = "plan-unchanged";
        if (target == requested) return false;
        // This is the game's destination transaction receipt, before SQL rows
        // are generated. The later native write AND 0x8F0 result consume it.
        // Source request, source removal, metadata and inventory storage are untouched.
        std::memcpy(reinterpret_cast<void*>(destination + 0x10), &target, sizeof(target));
        delivered = target;
        reason = "plan-scaled";
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { reason = "plan-read-exception"; return false; }
}

// The exact current Plan profile consumes push+48 as borrowed 0x18 groups:
// canonical container key +0, C8 array +8, count +10, capacity +14. Native
// DestinationGroup deep-copies each input before producing owned E0 receipts.
// Private input arrays are never native containers, SQL rows or client state.
struct BorrowedItemPlan {
    void* allocation = nullptr;
    uintptr_t original_groups = 0;
    uintptr_t groups = 0;
    uint32_t scaled_items = 0;
    bool timing_transfer = false;
    const char* reason = "plan-unqualified";
};
struct ActiveItemPlan { uintptr_t push; ActiveItemPlan* previous; };
using NativeItemPlanner = uintptr_t(__fastcall*)(uintptr_t, uint32_t*);

// Current native capacity sums existing quantities and incoming quantities in
// signed qwords, divides by the stack cap, then narrows slot deltas to WORD.
// Validate that arithmetic domain before exposing any enlarged private request.
struct NativeGainDomain {
    uint32_t item_id = 0;
    int64_t incoming = 0;
    int64_t existing = 0;
    int64_t minimum_capacity = 0;
};

bool ValidateNativeGainDomains(const BorrowedItemPlan& plan, uint32_t groups_count,
                               uintptr_t destination, const char*& reason,uintptr_t source_actor=0,uintptr_t sources=0,uint32_t source_count=0) {
    constexpr uint64_t kNativeSignedSlotLimit = std::numeric_limits<int16_t>::max();
    const auto positive_sum = [](int64_t& sum, int64_t amount) {
        if (amount < 0 || amount > std::numeric_limits<int64_t>::max() - sum) return false;
        sum += amount; return true;
    };
    const auto slots = [](int64_t amount, int64_t capacity) -> uint64_t {
        return static_cast<uint64_t>(amount / capacity) + (amount % capacity != 0);
    };
    reason = "plan-domain-owner";
    if (!g_item_inventory_owner_lookup) return false;
    __try {
        // Same exact actor->inventory-owner resolver that native Plan calls.
        const auto owner = reinterpret_cast<uintptr_t(__fastcall*)(uintptr_t)>(
            g_item_inventory_owner_lookup)(destination);
        if (owner < kMinimumPointerAddress) return false;
        const auto containers = *reinterpret_cast<const uintptr_t*>(owner + 0x18);
        const auto containers_count = *reinterpret_cast<const uint32_t*>(owner + 0x20);
        if (!containers_count || containers_count > 256 || containers < kMinimumPointerAddress) return false;
        for (uint32_t g = 0; g < groups_count; ++g) {
            const auto container_key = *reinterpret_cast<const uint16_t*>(plan.groups + uintptr_t{g} * 0x18);
            bool visited = false;
            for (uint32_t h = 0; h < g; ++h)
                if (*reinterpret_cast<const uint16_t*>(plan.groups + uintptr_t{h} * 0x18) == container_key) visited = true;
            if (visited) continue;
            uintptr_t container = 0;
            reason = "plan-domain-container";
            for (uint32_t i = 0; i < containers_count; ++i) {
                const auto candidate = *reinterpret_cast<const uintptr_t*>(containers + uintptr_t{i} * 8);
                if (candidate < kMinimumPointerAddress) return false;
                if (*reinterpret_cast<const uint16_t*>(candidate + 0x10) != container_key) continue;
                if (container) return false;
                container = candidate;
            }
            if (!container) return false;
            if(source_actor){
                // Distinct actors may proxy one effective inventory. Reject a
                // reorder of the same actual container, independent of actor IDs.
                reason="plan-transfer-container-alias";
                const auto source_owner=reinterpret_cast<uintptr_t(__fastcall*)(uintptr_t)>(g_item_inventory_owner_lookup)(source_actor);
                if(source_owner<kMinimumPointerAddress)return false;
                const auto source_containers=*reinterpret_cast<const uintptr_t*>(source_owner+0x18);
                const auto source_containers_count=*reinterpret_cast<const uint32_t*>(source_owner+0x20);
                if(!source_containers_count||source_containers_count>256||source_containers<kMinimumPointerAddress)return false;
                for(uint32_t j=0;j<source_count;++j){
                    const auto source_key=*reinterpret_cast<const uint16_t*>(sources+uintptr_t{j}*0x28+0x10);
                    uintptr_t source_container=0;
                    for(uint32_t k=0;k<source_containers_count;++k){
                        const auto candidate=*reinterpret_cast<const uintptr_t*>(source_containers+uintptr_t{k}*8);
                        if(candidate<kMinimumPointerAddress)return false;
                        if(*reinterpret_cast<const uint16_t*>(candidate+0x10)==source_key){
                            if(source_container)return false;source_container=candidate;
                        }
                    }
                    if(!source_container||source_container==container)return false;
                }
            }
            // The underlying vector uses full DWORD count/capacity. Native
            // callers later consume signed WORD slots; never accept high-word aliases.
            const auto existing_count = *reinterpret_cast<const uint32_t*>(container + 8);
            const auto existing_capacity = *reinterpret_cast<const uint32_t*>(container + 0xC);
            const auto existing_items = *reinterpret_cast<const uintptr_t*>(container);
            reason = "plan-domain-vector";
            if (existing_count > kNativeSignedSlotLimit || existing_capacity < existing_count ||
                (existing_count && existing_items < kMinimumPointerAddress)) return false;
            std::array<NativeGainDomain,256> domains{};
            uint32_t domain_count = 0;
            uint64_t incoming_slots_upper = 0;
            for (uint32_t h = 0; h < groups_count; ++h) {
                const auto group = plan.groups + uintptr_t{h} * 0x18;
                if (*reinterpret_cast<const uint16_t*>(group) != container_key) continue;
                const auto items = *reinterpret_cast<const uintptr_t*>(group + 8);
                const auto count = *reinterpret_cast<const uint32_t*>(group + 0x10);
                for (uint32_t i = 0; i < count; ++i) {
                    const auto item = items + uintptr_t{i} * 0xC8;
                    const auto amount = *reinterpret_cast<const int64_t*>(item + 0x10);
                    if (amount <= 0) continue; // Unchanged native removal/error path.
                    const auto key = *reinterpret_cast<const uint16_t*>(item + 8);
                    const auto info = psm::items::LookupMetadata(g_item_info_lookup,key,g_item_metadata_profile);
                    psm::items::MetadataSnapshot metadata{};
                    reason = "plan-domain-metadata";
                    if (key == 0xFFFF || info < kMinimumPointerAddress ||
                        !psm::items::ReadMetadataSnapshot(info,g_item_metadata_profile,metadata)) return false;
                    // Zero/negative raw capacities are not a valid positive divisor.
                    // Native special cases do not guarantee an override for them.
                    if (metadata.stack_size <= 0) return false;
                    // For >1, exact native Capacity returns this field unchanged.
                    // Its <=1 special cases can only increase capacity; one is
                    // a conservative lower bound for unchanged singleton inputs.
                    const auto capacity = metadata.stack_size > 1 ? metadata.stack_size : int64_t{1};
                    uint32_t d = 0;
                    while (d < domain_count && domains[d].item_id != metadata.item_id) ++d;
                    if (d == domain_count) {
                        if (domain_count == domains.size()) return false;
                        domains[d].item_id = metadata.item_id;
                        domains[d].minimum_capacity = capacity;
                        ++domain_count;
                    }
                    if (capacity < domains[d].minimum_capacity) domains[d].minimum_capacity = capacity;
                    reason = "plan-domain-incoming-overflow";
                    if (!positive_sum(domains[d].incoming,amount)) return false;
                    const auto added_slots = slots(amount,capacity);
                    reason = "plan-domain-slot-sum";
                    if (added_slots > kNativeSignedSlotLimit - incoming_slots_upper) return false;
                    incoming_slots_upper += added_slots;
                }
            }
            reason = "plan-domain-slot-headroom";
            if (incoming_slots_upper > kNativeSignedSlotLimit - existing_count) return false;
            for (uint32_t i = 0; i < existing_count; ++i) {
                const auto item = existing_items + uintptr_t{i} * 0xC8;
                const auto key = *reinterpret_cast<const uint16_t*>(item + 8);
                const auto amount = *reinterpret_cast<const int64_t*>(item + 0x10);
                if (key == 0xFFFF || amount <= 0) continue;
                const auto info = psm::items::LookupMetadata(g_item_info_lookup,key,g_item_metadata_profile);
                psm::items::MetadataSnapshot metadata{};
                reason = "plan-domain-existing-metadata";
                if (info < kMinimumPointerAddress ||
                    !psm::items::ReadMetadataSnapshot(info,g_item_metadata_profile,metadata)) return false;
                for (uint32_t d = 0; d < domain_count; ++d) if (domains[d].item_id == metadata.item_id) {
                    reason = "plan-domain-existing-overflow";
                    if (!positive_sum(domains[d].existing,amount)) return false;
                }
            }
            for (uint32_t d = 0; d < domain_count; ++d) {
                auto combined = domains[d].existing;
                reason = "plan-domain-combined-overflow";
                if (!positive_sum(combined,domains[d].incoming)) return false;
                reason = "plan-domain-quotient";
                if (slots(combined,domains[d].minimum_capacity) > kNativeSignedSlotLimit) return false;
            }
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { reason = "plan-domain-read-exception"; return false; }
}

bool PrepareBorrowedItemPlan(uintptr_t push, uintptr_t base, uintptr_t player,
                            uintptr_t caller, uintptr_t standalone_caller,
                            double multiplier, BorrowedItemPlan& plan) {
    plan.reason = "plan-player";
    if (player < kMinimumPointerAddress || !std::isfinite(multiplier) || multiplier <= 0 || multiplier == 1)
        return false;
    plan.reason = "plan-object-type";
    if (!HasNativeType(push, base, ".?AVItemPushInventory@pa@@")) return false;
    __try {
        const auto destination_actor=*reinterpret_cast<const uintptr_t*>(push+0x10);
        const bool external=destination_actor!=player;
        if(destination_actor<kMinimumPointerAddress){plan.reason="plan-destination";return false;}
        // A nonempty result means this transaction already has a native plan.
        // Never scale committed results or invoke a second destination plan.
        if (*reinterpret_cast<const uint32_t*>(push + 0x60)) {
            plan.reason = "plan-already-built"; return false;
        }
        const auto count = *reinterpret_cast<const uint32_t*>(push + 0x50);
        const auto groups = *reinterpret_cast<const uintptr_t*>(push + 0x48);
        plan.reason = "plan-group-bounds";
        if (!count || count > 64 || groups < kMinimumPointerAddress) return false;
        uintptr_t pop = 0;MoveScope* move=nullptr;
        const auto mode = *reinterpret_cast<const uint16_t*>(push + 0x68);
        if (mode == 0x37 || mode == 0x38) {
            plan.reason = "plan-transfer-caller";
            if (!caller || caller != standalone_caller) return false;
            pop = push + 0x80;
        } else if (mode == 0x18) {
            plan.reason = "plan-transfer-parent";
            if (push < kMinimumPointerAddress + 0x98 ||
                !HasNativeType(push - 0x98, base, ".?AVMoveItemBehavior_InventoryToOtherInventory@pa@@")) return false;
            pop = push - 0x68;
            if(external){
                plan.reason="plan-external-fallback-scope";
                move=g_item_move_tls==TLS_OUT_OF_INDEXES?nullptr:static_cast<MoveScope*>(TlsGetValue(g_item_move_tls));
                if(!move||!move->active||caller!=g_item_move_plan_caller||move->parent!=push-0x98||move->player!=player||
                   move->source!=player||move->destination!=destination_actor)return false;
            }
        }
        if(external&&mode!=0x38&&!(mode==0x18&&move)){plan.reason="plan-external-operation";return false;}
        uintptr_t sources = 0; uint32_t source_count = 0; bool same_actor = false;
        if (pop) {
            plan.reason = "plan-source-type";
            if (!HasNativeType(pop, base, ".?AVItemPopInventory@pa@@")) return false;
            const auto source_actor = *reinterpret_cast<const uintptr_t*>(pop + 0x10);
            plan.reason = "plan-source-actor";
            if (source_actor < kMinimumPointerAddress) return false;
            if(external&&source_actor!=player){plan.reason="plan-external-source";return false;}
            same_actor = source_actor == destination_actor;
            plan.timing_transfer = true;
            sources = *reinterpret_cast<const uintptr_t*>(pop + 0x50);
            source_count = *reinterpret_cast<const uint32_t*>(pop + 0x58);
            plan.reason = "plan-source-bounds";
            if (!source_count || source_count > 256 || sources < kMinimumPointerAddress) return false;
            if(move){
                plan.reason="plan-external-fallback-requests";
                if(source_count!=move->count||*reinterpret_cast<const uintptr_t*>(move->requests)!=move->data||
                   *reinterpret_cast<const uint32_t*>(move->requests+8)!=move->count||
                   std::memcmp(reinterpret_cast<const void*>(sources),reinterpret_cast<const void*>(move->data),size_t{source_count}*0x28))return false;
            }
        }
        uint32_t item_count = 0;
        for (uint32_t g = 0; g < count; ++g) {
            const auto group = groups + uintptr_t{g} * 0x18;
            const auto key = *reinterpret_cast<const uint16_t*>(group);
            const auto items = *reinterpret_cast<const uintptr_t*>(group + 8);
            const auto n = *reinterpret_cast<const uint32_t*>(group + 0x10);
            const auto capacity = *reinterpret_cast<const uint32_t*>(group + 0x14);
            plan.reason = "plan-item-bounds";
            if (key == 0xFFFF || !n || n > 256 - item_count || capacity < n ||
                items < kMinimumPointerAddress) return false;
            item_count += n;
            if(move&&key!=move->container){plan.reason="plan-external-fallback-container";return false;}
            if (same_actor) for (uint32_t i = 0; i < source_count; ++i) {
                const auto source_key = *reinterpret_cast<const uint16_t*>(sources + uintptr_t{i} * 0x28 + 0x10);
                plan.reason = "plan-source-container";
                if (source_key == 0xFFFF) return false;
                if (source_key == key) { plan.reason = "same-container"; return false; }
            }
        }
        const auto size = size_t{count} * 0x18 + size_t{item_count} * 0xC8;
        plan.allocation = HeapAlloc(GetProcessHeap(), 0, size);
        plan.reason = "plan-allocation";
        if (!plan.allocation) return false;
        plan.original_groups = groups;
        plan.groups = reinterpret_cast<uintptr_t>(plan.allocation);
        std::memcpy(plan.allocation, reinterpret_cast<const void*>(groups), size_t{count} * 0x18);
        auto next = plan.groups + size_t{count} * 0x18;
        for (uint32_t g = 0; g < count; ++g) {
            const auto group = plan.groups + uintptr_t{g} * 0x18;
            const auto original_items = *reinterpret_cast<const uintptr_t*>(group + 8);
            const auto n = *reinterpret_cast<const uint32_t*>(group + 0x10);
            std::memcpy(reinterpret_cast<void*>(next), reinterpret_cast<const void*>(original_items), size_t{n} * 0xC8);
            *reinterpret_cast<uintptr_t*>(group + 8) = next;
            *reinterpret_cast<uint32_t*>(group + 0x14) = n;
            for (uint32_t i = 0; i < n; ++i) {
                const auto item = next + uintptr_t{i} * 0xC8;
                const auto amount = *reinterpret_cast<const int64_t*>(item + 0x10);
                if (amount <= 0) continue;
                const auto key = *reinterpret_cast<const uint16_t*>(item + 8);
                plan.reason = "plan-item-metadata";
                if (key == 0xFFFF) return false;
                const auto info = psm::items::LookupMetadata(g_item_info_lookup, key, g_item_metadata_profile);
                psm::items::MetadataSnapshot metadata{};
                if (info < kMinimumPointerAddress ||
                    !psm::items::ReadMetadataSnapshot(info, g_item_metadata_profile, metadata)) return false;
                if (metadata.stack_size <= 1) continue;
                if (pop) {
                    // Validate ORIGINAL transfer quantities before applying a
                    // destination-only gain. Never change the pending pop list.
                    int64_t source_total = 0, destination_total = 0;
                    plan.reason = "plan-transfer-quantity";
                    for (uint32_t j = 0; j < source_count; ++j) {
                        const auto source = sources + uintptr_t{j} * 0x28;
                        if (*reinterpret_cast<const uint16_t*>(source) != key) continue;
                        const auto q = *reinterpret_cast<const int64_t*>(source + 0x18);
                        if (q <= 0 || q > std::numeric_limits<int64_t>::max() - source_total) return false;
                        source_total += q;
                    }
                    for (uint32_t h = 0; h < count; ++h) {
                        const auto original_group = groups + uintptr_t{h} * 0x18;
                        const auto original_array = *reinterpret_cast<const uintptr_t*>(original_group + 8);
                        const auto original_count = *reinterpret_cast<const uint32_t*>(original_group + 0x10);
                        for (uint32_t j = 0; j < original_count; ++j) {
                            const auto original_item = original_array + uintptr_t{j} * 0xC8;
                            if (*reinterpret_cast<const uint16_t*>(original_item + 8) != key) continue;
                            const auto q = *reinterpret_cast<const int64_t*>(original_item + 0x10);
                            if (q <= 0 || q > std::numeric_limits<int64_t>::max() - destination_total) return false;
                            destination_total += q;
                        }
                    }
                    if (!source_total || source_total != destination_total) return false;
                }
                // Use one frozen configuration value for the entire transaction.
                // Refuse overflow instead of saturating an enormous native request.
                plan.reason = "plan-quantity-range";
                int64_t target = amount;
                if (!psm::items::TryScaleDecimalQuantity(amount,multiplier,target)) return false;
                if (target == amount) continue;
                *reinterpret_cast<int64_t*>(item + 0x10) = target;
                ++plan.scaled_items;
            }
            next += size_t{n} * 0xC8;
        }
        if (plan.scaled_items && !ValidateNativeGainDomains(plan,count,destination_actor,plan.reason,external?player:0,sources,source_count)) return false;
        plan.reason = plan.scaled_items ? "plan-input-scaled" : "plan-input-unchanged";
        return plan.scaled_items != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { plan.reason = "plan-input-read-exception"; return false; }
}

uintptr_t RunBorrowedItemPlan(uintptr_t push, uint32_t* error, uintptr_t base,
                             uintptr_t player, uintptr_t caller, uintptr_t standalone_caller,
                             NativeItemPlanner native) {
    const auto incoming_error=GetLastError();
    auto timing=psm::item_timing::Begin(psm::item_timing::Kind::Plan,push,caller);
    if (g_item_plan_tls == TLS_OUT_OF_INDEXES) {SetLastError(incoming_error);return native(push,error);}
    auto* previous = static_cast<ActiveItemPlan*>(TlsGetValue(g_item_plan_tls));
    for (auto* active = previous; active; active = active->previous)
        if (active->push == push) {SetLastError(incoming_error);return native(push,error);}
    ActiveItemPlan active{push,previous};
    if (!TlsSetValue(g_item_plan_tls,&active)) {SetLastError(incoming_error);return native(push,error);}
    BorrowedItemPlan plan{};
    bool substituted = false,prepared=false,completed=false;
    uintptr_t result = 0;
    // No C++ owning object crosses this SEH/finally region. Heap ownership and
    // the native group pointer are restored even if the native callback raises.
    __try {
        const auto& config = GetConfig();
        const auto multiplier = config.items.gain_multiplier;
        const bool enabled = g_runtime_enabled.load(std::memory_order_acquire) && config.general.enabled;
        prepared = enabled && PrepareBorrowedItemPlan(push,base,player,caller,
            standalone_caller,multiplier,plan);
        psm::item_timing::Mark(timing,psm::item_timing::Phase::Prepared);
        if (prepared) {
            __try {
                auto** field = reinterpret_cast<void**>(push + 0x48);
                substituted = InterlockedCompareExchangePointer(field,reinterpret_cast<void*>(plan.groups),
                    reinterpret_cast<void*>(plan.original_groups)) == reinterpret_cast<void*>(plan.original_groups);
            } __except (EXCEPTION_EXECUTE_HANDLER) { substituted = false; }
        }
        if (g_item_plan_samples.fetch_add(1,std::memory_order_relaxed) < 96)
            Log("items: preplan reason=%s push=0x%p player=0x%p scaled-items=%u scaled=%d multiplier=%.6g stage=before-native-capacity",
                plan.reason,reinterpret_cast<void*>(push),reinterpret_cast<void*>(player),
                plan.scaled_items,substituted?1:0,multiplier);
        psm::item_timing::Mark(timing,psm::item_timing::Phase::NativeBegin);
        SetLastError(incoming_error);
        result = native(push,error);completed=true;
        psm::item_timing::Mark(timing,psm::item_timing::Phase::NativeEnd);
    } __finally {
        const auto native_error=GetLastError();
        if (substituted) {
            // The validated native Plan only reads +48. CAS also avoids
            // overwriting an unexpected replacement by a nested owner.
            InterlockedCompareExchangePointer(reinterpret_cast<void**>(push + 0x48),
                reinterpret_cast<void*>(plan.original_groups),reinterpret_cast<void*>(plan.groups));
        }
        if (plan.allocation) HeapFree(GetProcessHeap(),0,plan.allocation);
        TlsSetValue(g_item_plan_tls,previous);
        psm::item_timing::End(timing,substituted&&plan.timing_transfer,completed,AbnormalTermination()!=0,result,plan.scaled_items);
        SetLastError(native_error);
    }
    return result;
}

std::atomic<bool> g_item_menu_requests_enabled{true};
std::atomic<uint32_t> g_item_menu_samples{0};
psm::menu_gain512::StartupRetry g_menu_startup{};
psm::menu_gain512::Probe g_menu_startup_probe{};

__declspec(noinline) uintptr_t __fastcall ItemPreplanCallback(uintptr_t push,uint32_t* error) {
    const auto incoming_error=GetLastError();auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(L"CrimsonDesert.exe"));
    auto player=GetTrackedPlayerActor();
#ifdef PSM_ITEM_RECEIPT_TESTING
    if(g_item_timing_test_base){base=g_item_timing_test_base;player=g_item_timing_test_player;}
#endif
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());const auto native=g_item_preplan_hook.original<NativeItemPlanner>();
    SetLastError(incoming_error);return RunBorrowedItemPlan(push,error,base,player,caller,g_item_plan_caller,native);
}

void ItemPlanCallback(SafetyHookContext& ctx) {
    uintptr_t caller = 0;
    __try { caller = *reinterpret_cast<const uintptr_t*>(ctx.rsp + 0x78); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    if (caller != g_item_plan_caller || ctx.r14 != ctx.rdi + 0x58) return;
    const char* reason = "plan-unqualified";
    int64_t requested = 0, delivered = 0;
    const bool scaled = PrepareStorageTransaction(ctx.rdi, ctx.r15,
        reinterpret_cast<uintptr_t>(GetModuleHandleW(L"CrimsonDesert.exe")), caller,
        g_item_plan_caller, g_item_container_mapper, reason, requested, delivered);
    if (g_item_plan_samples.fetch_add(1, std::memory_order_relaxed) < 96)
        Log("items: transaction reason=%s push=0x%p source-requested=%lld destination-planned=%lld scaled=%d stage=before-sql",
            reason, reinterpret_cast<void*>(ctx.rdi), static_cast<long long>(requested),
            static_cast<long long>(delivered), scaled ? 1 : 0);
}

__declspec(noinline) uintptr_t __fastcall ItemReceiptCallback(
    uintptr_t container, uint32_t* error, const void* incoming, uint16_t slot, uint8_t native_flag) {
    const auto incoming_error=GetLastError();
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    auto timing=psm::item_timing::Begin(psm::item_timing::Kind::Receipt,container,caller);
    psm::items::Receipt original{};
    int64_t current = 0, stack_size = 0;
    const bool from_push = caller == g_item_receipt_caller;
    const bool sample = from_push && g_item_receipt_samples.fetch_add(1, std::memory_order_relaxed) < 96;
    uint16_t diagnostic_key = 0xFFFF;
    int64_t diagnostic_quantity = 0, diagnostic_before = 0;
    const bool identity_read = sample && ReadReceiptIdentity(incoming, &diagnostic_key, &diagnostic_quantity);
    const bool before_read = identity_read && ReadContainerQuantity(container, slot, diagnostic_key, &diagnostic_before);
    auto* scope = g_item_scope_tls != TLS_OUT_OF_INDEXES
        ? static_cast<ItemReceiptScope*>(TlsGetValue(g_item_scope_tls)) : nullptr;
    const bool qualified = from_push && scope && scope->receipt == incoming && !scope->used;
    const char* read_reason = "not-qualified";
    const bool readable = qualified && IsCrossContainer(*scope, container, read_reason) &&
        ReadNativeReceipt(container, incoming, slot, &original, &current, &stack_size, read_reason);
    const char* reason = !scope ? "no-push-scope" : !scope->receipt ? scope->reason :
        scope->receipt != incoming ? "receipt-identity" : scope->used ? "already-used" :
        !readable ? read_reason : "native-observed";
    if (qualified) scope->used = true;
    // Observe only. Scaling here leaves the SQL plan and client delta unchanged.
    // Do not serialize native inventory work under SafetyHook's call mutex.
    if(from_push&&scope&&scope->timing_push_id&&scope->timing_epoch==timing.sample.epoch)timing.sample.parent=scope->timing_push_id;
    else timing.active=false;
    uintptr_t result=0;bool completed=false;DWORD native_last_error=incoming_error;
    __try {
        psm::item_timing::Mark(timing,psm::item_timing::Phase::NativeBegin);
        SetLastError(incoming_error);
        result=g_item_receipt_hook.unsafe_call<uintptr_t>(container,error,incoming,slot,native_flag);completed=true;
        native_last_error=GetLastError();
        psm::item_timing::Mark(timing,psm::item_timing::Phase::NativeEnd);
    } __finally {
        if(AbnormalTermination()){
            const auto exception_error=GetLastError();psm::item_timing::End(timing,false,false,true,result);SetLastError(exception_error);
        }
    }
    if (sample) {
        uint32_t native_error = 0xFFFFFFFF;
        int64_t observed = 0;
        const bool after = identity_read && ReadContainerQuantity(container, slot, diagnostic_key, &observed);
        __try { native_error = *error; } __except (EXCEPTION_EXECUTE_HANDLER) {}
        Log("items: receipt reason=%s container=0x%p slot=%u item=%u identity=%d qualified=%d readable=%d requested=%lld delivered=%lld scaled=%d before=%lld before-read=%d after=%lld after-read=%d native_error=%u",
            reason, reinterpret_cast<void*>(container), static_cast<unsigned>(slot), static_cast<unsigned>(diagnostic_key),
            identity_read ? 1 : 0, qualified ? 1 : 0, readable ? 1 : 0,
            static_cast<long long>(diagnostic_quantity),
            static_cast<long long>(diagnostic_quantity),
            0, static_cast<long long>(diagnostic_before), before_read ? 1 : 0,
            static_cast<long long>(observed), after ? 1 : 0, native_error);
        if (scope) {
            Log("items: scope reason=%s push-caller-rva=0x%llX parent-table-rva=0x%llX push-table-rva=0x%llX source-actor=0x%p destination-actor=0x%p source-count=%u destination-count=%u mode=0x%X source-inventory=0x%X destination-inventory=0x%X standalone=%d source-container=0x%X",
                scope->reason, static_cast<unsigned long long>(scope->caller_rva),
                static_cast<unsigned long long>(scope->parent_table_rva), static_cast<unsigned long long>(scope->push_table_rva),
                reinterpret_cast<void*>(scope->source_actor), reinterpret_cast<void*>(scope->destination_actor),
                scope->source_count, scope->destination_count, static_cast<unsigned>(scope->mode),
                static_cast<unsigned>(scope->source_inventory), static_cast<unsigned>(scope->destination_inventory),
                scope->standalone ? 1 : 0, static_cast<unsigned>(scope->source_container));
        }
    }
    psm::item_timing::End(timing,false,completed,false,result);
    SetLastError(native_last_error);
    return result;
}

using MenuPolicyRequest = psm::menu_item_policy_v1::Request;
using MenuPolicyCallback = std::uint32_t(__fastcall*)(const MenuPolicyRequest*, std::int64_t*, void*);
using MenuPolicyVersion = std::uint32_t(__fastcall*)();
using MenuPolicyRegister = std::uint32_t(__fastcall*)(MenuPolicyCallback, void*, std::uint64_t*);

// Registration is process-resident. The broker pins callback code and this
// reference keeps the provider alive. Disable is a callback-local passthrough.
HMODULE g_menu_policy_provider = nullptr;

bool WriteMenuPolicyQuantity(std::int64_t* out, std::int64_t quantity) noexcept {
    if (!out) return false;
    __try { *out = quantity; return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

std::uint32_t __fastcall MenuQuantityPolicyV1(const MenuPolicyRequest* input,
                                               std::int64_t* out, void*) {
    const auto error = GetLastError();
    const auto csr = _mm_getcsr();
    std::uint32_t result = 0;
    try {
        MenuPolicyRequest request{};
        std::int64_t quantity = 0;
        const auto config = GetConfig();
        if (g_runtime_enabled.load(std::memory_order_acquire) && config.general.enabled &&
            g_item_menu_requests_enabled.load(std::memory_order_acquire) &&
            psm::capture_compat::SafeCopy(reinterpret_cast<uintptr_t>(input), &request, sizeof(request)) &&
            psm::menu_item_policy_v1::Quantity(request, config.items.gain_multiplier, quantity) &&
            WriteMenuPolicyQuantity(out, quantity)) {
            result = 1;
            if (g_item_menu_samples.fetch_add(1, std::memory_order_relaxed) < 32)
                Log("items: Menu policy V1 key=%u requested=%lld authored=%lld once before both native plans",
                    unsigned(request.item_key), static_cast<long long>(request.quantity),
                    static_cast<long long>(quantity));
        }
    } catch (...) {
        // No configuration or diagnostic exception may escape the broker callback.
    }
    _mm_setcsr(csr);
    SetLastError(error);
    return result;
}

psm::menu_gain512::State RegisterMenuPolicyV1(HMODULE provider, psm::menu_gain512::Probe& probe) {
    using psm::menu_gain512::State;
    constexpr const char* version_name = "DesertLinkAddItemPolicyVersion";
    constexpr const char* register_name = "DesertLinkAddItemPolicyRegisterV1";
    constexpr const char* unregister_name = "DesertLinkAddItemPolicyUnregisterV1";
    const auto version = GetProcAddress(provider, version_name);
    const auto install = GetProcAddress(provider, register_name);
    const auto remove = GetProcAddress(provider, unregister_name);
    if (!version && !install && !remove) return State::Absent;

    psm::v3446::Image image;
    const auto base = reinterpret_cast<uintptr_t>(provider);
    const auto direct_export = [&](const char* name, FARPROC function) {
        const auto rva = psm::capture_compat::Context::DataExport(image, name);
        return rva && image.Exec(rva) && reinterpret_cast<uintptr_t>(function) == base + rva;
    };
    if (!version || !install || !remove ||
        !image.Init(provider, psm::v3446::ModuleSize(provider)) ||
        !direct_export(version_name, version) || !direct_export(register_name, install) ||
        !direct_export(unregister_name, remove) ||
        reinterpret_cast<MenuPolicyVersion>(version)() != 1) {
        probe = {"add-item-policy-v1-incompatible", 0};
        return State::Unsupported;
    }
    std::uint64_t token = 0;
    const auto status = reinterpret_cast<MenuPolicyRegister>(install)(MenuQuantityPolicyV1, nullptr, &token);
    if (status || !token) {
        probe = {"add-item-policy-v1-declined", status};
        return State::Unsupported;
    }
    g_menu_policy_provider = provider;
    g_item_menu_requests_enabled.store(true, std::memory_order_release);
    probe = {"add-item-policy-v1", 0};
    Log("hooks: Menu Add Item policy V1 registered; one quantity before both native plans");
    return State::Ready;
}

psm::menu_gain512::State TryInstallMenuItemGainBody(psm::menu_gain512::Probe& probe){
    using psm::menu_gain512::State;
    if (g_menu_policy_provider) {
        g_item_menu_requests_enabled.store(true, std::memory_order_release);
        probe = {"add-item-policy-v1", 0};
        return State::Ready;
    }
    HMODULE provider=nullptr;
    if(!GetModuleHandleExW(0,L"DesertLinkModMenu.asi",&provider)){probe={"provider-absent",0};return State::Absent;}
    const auto policy = RegisterMenuPolicyV1(provider, probe);
    if (policy != State::Ready) FreeLibrary(provider);
    if (policy == State::Absent) {
        probe = {"add-item-policy-v1-not-exported", 0};
        return State::Unsupported;
    }
    return policy;
}
psm::menu_gain512::State TryInstallMenuItemGain(psm::menu_gain512::Probe& probe){
    try{return TryInstallMenuItemGainBody(probe);}
    catch(...){
        probe={"startup-exception",0};g_item_menu_requests_enabled.store(false,std::memory_order_release);
        return psm::menu_gain512::State::Unsupported;
    }
}
void StartMenuItemGainStartup(){
    using psm::menu_gain512::State;
    g_item_menu_requests_enabled.store(false,std::memory_order_release);g_menu_startup.Begin(GetTickCount64());
    const auto state=TryInstallMenuItemGain(g_menu_startup_probe);
    if(state==State::Ready)g_item_menu_requests_enabled.store(true,std::memory_order_release);
    if(state==State::Ready||state==State::Unsupported)g_menu_startup.Stop();
    if(state!=State::Ready)Log("hooks: Menu Add Item policy state=%u reason=%s rva=0x%X; %s",unsigned(state),
        g_menu_startup_probe.reason,unsigned(g_menu_startup_probe.rva),
        state==State::Unsupported?"provider keeps original quantity; native item scaling is independent":"bounded30s producer readiness retry pending");
}

#ifdef PLAYER_STATUS_MODIFIER_ECONOMY_ONLY
constexpr bool kEconomyOnlyBuild = true;
#else
constexpr bool kEconomyOnlyBuild = false;
#endif

bool ShouldLogAggressive(std::atomic<std::uint32_t>& counter, const std::uint32_t budget) {
    return counter.fetch_add(1, std::memory_order_acq_rel) < budget;
}

template <typename T>
bool TryReadValue(const uintptr_t address, T* const value) {
    if (address < kMinimumPointerAddress || value == nullptr) {
        return false;
    }

    __try {
        *value = *reinterpret_cast<const T*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void LogItemGainAffinityDiagnostic(SafetyHookContext& ctx,
                                   const ItemGainCaptureKind capture_kind,
                                   const int count,
                                   const bool player_ready) {
    const uintptr_t target = SelectItemGainTargetAddress(
        capture_kind, ctx.rsi, ctx.rcx, ctx.r8, ctx.rdi, ctx.rdx);

    uintptr_t return_address = 0;
    uintptr_t stack_20 = 0;
    uintptr_t stack_28 = 0;
    uintptr_t stack_30 = 0;
    uintptr_t stack_38 = 0;
    uintptr_t stack_40 = 0;
    uintptr_t tramp_20 = 0;
    int64_t q_m10 = 0;
    int64_t q_m08 = 0;
    int64_t q_00 = 0;
    int64_t q_08 = 0;
    int64_t q_10 = 0;
    int64_t q_18 = 0;
    int64_t q_20 = 0;
    std::uint32_t d_m10 = 0;
    std::uint32_t d_m08 = 0;
    std::uint32_t d_00 = 0;
    std::uint32_t d_04 = 0;
    std::uint32_t d_08 = 0;
    std::uint32_t d_0c = 0;
    std::uint32_t d_10 = 0;
    std::uint32_t d_14 = 0;
    std::uint32_t d_18 = 0;
    std::uint32_t d_1c = 0;

    TryReadValue(ctx.rsp + 0x00, &return_address);
    TryReadValue(ctx.rsp + 0x20, &stack_20);
    TryReadValue(ctx.rsp + 0x28, &stack_28);
    TryReadValue(ctx.rsp + 0x30, &stack_30);
    TryReadValue(ctx.rsp + 0x38, &stack_38);
    TryReadValue(ctx.rsp + 0x40, &stack_40);
    TryReadValue(ctx.trampoline_rsp + 0x20, &tramp_20);
    TryReadValue(target - 0x10, &q_m10);
    TryReadValue(target - 0x08, &q_m08);
    TryReadValue(target + 0x00, &q_00);
    TryReadValue(target + 0x08, &q_08);
    TryReadValue(target + 0x10, &q_10);
    TryReadValue(target + 0x18, &q_18);
    TryReadValue(target + 0x20, &q_20);
    TryReadValue(target - 0x10, &d_m10);
    TryReadValue(target - 0x08, &d_m08);
    TryReadValue(target + 0x00, &d_00);
    TryReadValue(target + 0x04, &d_04);
    TryReadValue(target + 0x08, &d_08);
    TryReadValue(target + 0x0C, &d_0c);
    TryReadValue(target + 0x10, &d_10);
    TryReadValue(target + 0x14, &d_14);
    TryReadValue(target + 0x18, &d_18);
    TryReadValue(target + 0x1C, &d_1c);

    const int64_t amount = SelectItemGainAmount(
        capture_kind, static_cast<int64_t>(ctx.rax), static_cast<int64_t>(ctx.rcx));
    Log("hooks: item-gain-affinity diagnostic #%d player_ready=%d target=0x%p base=0x%p offset=0x%p amount=%lld current=%lld projected=%lld rip=0x%p ret=0x%p",
        count,
        player_ready ? 1 : 0,
        reinterpret_cast<void*>(target),
        reinterpret_cast<void*>(ctx.r8),
        reinterpret_cast<void*>(ctx.rdi),
        static_cast<long long>(amount),
        static_cast<long long>(q_00),
        static_cast<long long>(q_00 + amount),
        reinterpret_cast<void*>(ctx.rip),
        reinterpret_cast<void*>(return_address));

    Log("hooks: item-gain-affinity window #%d q_m10=%lld q_m08=%lld q00=%lld q08=%lld q10=%lld q18=%lld q20=%lld d_m10=%u d_m08=%u d00=%u d04=%u d08=%u d0c=%u d10=%u d14=%u d18=%u d1c=%u",
        count,
        static_cast<long long>(q_m10),
        static_cast<long long>(q_m08),
        static_cast<long long>(q_00),
        static_cast<long long>(q_08),
        static_cast<long long>(q_10),
        static_cast<long long>(q_18),
        static_cast<long long>(q_20),
        static_cast<unsigned>(d_m10),
        static_cast<unsigned>(d_m08),
        static_cast<unsigned>(d_00),
        static_cast<unsigned>(d_04),
        static_cast<unsigned>(d_08),
        static_cast<unsigned>(d_0c),
        static_cast<unsigned>(d_10),
        static_cast<unsigned>(d_14),
        static_cast<unsigned>(d_18),
        static_cast<unsigned>(d_1c));

    Log("hooks: item-gain-affinity regs #%d rax=0x%p rbx=0x%p rcx=%lld rdx=0x%p rsi=0x%p rdi=0x%p r8=0x%p r9=0x%p r10=0x%p r11=0x%p r12=0x%p r13=0x%p r14=0x%p r15=0x%p stack20=0x%p stack28=0x%p stack30=0x%p stack38=0x%p stack40=0x%p tramp20=0x%p",
        count,
        reinterpret_cast<void*>(ctx.rax),
        reinterpret_cast<void*>(ctx.rbx),
        static_cast<long long>(amount),
        reinterpret_cast<void*>(ctx.rdx),
        reinterpret_cast<void*>(ctx.rsi),
        reinterpret_cast<void*>(ctx.rdi),
        reinterpret_cast<void*>(ctx.r8),
        reinterpret_cast<void*>(ctx.r9),
        reinterpret_cast<void*>(ctx.r10),
        reinterpret_cast<void*>(ctx.r11),
        reinterpret_cast<void*>(ctx.r12),
        reinterpret_cast<void*>(ctx.r13),
        reinterpret_cast<void*>(ctx.r14),
        reinterpret_cast<void*>(ctx.r15),
        reinterpret_cast<void*>(stack_20),
        reinterpret_cast<void*>(stack_28),
        reinterpret_cast<void*>(stack_30),
        reinterpret_cast<void*>(stack_38),
        reinterpret_cast<void*>(stack_40),
        reinterpret_cast<void*>(tramp_20));
}

void DispatchDamageCallback(SafetyHookContext& ctx, bool current_profile) {
    uintptr_t return_address = 0;
    uintptr_t source_context = 0;
    // At the current dispatcher site the original delta and target have
    // already moved into RAX and RSI, including any native producer edits.
    const uintptr_t target = current_profile ? ctx.rsi : ctx.rcx;
    const uintptr_t selector = current_profile ? ctx.rbx & 0xFFFFu : ctx.rdx;
    auto& delta = current_profile ? ctx.rax : ctx.r9;
    const bool trace_sample = ShouldLogAggressive(g_damage_trace_samples, 64);
    const bool aggressive_sample = kEconomyOnlyBuild && ShouldLogAggressive(g_damage_aggressive_samples, 512);
    if (trace_sample || aggressive_sample) {
        Log("hooks: damage trace entry target=0x%p selector=0x%p r8=0x%p delta=%lld rsp=0x%p rip=0x%p",
            reinterpret_cast<void*>(target),
            reinterpret_cast<void*>(selector),
            reinterpret_cast<void*>(ctx.r8),
            static_cast<long long>(static_cast<int64_t>(delta)),
            reinterpret_cast<void*>(ctx.rsp),
            reinterpret_cast<void*>(ctx.rip));
    }

    bool routed_through_menu = false;
    if (current_profile) {
        damage514::Frame frame{};
        if (!g_damage_binding_ready.load(std::memory_order_acquire) ||
            !damage514::DecodeFrame(g_damage_binding,ctx.rsp,frame)) return;
        return_address = frame.caller;
        source_context = frame.source;
        routed_through_menu = frame.menu;
    } else __try {
        return_address = *reinterpret_cast<const uintptr_t*>(ctx.rsp);
        source_context = *reinterpret_cast<const uintptr_t*>(ctx.rsp + 0x28);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (trace_sample || aggressive_sample) {
            Log("hooks: damage aggressive stack-read exception code=0x%08lX rsp=0x%p",
                GetExceptionCode(),
                reinterpret_cast<void*>(ctx.rsp));
        }
        return;
    }

    if (target < kMinimumPointerAddress) {
        if (trace_sample || aggressive_sample) {
            Log("hooks: damage aggressive skipped invalid target rcx=0x%p ret=0x%p sourceCtx=0x%p",
                reinterpret_cast<void*>(target),
                reinterpret_cast<void*>(return_address),
                reinterpret_cast<void*>(source_context));
        }
        return;
    }

    __try {
            const int32_t status_id = static_cast<int32_t>(selector & 0xFFFFu);
            int32_t status_id_18 = 0;
            int32_t status_id_28 = 0;
            int32_t status_id_30 = 0;
            const bool has_status_18 = TryReadValue<int32_t>(selector + 0x18, &status_id_18);
            const bool has_status_28 = TryReadValue<int32_t>(selector + 0x28, &status_id_28);
            const bool has_status_30 = TryReadValue<int32_t>(selector + 0x30, &status_id_30);
            int64_t adjusted_delta = static_cast<int64_t>(delta);
            psm::shiptrace::ObserveDamage(target, static_cast<uint16_t>(status_id), adjusted_delta, source_context, return_address);
            const bool relevant_event =
                IsRelevantDamageEvent(target, status_id, source_context, adjusted_delta);
            const bool log_sample = relevant_event && ShouldLogSample(g_damage_samples, 64);
            if (trace_sample || aggressive_sample) {
                Log("hooks: damage trace decoded target=0x%p status=%d rdx18=%d/%d rdx28=%d/%d rdx30=%d/%d delta=%lld relevant=%d sourceCtx=0x%p ret=0x%p",
                    reinterpret_cast<void*>(target),
                    status_id,
                    has_status_18 ? 1 : 0,
                    status_id_18,
                    has_status_28 ? 1 : 0,
                    status_id_28,
                    has_status_30 ? 1 : 0,
                    status_id_30,
                    static_cast<long long>(adjusted_delta),
                    relevant_event ? 1 : 0,
                    reinterpret_cast<void*>(source_context),
                    reinterpret_cast<void*>(return_address));
            }
            if (log_sample) {
                Log("hooks: damage callback target=0x%p delta=%lld sourceCtx=0x%p ret=0x%p",
                    reinterpret_cast<void*>(target),
                    static_cast<long long>(adjusted_delta),
                    reinterpret_cast<void*>(source_context),
                    reinterpret_cast<void*>(return_address));
        }

            if (TryScalePlayerDamage(target,
                                     status_id,
                                     return_address,
                                     source_context,
                                     &adjusted_delta,
                                     current_profile && g_damage_stat_writer.load(std::memory_order_acquire),
                                     routed_through_menu && damage514::PreferMenuOutgoing(&g_damage_binding))) {
                delta = static_cast<uintptr_t>(adjusted_delta);
                if (log_sample) {
                    Log("hooks: damage scaled target=0x%p final=%lld sourceCtx=0x%p ret=0x%p",
                        reinterpret_cast<void*>(target),
                        static_cast<long long>(adjusted_delta),
                        reinterpret_cast<void*>(source_context),
                        reinterpret_cast<void*>(return_address));
            }
        } else if (trace_sample || aggressive_sample) {
            Log("hooks: damage trace unchanged target=0x%p status=%d delta=%lld sourceCtx=0x%p ret=0x%p",
                reinterpret_cast<void*>(target),
                status_id,
                static_cast<long long>(adjusted_delta),
                reinterpret_cast<void*>(source_context),
                reinterpret_cast<void*>(return_address));
        }

    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("hooks: damage aggressive exception code=0x%08lX target=0x%p rdx=0x%p r9=%lld sourceCtx=0x%p ret=0x%p",
            GetExceptionCode(),
            reinterpret_cast<void*>(target),
            reinterpret_cast<void*>(selector),
            static_cast<long long>(static_cast<int64_t>(delta)),
            reinterpret_cast<void*>(source_context),
            reinterpret_cast<void*>(return_address));
        if (!g_reported_damage_exception.exchange(true, std::memory_order_acq_rel)) {
            Log("hooks: exception 0x%08lX inside damage hook", GetExceptionCode());
        }
    }
}

void DamageCallback(SafetyHookContext& ctx) {
    DispatchDamageCallback(ctx,false);
}

void DamageInteriorCallback(SafetyHookContext& ctx) {
    DispatchDamageCallback(ctx,true);
}

void ItemGainCallback(SafetyHookContext& ctx) {
    // ALWAYS log first few calls to verify hook is working
    static std::atomic<int> call_count{0};
    const int count = call_count.fetch_add(1, std::memory_order_relaxed);
    const bool aggressive_sample = kEconomyOnlyBuild && ShouldLogAggressive(g_item_gain_aggressive_samples, 1024);
    const ItemGainCaptureKind capture_kind = g_item_gain_capture_kind;
    const uintptr_t target = SelectItemGainTargetAddress(
        capture_kind, ctx.rsi, ctx.rcx, ctx.r8, ctx.rdi, ctx.rdx);
    const int64_t amount = SelectItemGainAmount(
        capture_kind, static_cast<int64_t>(ctx.rax), static_cast<int64_t>(ctx.rcx));
    
    if (count < 10 || aggressive_sample) {
        Log("hooks: item-gain callback TRIGGERED #%d contract=%s amount=%lld target=0x%p rip=0x%p",
            count,
            capture_kind == ItemGainCaptureKind::CurrentAmountRaxTargetRsiRcx
                ? "rax/rsi+rcx"
                : capture_kind == ItemGainCaptureKind::CurrentAmountRcxTargetRdx
                    ? "rcx/rdx"
                    : "rcx/r8+rdi",
            static_cast<long long>(amount),
            reinterpret_cast<void*>(target),
            reinterpret_cast<void*>(ctx.rip));
    }
    
    const bool player_ready = GetTrackedPlayerStatusMarker() >= kMinimumPointerAddress;
    const bool log_sample = player_ready && ShouldLogSample(g_item_gain_samples, 96);
    if (log_sample) {
        Log("hooks: item-gain callback target=0x%p amount=%lld rip=0x%p",
            reinterpret_cast<void*>(target),
            static_cast<long long>(amount),
            reinterpret_cast<void*>(ctx.rip));
    }

    const bool affinity_diagnostic_sample = !kEconomyOnlyBuild &&
        ((player_ready && ShouldLogSample(g_item_gain_affinity_diagnostic_samples, 64)) || count < 10);
    if (affinity_diagnostic_sample) {
        LogItemGainAffinityDiagnostic(ctx, capture_kind, count, player_ready);
    }

    __try {
        int64_t adjusted_amount = amount;
        if (aggressive_sample) {
            int64_t current_value = 0;
            const bool target_readable = TryReadValue(target, &current_value);
            Log("hooks: item-gain aggressive before #%d player_ready=%d target_readable=%d current=%lld amount=%lld",
                count,
                player_ready ? 1 : 0,
                target_readable ? 1 : 0,
                static_cast<long long>(current_value),
                static_cast<long long>(adjusted_amount));
        }
        if (TryScaleItemGain(amount, &adjusted_amount)) {
            if (capture_kind == ItemGainCaptureKind::CurrentAmountRaxTargetRsiRcx) {
                ctx.rax = static_cast<uintptr_t>(adjusted_amount);
            } else {
                ctx.rcx = static_cast<uintptr_t>(adjusted_amount);
            }
            if (log_sample || count < 10 || affinity_diagnostic_sample || aggressive_sample) {
                Log("hooks: item-gain scaled to %lld target=0x%p",
                    static_cast<long long>(adjusted_amount),
                    reinterpret_cast<void*>(target));
            }
        } else if (aggressive_sample) {
            Log("hooks: item-gain aggressive unchanged #%d amount=%lld target=0x%p",
                count,
                static_cast<long long>(adjusted_amount),
                reinterpret_cast<void*>(target));
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("hooks: item-gain aggressive exception code=0x%08lX #%d rcx=%lld r8=0x%p rdi=0x%p target=0x%p rip=0x%p",
            GetExceptionCode(),
            count,
            static_cast<long long>(ctx.rcx),
            reinterpret_cast<void*>(ctx.r8),
            reinterpret_cast<void*>(ctx.rdi),
            reinterpret_cast<void*>(target),
            reinterpret_cast<void*>(ctx.rip));
        if (!g_reported_item_gain_exception.exchange(true, std::memory_order_acq_rel)) {
            Log("hooks: exception 0x%08lX inside item-gain hook", GetExceptionCode());
        }
    }
}

void PositionHeightCallback(SafetyHookContext& ctx) {
    if (ctx.r13 < kMinimumPointerAddress) {
        return;
    }

    float horizontal_multiplier = 1.0f;
    if (ConsumeHorizontalMultiplier(&horizontal_multiplier) && horizontal_multiplier != 1.0f) {
        __try {
            const auto* const current_position = reinterpret_cast<const float*>(ctx.r13);
            const float base_x = current_position[0];
            const float base_z = current_position[2];
            const float delta_x = ctx.xmm0.f32[0] - base_x;
            const float delta_z = ctx.xmm0.f32[2] - base_z;
            ctx.xmm0.f32[0] = base_x + delta_x * horizontal_multiplier;
            ctx.xmm0.f32[2] = base_z + delta_z * horizontal_multiplier;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    float height_delta = 0.0f;
    if (!ConsumeHeightAdjustment(&height_delta)) {
        return;
    }

    ctx.xmm0.f32[1] += height_delta;
}

bool InstallSharedStatApplyHook(const psm::v3446::Image& game) {
    struct MenuReference {
        HMODULE module = nullptr;
        ~MenuReference() { if (module) FreeLibrary(module); }
    } menu_reference;
    GetModuleHandleExW(0,L"DesertLinkModMenu.asi",&menu_reference.module);
    psm::v3446::Image menu;
    const auto* provider = menu_reference.module &&
        menu.Init(menu_reference.module,psm::v3446::ModuleSize(menu_reference.module))
        ? &menu : nullptr;
    const auto binding = damage514::Resolve(game,provider);
    const bool already_bound = g_damage_binding_ready.load(std::memory_order_acquire);
    if (!binding.code_begin || (already_bound && g_damage_binding != binding)) {
        Log("hooks: shared stat-apply dispatcher ownership contract refused");
        return false;
    }

    const auto target = reinterpret_cast<uintptr_t>(game.data) + binding.native_site;
    auto prepared = psm::game::CreateMid(reinterpret_cast<void*>(target),DamageInteriorCallback,
                                          SafetyHookMid::StartDisabled);
    if (!prepared || prepared->original_bytes().size() != damage514::kPatchSize ||
        std::memcmp(prepared->original_bytes().data(),
                    damage514::kNativePatch,
                    damage514::kPatchSize) || binding != damage514::Resolve(game,provider)) {
        Log("hooks: shared stat-apply nine-byte relay preparation refused");
        return false;
    }
    if (binding.menu) {
        HMODULE pinned = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                               reinterpret_cast<LPCWSTR>(binding.menu),&pinned) ||
            pinned != menu_reference.module) return false;
    }
    if (!already_bound) {
        g_damage_binding = binding;
        g_damage_binding_ready.store(true,std::memory_order_release);
    }
    // Keep 3732's tested damage and healing routing. The 3663 build
    // already used typed-writer precedence on this interior hook.
    g_damage_stat_writer.store(binding.native_site == damage514::kNativeSite &&
        static_cast<bool>(g_stat_write_hook),std::memory_order_release);
    if (std::memcmp(reinterpret_cast<const void*>(target),damage514::kNativePatch,damage514::kPatchSize) || !psm::game::EnablePreparedHook(*prepared)) return false;
    g_damage_hook = std::move(*prepared);
    damage514::g_verified_menu.store(binding.menu ? &g_damage_binding : nullptr,std::memory_order_release);
    Log("hooks: installed native damage post-Tailwind at +0x%llX menu=%d typed-stat-writer=%d",
        static_cast<unsigned long long>(binding.native_site), binding.menu ? 1 : 0,
        g_damage_stat_writer.load() ? 1 : 0);
    return true;
}

bool InstallDamageHook() {
    psm::v3446::Image game;
    const auto host = psm::game::MainModule();
    if (game.Init(host,psm::v3446::ModuleSize(host)) && damage514::IsCurrentGame(game))
        return InstallSharedStatApplyHook(game);
    const uintptr_t target = ScanForDamageBattleAccess();
    if (target == 0) {
        return false;
    }

    auto hook_result = psm::game::CreateMid(reinterpret_cast<void*>(target), DamageCallback);
    if (!hook_result.has_value()) {
        Log("hooks: failed to create damage mid hook");
        return false;
    }

    g_damage_hook = std::move(*hook_result);
    Log("hooks: installed damage hook at 0x%p", reinterpret_cast<void*>(target));
    return true;
}

bool InstallItemGainHook() {
    const ItemGainCaptureTarget target = ScanForItemGainAccess();
    if (target.address == 0) {
        return false;
    }

    g_item_gain_capture_kind = target.capture_kind;
    if (target.capture_kind == ItemGainCaptureKind::NativeItemReceipt) {
        if (g_item_receipt_hook) {
            if (g_item_receipt_hook.target_address() != target.address) {
                Log("hooks: item-receipt target changed in-process; restart required");
                return false;
            }
            if (target.native_plan_function ? g_item_preplan_hook.target_address() != target.native_plan_function :
                g_item_plan_hook.target_address() != target.plan_site) return false;
            if(target.native_move_parent&&g_item_move_parent_hook.target_address()!=target.native_move_parent)return false;
            if (!ValidateItemNativeEntriesBeforeEnable(target)) return false;
            const bool move_enabled=!target.native_move_parent||psm::game::EnablePreparedHook(g_item_move_parent_hook);
            const bool plan_enabled = target.native_plan_function ? psm::game::EnablePreparedHook(g_item_preplan_hook) : psm::game::EnablePreparedHook(g_item_plan_hook);
            if (move_enabled && plan_enabled && psm::game::EnablePreparedHook(g_item_push_hook) && psm::game::EnablePreparedHook(g_item_receipt_hook)) {psm::item_timing::Enable(true);return true;}
            static_cast<void>(g_item_move_parent_hook.disable());
            static_cast<void>(g_item_preplan_hook.disable());
            static_cast<void>(g_item_plan_hook.disable());
            static_cast<void>(g_item_receipt_hook.disable());
            static_cast<void>(g_item_push_hook.disable());
            return false;
        }
        if (g_item_scope_tls == TLS_OUT_OF_INDEXES) g_item_scope_tls = TlsAlloc();
        if (g_item_scope_tls == TLS_OUT_OF_INDEXES || !target.push_function ||
            !target.plan_site || !target.plan_caller_return || !target.container_mapper) return false;
        SafetyHookMid plan_hook{};
        SafetyHookInline preplan_hook{},move_hook{};
        if(target.native_move_parent){
            if(g_item_move_tls==TLS_OUT_OF_INDEXES)g_item_move_tls=TlsAlloc();
            if(g_item_move_tls==TLS_OUT_OF_INDEXES||!target.move_parent_caller_return||!target.move_parent_plan_return)return false;
            auto created=psm::game::CreateInline(reinterpret_cast<void*>(target.native_move_parent),
                reinterpret_cast<void*>(ItemMoveParentCallback),SafetyHookInline::StartDisabled);
            if(!created)return false;move_hook=std::move(*created);
        }
        if (target.native_plan_function) {
            if (g_item_plan_tls == TLS_OUT_OF_INDEXES) g_item_plan_tls = TlsAlloc();
            if (g_item_plan_tls == TLS_OUT_OF_INDEXES) return false;
            auto created = psm::game::CreateInline(reinterpret_cast<void*>(target.native_plan_function),
                reinterpret_cast<void*>(ItemPreplanCallback),SafetyHookInline::StartDisabled);
            if (!created) return false;
            preplan_hook = std::move(*created);
        } else {
            auto created = psm::game::CreateMid(reinterpret_cast<void*>(target.plan_site),
                ItemPlanCallback,SafetyHookMid::StartDisabled);
            if (!created) return false;
            plan_hook = std::move(*created);
        }
        auto push_hook = psm::game::CreateInline(reinterpret_cast<void*>(target.push_function),
            reinterpret_cast<void*>(ItemPushCallback), SafetyHookInline::StartDisabled);
        if (!push_hook) return false;
        auto hook = psm::game::CreateInline(reinterpret_cast<void*>(target.address),
            reinterpret_cast<void*>(ItemReceiptCallback), SafetyHookInline::StartDisabled);
        if (!hook) {
            Log("hooks: native item-receipt inline creation failed");
            return false;
        }
        if (!ValidateItemNativeEntriesBeforeEnable(target)) {
            Log("hooks: native item entry ownership changed before enable; item group refused");
            return false;
        }
        g_item_receipt_caller = target.caller_return;
        g_item_info_lookup = target.item_info_lookup;
        g_item_metadata_profile = {target.metadata_kind,target.stack_size_offset,target.weighted_offset,target.item_id_offset};
        g_item_standalone_caller = target.standalone_caller_return;
        g_inventory_type_table_global = target.inventory_type_table_global;
        g_item_plan_caller = target.plan_caller_return;
        g_item_container_mapper = target.container_mapper;
        g_item_inventory_owner_lookup = target.native_inventory_owner_lookup;
        g_item_move_caller=target.move_parent_caller_return;g_item_move_plan_caller=target.move_parent_plan_return;
        g_item_receipt_samples.store(0);
        g_item_plan_samples.store(0);
        g_item_plan_hook = std::move(plan_hook);
        g_item_preplan_hook = std::move(preplan_hook);
        g_item_move_parent_hook=std::move(move_hook);
        g_item_push_hook = std::move(*push_hook);
        g_item_receipt_hook = std::move(*hook);
        const bool move_enabled=!target.native_move_parent||psm::game::EnablePreparedHook(g_item_move_parent_hook);
        const bool plan_enabled = target.native_plan_function ? psm::game::EnablePreparedHook(g_item_preplan_hook) : psm::game::EnablePreparedHook(g_item_plan_hook);
        if (!psm::game::EnablePreparedHook(g_item_push_hook) || !psm::game::EnablePreparedHook(g_item_receipt_hook) || !move_enabled || !plan_enabled) {
            static_cast<void>(g_item_move_parent_hook.disable());
            static_cast<void>(g_item_preplan_hook.disable());
            static_cast<void>(g_item_plan_hook.disable());
            static_cast<void>(g_item_receipt_hook.disable());
            static_cast<void>(g_item_push_hook.disable());
            return false;
        }
        psm::item_timing::Enable(true);
        Log("hooks: installed native item receipt receiver=0x%p caller=0x%p push=0x%p standalone-return=0x%p; receipt observation only",
            reinterpret_cast<void*>(target.address), reinterpret_cast<void*>(target.caller_return), reinterpret_cast<void*>(target.push_function),
            reinterpret_cast<void*>(target.standalone_caller_return));
        Log("hooks: item transaction planner=0x%p caller=0x%p; %s, receiver observation only",
            reinterpret_cast<void*>(target.native_plan_function ? target.native_plan_function : target.plan_site),
            reinterpret_cast<void*>(target.plan_caller_return),target.native_plan_function ?
            "player destination gains plus exact player-origin inventory moves before native capacity; original source/charge" : "legacy before-sql scaling");
        return true;
    }
    auto hook_result = psm::game::CreateMid(
        reinterpret_cast<void*>(target.address), ItemGainCallback);
    if (!hook_result.has_value()) {
        Log("hooks: failed to create item-gain mid hook");
        return false;
    }

    g_item_gain_hook = std::move(*hook_result);
    Log("hooks: installed item-gain hook at 0x%p contract=%s",
        reinterpret_cast<void*>(target.address),
        target.capture_kind == ItemGainCaptureKind::CurrentAmountRaxTargetRsiRcx
            ? "amount-rax,target-rsi+rcx+0x10"
            : target.capture_kind == ItemGainCaptureKind::CurrentAmountRcxTargetRdx
                ? "amount-rcx,target-rdx+0x10"
                : "amount-rcx,target-r8+rdi+0x10");
    return true;
}

}  // namespace

#ifdef PSM_ITEM_RECEIPT_TESTING
std::uint32_t RunMenuPolicyV1Test(const MenuPolicyRequest* request, std::int64_t* out) {
    g_item_menu_requests_enabled.store(true, std::memory_order_release);
    return MenuQuantityPolicyV1(request, out, nullptr);
}

uintptr_t RunMoveParentTest(uintptr_t parent,uint32_t* error,uintptr_t source,uintptr_t destination,uint16_t container,
 uintptr_t requests,uint8_t option,uintptr_t context,uintptr_t image,uintptr_t player,uintptr_t native,uintptr_t caller){
 if(g_item_move_tls==TLS_OUT_OF_INDEXES)g_item_move_tls=TlsAlloc();
 g_item_move_caller=0x234567;g_item_move_plan_caller=0x456789;
 return RunMoveParent(parent,error,source,destination,container,requests,option,context,image,player,caller,reinterpret_cast<NativeMoveParent>(native));
}
bool InstallMoveParentHookTest(uintptr_t native){
 auto hook=SafetyHookInline::create(reinterpret_cast<void*>(native),reinterpret_cast<void*>(ItemMoveParentCallback),SafetyHookInline::StartDisabled);
 if(!hook)return false;if(g_item_move_tls==TLS_OUT_OF_INDEXES)g_item_move_tls=TlsAlloc();
 if(g_item_move_tls==TLS_OUT_OF_INDEXES)return false;g_item_move_parent_hook=std::move(*hook);return g_item_move_parent_hook.enable().has_value();
}
void DisableMoveParentHookTest(){static_cast<void>(g_item_move_parent_hook.disable());}
void RemoveMoveParentHookTest(){g_item_move_parent_hook.reset();}
bool InstallItemTimingTestHooks(uintptr_t image,uintptr_t player,uintptr_t plan,uintptr_t push,uintptr_t receipt,
 uintptr_t plan_caller,uintptr_t push_caller,uintptr_t receipt_caller){
 if(g_item_plan_tls==TLS_OUT_OF_INDEXES)g_item_plan_tls=TlsAlloc();if(g_item_scope_tls==TLS_OUT_OF_INDEXES)g_item_scope_tls=TlsAlloc();
 if(g_item_plan_tls==TLS_OUT_OF_INDEXES||g_item_scope_tls==TLS_OUT_OF_INDEXES)return false;
 auto a=SafetyHookInline::create(reinterpret_cast<void*>(plan),reinterpret_cast<void*>(ItemPreplanCallback),SafetyHookInline::StartDisabled);
 auto b=SafetyHookInline::create(reinterpret_cast<void*>(push),reinterpret_cast<void*>(ItemPushCallback),SafetyHookInline::StartDisabled);
 auto c=SafetyHookInline::create(reinterpret_cast<void*>(receipt),reinterpret_cast<void*>(ItemReceiptCallback),SafetyHookInline::StartDisabled);
 if(!a||!b||!c)return false;g_item_preplan_hook=std::move(*a);g_item_push_hook=std::move(*b);g_item_receipt_hook=std::move(*c);
 g_item_timing_test_base=image;g_item_timing_test_player=player;g_item_plan_caller=plan_caller;g_item_standalone_caller=push_caller;g_item_receipt_caller=receipt_caller;
 return g_item_preplan_hook.enable()&&g_item_push_hook.enable()&&g_item_receipt_hook.enable();
}
void RemoveItemTimingTestHooks(){g_item_preplan_hook.reset();g_item_push_hook.reset();g_item_receipt_hook.reset();g_item_timing_test_base=g_item_timing_test_player=0;}
void SetItemPlanOwnerLookupTest(uintptr_t lookup) { g_item_inventory_owner_lookup = lookup; }
uintptr_t RunBorrowedItemPlanTest(uintptr_t push, uint32_t* error, uintptr_t image,
                                uintptr_t player, uintptr_t native, uintptr_t caller) {
    if (g_item_plan_tls == TLS_OUT_OF_INDEXES) g_item_plan_tls = TlsAlloc();
    return RunBorrowedItemPlan(push,error,image,player,caller,0x123456,
        reinterpret_cast<NativeItemPlanner>(native));
}
void SetItemReceiptTestMetadata3446(uintptr_t lookup) {
    g_item_info_lookup = lookup;
    g_item_metadata_profile = {ItemMetadataKind::KeyEdx3446,0x10,0x1D,8};
}
bool PrepareItemTransactionTest(uintptr_t push, uintptr_t owner, uintptr_t image,
                                uintptr_t caller, uintptr_t mapper, const char** why) {
    const char* reason = "uninitialized";
    int64_t requested = 0, delivered = 0;
    const bool result = PrepareStorageTransaction(push, owner, image, caller, 0x123456,
                                                  mapper, reason, requested, delivered);
    if (why) *why = reason;
    return result;
}
const char* DescribeItemReceiptTestScope(uintptr_t push, uintptr_t image,
                                       uintptr_t caller, uintptr_t valid_caller, uintptr_t table) {
    return ReadSingleStorageScope(push, image, caller, valid_caller, table).reason;
}
void SetItemReceiptTestNativeScope(uintptr_t push, uintptr_t image, uintptr_t caller,
                                  uintptr_t valid_caller, uintptr_t table) {
    thread_local ItemReceiptScope scope{}; // Test executable only.
    scope = ReadSingleStorageScope(push, image, caller, valid_caller, table);
    TlsSetValue(g_item_scope_tls, &scope);
}

bool InstallItemReceiptTestHook(void* native, uintptr_t caller_return, uintptr_t item_info) {
    auto hook = SafetyHookInline::create(native, reinterpret_cast<void*>(ItemReceiptCallback),
        SafetyHookInline::StartDisabled);
    if (!hook) return false;
    if (g_item_scope_tls == TLS_OUT_OF_INDEXES) g_item_scope_tls = TlsAlloc();
    g_item_receipt_caller = caller_return;
    g_item_info_lookup = item_info;
    g_item_metadata_profile = {};
    g_item_receipt_hook = std::move(*hook);
    return g_item_receipt_hook.enable().has_value();
}
void SetItemReceiptTestScope(const void* receipt, uint32_t count) {
    thread_local ItemReceiptScope scope{}; // Test executable only, never the ASI.
    scope = {};
    if (count == 1) {
        scope.receipt = receipt;
        scope.amount = psm::items::Quantity(*static_cast<const psm::items::Receipt*>(receipt));
    }
    TlsSetValue(g_item_scope_tls, &scope);
}
void RemoveItemReceiptTestHook() { g_item_receipt_hook.reset(); }
#endif

bool InstallEconomyHooks() {
    const auto config = GetConfig();

    if (ShouldInstallDamageHook(config) && !InstallDamageHook()) {
        Log("hooks: damage hook unavailable; continuing without damage scaling");
    }

    if (ShouldInstallItemGainHook(config) && !InstallItemGainHook()) {
        Log("hooks: item-gain hook unavailable; continuing without item gain scaling");
    }

    if(ShouldInstallItemGainHook(config))StartMenuItemGainStartup();
    return true;
}

void RemoveEconomyHooks() {
    g_menu_startup.Stop();g_item_menu_requests_enabled.store(false,std::memory_order_release);
    // Keep the resident foreign-entry lease/module/original; never restore over
    // another mod's later patch and never free underneath an in-flight callback.
    psm::item_timing::Enable(false);
    if(g_item_move_parent_hook)static_cast<void>(g_item_move_parent_hook.disable());
    if (g_item_preplan_hook) static_cast<void>(g_item_preplan_hook.disable());
    if (g_item_plan_hook) static_cast<void>(g_item_plan_hook.disable());
    if (g_position_height_hook) {
        g_position_height_hook.reset();
        Log("hooks: removed position-height hook");
    }

    if (g_item_gain_hook) {
        g_item_gain_hook.reset();
        Log("hooks: removed item-gain hook");
    }
    if (g_item_receipt_hook) {
        // A config reload may arrive while a callback prepares its copy. Keep
        // the original trampoline alive so that in-flight native calls finish.
        const auto disabled = g_item_receipt_hook.disable();
        Log("hooks: native item-receipt disabled=%d; original trampoline retained", disabled ? 1 : 0);
    }
    if (g_item_push_hook) static_cast<void>(g_item_push_hook.disable());

    if (g_damage_hook) {
        g_damage_hook.reset();
        damage514::g_verified_menu.store(nullptr,std::memory_order_release);
        Log("hooks: removed damage hook");
    }
}

bool InstallOptionalPositionHeightHook() {
    const uintptr_t target = ScanForPositionHeightAccess();
    if (target == 0) {
        return false;
    }

    auto hook_result = psm::game::CreateMid(reinterpret_cast<void*>(target), PositionHeightCallback);
    if (!hook_result.has_value()) {
        Log("hooks: failed to create position-height mid hook");
        return false;
    }

    g_position_height_hook = std::move(*hook_result);
    Log("hooks: installed position-height hook at 0x%p", reinterpret_cast<void*>(target));
    return true;
}

void PollMenuItemGainStartup(){
 using namespace psm::menu_gain512;
 std::lock_guard lock(g_hook_mutex);if(!g_menu_startup.active)return;
 if(!g_menu_startup.Due(GetTickCount64())){
  if(!g_menu_startup.active)Log("hooks: Menu Add Item policy startup timeout reason=%s rva=0x%X; restart required",
      g_menu_startup_probe.reason,unsigned(g_menu_startup_probe.rva));
  return;
 }
 Probe probe{};
 const auto state=TryInstallMenuItemGain(probe);
 if(state==State::Ready||state==State::Unsupported)g_menu_startup.Stop();
 if(state==State::Unsupported){
  Log("hooks: Menu Add Item policy startup refused reason=%s rva=0x%X; restart required",probe.reason,unsigned(probe.rva));
 }else if(state!=State::Ready&&(probe.rva!=g_menu_startup_probe.rva||std::strcmp(probe.reason,g_menu_startup_probe.reason))){
  Log("hooks: Menu Add Item policy still pending state=%u reason=%s rva=0x%X",unsigned(state),probe.reason,unsigned(probe.rva));
 }
 g_menu_startup_probe=probe;
}

