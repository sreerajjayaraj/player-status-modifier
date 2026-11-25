#pragma once
#include "item_3446_profiles.h"
#include "item_capture_compat.h"
#include "item_insert_3663_profiles.h"
#include "item_insert_3663_menu601_profiles.h"
#include "item_capture_v1_shapes.generated.h"
#include <span>

namespace psm::items3446 {
#include "item_plan_3462_profiles.h"
}
namespace psm::item_insert_3663 {
inline constexpr std::size_t kCallRva = 0x2367B55;
inline constexpr std::size_t kNativeRowRva = 0x4177D8;
inline constexpr std::size_t kMenuDescriptorRva = 0x416490;
inline constexpr std::size_t kMenuStateRva = 0x648FD0;
inline constexpr std::size_t kMenuCapacityRecordRva = kMenuStateRva + 40;
inline constexpr std::size_t kMenuOwnerRecordRva = kMenuStateRva + 80;
inline constexpr std::size_t kNativeOwnerRowRva = 0x417848;

enum class Lane { Insert, OwnerLookup };

template<std::size_t N>
bool Bytes(const v3446::Image& image, std::size_t rva, const unsigned char (&expected)[N]) {
    return image.Range(rva, N) && !std::memcmp(image.data + rva, expected, N);
}

template<std::size_t C, std::size_t U>
bool Owner(const v3446::Image& image, std::size_t rva, std::size_t unwind_rva,
           const unsigned char (&code)[C], const unsigned char (&unwind)[U],
           std::size_t replaced_prefix = 0) {
    RUNTIME_FUNCTION function{};
    return image.Exec(rva, C) && image.UniqueRecord(rva, function) &&
           function.BeginAddress == rva && function.EndAddress == rva + C &&
           function.UnwindData == unwind_rva && Bytes(image, unwind_rva, unwind) &&
           !std::memcmp(image.data + rva + replaced_prefix, code + replaced_prefix, C - replaced_prefix);
}

struct CaptureOwner {
    std::size_t rva, unwind_rva;
    std::span<const unsigned char> code, unwind;

    bool Matches(const v3446::Image& image) const {
        RUNTIME_FUNCTION function{};
        return image.Exec(rva, code.size()) && image.UniqueRecord(rva, function) &&
               function.BeginAddress == rva && function.EndAddress == rva + code.size() &&
               function.UnwindData == unwind_rva && image.Range(unwind_rva, unwind.size()) &&
               !std::memcmp(image.data + unwind_rva, unwind.data(), unwind.size()) &&
               !std::memcmp(image.data + rva, code.data(), code.size());
    }
};

struct CaptureLane {
    std::size_t row;
    std::span<const unsigned char> row_bytes;
    CaptureOwner handler;
};

struct CaptureLayout {
    std::size_t descriptor_rva;
    std::array<std::size_t, 13> descriptor_pointers;
    std::array<CaptureOwner, 6> owners;
    CaptureLane insert, owner_lookup;
};

inline constexpr CaptureLayout kMenu60Capture{
    kMenuDescriptorRva,
    {0, kMenuStateRva, 0x738B0, 0x730F0, kMenuHandlerRva, 0x733A0, 0x73440,
     0x73600, 0x737A0, 0x72A80, 0x72CA0, 0x72EF0, 0x73000},
    {{{kMenuInventoryInitRva, kMenuInventoryInitUnwindRva, kMenuInventoryInitCode, kMenuInventoryInitUnwind},
      {kMenuInstallerRva, kMenuInstallerUnwindRva, kMenuInstallerCode, kMenuInstallerUnwind},
      {kMenuSelectNativeRva, kMenuSelectNativeUnwindRva, kMenuSelectNativeCode, kMenuSelectNativeUnwind},
      {kMenuNotifySubscribersRva, kMenuNotifySubscribersUnwindRva, kMenuNotifySubscribersCode, kMenuNotifySubscribersUnwind},
      {kMenuCreateHookRva, kMenuCreateHookUnwindRva, kMenuCreateHookCode, kMenuCreateHookUnwind},
      {kMenuCreateHookCoreRva, kMenuCreateHookCoreUnwindRva, kMenuCreateHookCoreCode, kMenuCreateHookCoreUnwind}}},
    {kNativeRowRva, kMenuNativeInsertRow, {kMenuHandlerRva, kMenuHandlerUnwindRva, kMenuHandlerCode, kMenuHandlerUnwind}},
    {kNativeOwnerRowRva, kMenuNativeOwnerRow, {kMenuOwnerHandlerRva, kMenuOwnerHandlerUnwindRva, kMenuOwnerHandlerCode, kMenuOwnerHandlerUnwind}}
};

inline constexpr CaptureLayout kMenu601Capture{
    0x4154E0,
    {0, 0x647DA0, 0x739D0, 0x73210, menu601::kMenuHandlerRva, 0x734C0, 0x73560,
     0x73720, 0x738C0, 0x72BA0, 0x72DC0, 0x73010, 0x73120},
    {{{menu601::kMenuInventoryInitRva, menu601::kMenuInventoryInitUnwindRva, menu601::kMenuInventoryInitCode, menu601::kMenuInventoryInitUnwind},
      {menu601::kMenuInstallerRva, menu601::kMenuInstallerUnwindRva, menu601::kMenuInstallerCode, menu601::kMenuInstallerUnwind},
      {menu601::kMenuSelectNativeRva, menu601::kMenuSelectNativeUnwindRva, menu601::kMenuSelectNativeCode, menu601::kMenuSelectNativeUnwind},
      {menu601::kMenuNotifySubscribersRva, menu601::kMenuNotifySubscribersUnwindRva, menu601::kMenuNotifySubscribersCode, menu601::kMenuNotifySubscribersUnwind},
      {menu601::kMenuCreateHookRva, menu601::kMenuCreateHookUnwindRva, menu601::kMenuCreateHookCode, menu601::kMenuCreateHookUnwind},
      {menu601::kMenuCreateHookCoreRva, menu601::kMenuCreateHookCoreUnwindRva, menu601::kMenuCreateHookCoreCode, menu601::kMenuCreateHookCoreUnwind}}},
    {0x416828, menu601::kMenuNativeInsertRow, {menu601::kMenuHandlerRva, menu601::kMenuHandlerUnwindRva, menu601::kMenuHandlerCode, menu601::kMenuHandlerUnwind}},
    {0x416898, menu601::kMenuNativeOwnerRow, {menu601::kMenuOwnerHandlerRva, menu601::kMenuOwnerHandlerUnwindRva, menu601::kMenuOwnerHandlerCode, menu601::kMenuOwnerHandlerUnwind}}
};

inline const CaptureLayout* FindCaptureLayout(const v3446::Image& image) {
    using capture_compat::Context;
    const auto rva = Context::DataExport(image, "DesertLinkInventoryCaptureApiV1");
    if (!rva || !Context::DataSection(image, rva, 17 * sizeof(uint64_t), false)) return nullptr;
    for (const auto* layout : {&kMenu60Capture, &kMenu601Capture})
        if (rva == layout->descriptor_rva) return layout;
    return nullptr;
}


template<bool Notifier = false, class Shape, class Unwind, class Operands>
bool CaptureFunction(const v3446::Image& image, std::size_t rva, const Shape& shape,
                     const Unwind& unwind, const Operands& operands) {
    RUNTIME_FUNCTION record{};
    if (!image.Exec(rva, sizeof(shape)) || !image.UniqueRecord(rva, record) ||
        record.BeginAddress != rva || record.EndAddress != rva + sizeof(shape) ||
        !Bytes(image, record.UnwindData, unwind)) return false;
    std::array<unsigned char, sizeof(shape)> actual{};
    std::memcpy(actual.data(), image.data + rva, actual.size());
    for (const auto& field : operands) std::memset(actual.data() + field.offset, 0, sizeof(std::int32_t));
    if constexpr (Notifier) {
        std::memset(actual.data() + 0x8B, 0, sizeof(std::uint32_t));
        std::memset(actual.data() + 0xA5, 0, sizeof(std::uint32_t));
    }
    return std::memcmp(actual.data(), shape, actual.size()) == 0;
}

inline std::size_t LinkedRva(const v3446::Image& image, std::size_t source, capture_v1::Operand op) {
    std::int32_t delta = 0;
    if (!image.Read(source + op.offset, delta)) return image.size;
    const auto rva = static_cast<std::int64_t>(source + op.instruction_end) + delta;
    return rva >= 0 && image.Range(static_cast<std::size_t>(rva), 1)
        ? static_cast<std::size_t>(rva) : image.size;
}

inline bool GuardDispatchSlot(const v3446::Image& image, std::size_t slot) {
    const auto config = image.nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG];
    const auto at = offsetof(IMAGE_LOAD_CONFIG_DIRECTORY64, GuardCFDispatchFunctionPointer);
    std::uint64_t dispatch = 0;
    const auto base = reinterpret_cast<uintptr_t>(image.data);
    if (config.Size < at + sizeof(dispatch) || !image.Read(config.VirtualAddress + at, dispatch) ||
        !capture_compat::Context::DataSection(image, slot, sizeof(dispatch), false)) return false;
    return dispatch == base + slot;
}

inline capture_compat::Capture CaptureFromV1(const v3446::Image& image, Lane lane) {
    using namespace capture_v1;
    using Context = capture_compat::Context;
    const auto rva = Context::DataExport(image, "DesertLinkInventoryCaptureApiV1");
    std::array<std::uint64_t, 17> descriptor{};
    if (!rva || !Context::DataSection(image, rva, sizeof(descriptor), false) ||
        !image.Read(rva, descriptor)) return {};
    const auto base = reinterpret_cast<uintptr_t>(image.data);
    if (descriptor[0] != 0x31435041434C4444ULL || descriptor[1] != 0x8800000001ULL ||
        descriptor[2] || descriptor[3] != 0x31395F32315F4944ULL || descriptor[4] != base ||
        descriptor[5] < base) return {};
    const auto state = descriptor[5] - base;
    if (state % alignof(uintptr_t) || !Context::DataSection(image, state, 120, true)) return {};
    for (std::size_t i = 6; i < descriptor.size(); ++i)
        if (descriptor[i] < base || !image.Exec(descriptor[i] - base)) return {};

    const auto insert = descriptor[8] - base;
    const auto owner = descriptor[9] - base;
    if (!CaptureFunction(image, insert, kInsert, kInsertUnwind, kInsertOperands) ||
        !CaptureFunction(image, owner, kOwnerLookup, kOwnerLookupUnwind, kOwnerLookupOperands))
        return {};
    const auto cookie = LinkedRva(image, insert, kInsertOperands[0]);
    const auto notify = LinkedRva(image, insert, kInsertOperands[1]);
    const auto guard_dispatch = LinkedRva(image, insert, kInsertOperands[3]);
    const auto cookie_check = LinkedRva(image, insert, kInsertOperands[4]);
    if (!Context::DataSection(image, cookie, sizeof(uintptr_t), true) ||
        LinkedRva(image, owner, kOwnerLookupOperands[0]) != cookie ||
        LinkedRva(image, owner, kOwnerLookupOperands[1]) != state + 88 ||
        LinkedRva(image, owner, kOwnerLookupOperands[2]) != guard_dispatch ||
        LinkedRva(image, owner, kOwnerLookupOperands[3]) != notify ||
        LinkedRva(image, owner, kOwnerLookupOperands[4]) != cookie_check ||
        LinkedRva(image, insert, kInsertOperands[2]) != state + 48 ||
        !GuardDispatchSlot(image, guard_dispatch) ||
        !CaptureFunction(image, cookie_check, kCookie, kCookieUnwind, kCookieOperands) ||
        LinkedRva(image, cookie_check, kCookieOperands[0]) != cookie ||
        !image.Exec(LinkedRva(image, cookie_check, kCookieOperands[1])) ||
        !CaptureFunction<true>(image, notify, kNotify, kNotifyUnwind, kNotifyOperands) ||
        LinkedRva(image, notify, kNotifyOperands[0]) != cookie ||
        LinkedRva(image, notify, kNotifyOperands[3]) != 0 ||
        LinkedRva(image, notify, kNotifyOperands[16]) != cookie_check) return {};
    std::uint32_t active = 0, subscribers = 0;
    if (!image.Read(notify + 0x8B, active) || active != state + 32 ||
        !image.Read(notify + 0xA5, subscribers) ||
        !Context::DataSection(image, subscribers, 3 * 0xB90, true)) return {};
    const auto get_last_error = LinkedRva(image, notify, kNotifyOperands[1]);
    const auto set_last_error = LinkedRva(image, notify, kNotifyOperands[5]);
    if (get_last_error != LinkedRva(image, notify, kNotifyOperands[6]) ||
        set_last_error != LinkedRva(image, notify, kNotifyOperands[8]) ||
        set_last_error != LinkedRva(image, notify, kNotifyOperands[10]) ||
        set_last_error != LinkedRva(image, notify, kNotifyOperands[15])) return {};
    const auto tls_index = LinkedRva(image, notify, kNotifyOperands[2]);
    if (!Context::DataSection(image, tls_index, sizeof(std::uint32_t), true) ||
        LinkedRva(image, notify, kNotifyOperands[11]) != tls_index) return {};
    for (const auto index : {1u, 4u, 5u, 6u, 8u, 10u, 12u, 13u, 14u, 15u})
        if (!Context::DataSection(image, LinkedRva(image, notify, kNotifyOperands[index]),
                                  sizeof(uintptr_t), false)) return {};
    if (!image.Exec(LinkedRva(image, notify, kNotifyOperands[7])) ||
        !image.Exec(LinkedRva(image, notify, kNotifyOperands[9]))) return {};

    const auto record = state + (lane == Lane::Insert ? 40 : 80);
    return {base + (lane == Lane::Insert ? insert : owner), base + record + 8,
            lane == Lane::Insert ? 5u : 6u, base + record};
}

inline capture_compat::Capture MenuCapture(const void* module, Lane lane) {
    v3446::Image image;
    if (!module || !image.Init(module, v3446::ModuleSize(module))) return {};
    const auto* layout = FindCaptureLayout(image);
    if (!layout) return CaptureFromV1(image, lane);

    using capture_compat::Context;
    const auto base = reinterpret_cast<uintptr_t>(module);
    std::array<uint64_t, 17> descriptor{
        0x31435041434C4444ULL, 0x8800000001ULL, 0, 0x31395F32315F4944ULL
    };
    for (std::size_t i = 0; i < layout->descriptor_pointers.size(); ++i)
        descriptor[4 + i] = base + layout->descriptor_pointers[i];
    if (std::memcmp(image.data + layout->descriptor_rva, descriptor.data(), sizeof(descriptor)))
        return {};
    for (const auto& owner : layout->owners)
        if (!owner.Matches(image)) return {};

    const auto insert = lane == Lane::Insert;
    const auto& selected = insert ? layout->insert : layout->owner_lookup;
    const auto record = layout->descriptor_pointers[1] + (insert ? 40 : 80);
    if (!Context::DataSection(image, record, 40, true) ||
        !Context::DataSection(image, selected.row, selected.row_bytes.size(), false) ||
        std::memcmp(image.data + selected.row, selected.row_bytes.data(), selected.row_bytes.size()) ||
        !selected.handler.Matches(image)) return {};
    return {base + selected.handler.rva, base + record + 8, insert ? 5u : 6u, base + record};
}

enum class Route { Refused, Native, Menu };
struct Proof {
    Route route = Route::Refused;
    capture_compat::Snapshot bytes{};
    bool operator==(const Proof&) const = default;
    explicit operator bool() const { return route != Route::Refused; }
};

inline Proof Observe(const v3446::Image& game, const void* menu, Lane lane) {
    if (!v3446::HasCurrentResourceLayout(game)) return {};
    const bool insert = lane == Lane::Insert;
    const auto rva = insert ? kNativeInsertRva : kNativeOwnerRva;
    const auto& profile = insert ? items3446::kBorrowCapacityPlanner : items3446::kOwner;
    if (insert ? game.Branch(kCallRva) != rva ||
                 !Owner(game, rva, kNativeInsertUnwindRva, items3446::kBorrowCapacityPlannerCode,
                        kNativeInsertUnwind, 5)
               : !Owner(game, rva, kNativeOwnerUnwindRva, items3446::kOwnerCode,
                        kNativeOwnerUnwind, 5)) return {};
    Proof proof{};
    if (capture_compat::ObserveStrict(game, profile, rva, proof.bytes)) {
        proof.route = Route::Native;
        return proof;
    }
    const auto capture = MenuCapture(menu, lane);
    if (!capture_compat::Observe(game, profile, rva, capture, proof.bytes)) return {};
    proof.route = Route::Menu;
    return proof;
}

template<class Sample>
Proof StableProof(const Sample& sample) {
    auto first = sample();
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        auto next = sample();
        if (next && next == first) return next;
        first = next;
    }
    return {};
}

inline Proof ObserveStable(const v3446::Image& game, const void* menu, Lane lane) {
    return StableProof([&] { return Observe(game, menu, lane); });
}
}
