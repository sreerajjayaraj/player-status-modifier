#pragma once

#include "item_3732_profiles.h"
#include "item_insert_3663.h"
#include "scanner.h"

namespace psm::items3732 {

inline constexpr std::size_t kOwnerCallInPlan = 0x2A;
inline constexpr std::size_t kDestinationCallInPlan = 0xC8;
inline constexpr std::size_t kInsertCallInDestination = 0x4B5;
inline constexpr std::size_t kCurrentWeightedOffset = 0x1E;

using NativeBindings = std::array<std::size_t, Count>;

inline bool MatchesNative(const v3446::Image& game, const Profile& profile, std::size_t entry) {
    return entry && game.Exec(entry, profile.size) &&
           std::memcmp(game.data + entry, profile.code, profile.size) == 0 &&
           game.Fragments(entry, profile);
}

inline item_insert_3663::Proof ObserveEntry(const v3446::Image& game, const void* menu,
                                          const Profile& profile, std::size_t entry,
                                          item_insert_3663::Lane lane) {
    using item_insert_3663::Proof;
    using item_insert_3663::Route;
    auto observe = [&]() -> Proof {
        Proof proof{};
        if (capture_compat::ObserveStrict(game, profile, entry, proof.bytes)) {
            proof.route = Route::Native;
        } else if (capture_compat::Observe(game, profile, entry,
                                          item_insert_3663::MenuCapture(menu, lane), proof.bytes)) {
            proof.route = Route::Menu;
        }
        return proof;
    };
    return item_insert_3663::StableProof(observe);
}

inline bool MoveRequestTypeVerified(const v3446::Image& game, std::size_t request) {
    using capture_compat::Context;
    constexpr std::size_t kMethodSlot = 0x5F6B2A8;
    constexpr std::size_t kLocatorSlot = kMethodSlot - 24;
    constexpr char kRequestType[] = ".?AVTrocTrMoveItemInventoryToOtherInventoryReq@pa@@";
    const auto preferredBase = game.nt.OptionalHeader.ImageBase;
    std::uint64_t method = 0, locatorPointer = 0;
    if (!(game.nt.FileHeader.Characteristics & IMAGE_FILE_RELOCS_STRIPPED) ||
        game.nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size ||
        !Context::DataSection(game, kLocatorSlot, 32, false) ||
        !game.Read(kMethodSlot, method) || method != preferredBase + request ||
        !game.Read(kLocatorSlot, locatorPointer) || locatorPointer <= preferredBase)
        return false;

    const auto locator = static_cast<std::size_t>(locatorPointer - preferredBase);
    std::array<std::uint32_t, 6> rtti{};
    return Context::DataSection(game, locator, sizeof(rtti), false) &&
           game.Read(locator, rtti) && rtti[0] == 1 && rtti[5] == locator &&
           Context::DataSection(game, rtti[3], 16 + sizeof(kRequestType), true) &&
           std::memcmp(game.data + rtti[3] + 16, kRequestType, sizeof(kRequestType)) == 0;
}

inline bool LayoutConstantsVerified(const v3446::Image& game, const NativeBindings& entries) {
    // These obfuscated immediates decode to the original slot increment,
    // receipt success code and stackability cutoff. The complete instruction
    // owners have already been checked, including the unencoded merge zero.
    std::uint32_t increment = 0, success = 0, capacity = 0;
    return game.RipDword(entries[Receipt] + 0x3D, 2, 6, increment) &&
           std::uint32_t{0} - 0x5B74B662u - increment == 1 &&
           game.RipDword(entries[Receipt] + 0x270, 2, 6, success) &&
           0x8348242Cu - success == 0 &&
           game.RipDword(entries[Capacity] + 0xC, 2, 6, capacity) &&
           capacity + 0xB15DF011u == 1;
}

inline ItemGainCaptureTarget Resolve(const v3446::Image& game, std::size_t plan, const void* menu,
                                     NativeBindings& entries, unsigned& captureMask,
                                     std::size_t& failedCall, std::size_t& expected, std::size_t& observed) {
    entries[Plan] = plan;
    for (std::size_t i = 0; i < Count; ++i) {
        if (i == Owner || i == BorrowCapacityPlanner || i == Plan) continue;
        entries[i] = game.Find(*kProfiles[i], kProfiles[i]->count != 0);
    }
    entries[Owner] = game.Branch(plan + kOwnerCallInPlan);
    if (entries[DestinationGroup])
        entries[BorrowCapacityPlanner] = game.Branch(entries[DestinationGroup] + kInsertCallInDestination);

    using item_insert_3663::Lane;
    using item_insert_3663::Route;
    const auto owner = ObserveEntry(game, menu, kOwner, entries[Owner], Lane::OwnerLookup);
    const auto insert = ObserveEntry(game, menu, kBorrowCapacityPlanner,
                                    entries[BorrowCapacityPlanner], Lane::Insert);
    if (!owner) entries[Owner] = 0;
    if (!insert) entries[BorrowCapacityPlanner] = 0;
    if (std::find(entries.begin(), entries.end(), 0) != entries.end()) return {};

    for (const auto& edge : kEdges) {
        const auto at = entries[edge.caller] + edge.offset;
        const auto entry = game.Branch(at);
        const auto target = edge.target == Owner || edge.target == BorrowCapacityPlanner
            ? entry : game.Follow(entry);
        if (target == entries[edge.target]) continue;
        failedCall = at;
        expected = entries[edge.target];
        observed = target;
        return {};
    }
    if (!LayoutConstantsVerified(game, entries) || !MoveRequestTypeVerified(game, entries[MoveRequest]))
        return {};

    std::int32_t tableOffset = 0;
    if (!game.Read(entries[Pop] + 0x53, tableOffset)) return {};
    const auto table = static_cast<std::int64_t>(entries[Pop]) + 0x57 + tableOffset;
    if (table <= 0 || table % alignof(uintptr_t) ||
        !capture_compat::Context::DataSection(game, static_cast<std::size_t>(table), sizeof(uintptr_t), true))
        return {};

    captureMask = (owner.route == Route::Menu ? 1u : 0u) | (insert.route == Route::Menu ? 2u : 0u);
    const auto base = reinterpret_cast<uintptr_t>(game.data);
    return {
        base + entries[Receipt], ItemGainCaptureKind::NativeItemReceipt,
        base + entries[Push] + 0x4D6, base + entries[Metadata], base + entries[Push],
        base + entries[Standalone] + 0x1591, base + static_cast<std::size_t>(table),
        base + plan + 0xE6, base + entries[Standalone] + 0x14B0, base + entries[Mapper],
        ItemMetadataKind::PointerRcx3663, 0x10, kCurrentWeightedOffset, 0x08,
        base + plan, base + entries[Owner], base + entries[MoveParent],
        base + entries[Standalone] + 0x1235, base + entries[MoveParent] + 0x32D
    };
}

inline bool ValidateBeforeEnable(const v3446::Image& game, const void* menu,
                                 const ItemGainCaptureTarget& item) {
    const auto base = reinterpret_cast<uintptr_t>(game.data);
    const auto plan = item.native_plan_function - base;
    if (!MatchesNative(game, kPlan, plan) || !MatchesNative(game, kReceipt, item.address - base) ||
        !MatchesNative(game, kPush, item.push_function - base) ||
        !MatchesNative(game, kMoveParent, item.native_move_parent - base))
        return false;

    const auto destination = game.Follow(game.Branch(plan + kDestinationCallInPlan));
    if (!MatchesNative(game, kDestinationGroup, destination)) return false;
    const auto owner = game.Branch(plan + kOwnerCallInPlan);
    const auto insert = game.Branch(destination + kInsertCallInDestination);
    using item_insert_3663::Lane;
    return owner == item.native_inventory_owner_lookup - base &&
           ObserveEntry(game, menu, kOwner, owner, Lane::OwnerLookup) &&
           ObserveEntry(game, menu, kBorrowCapacityPlanner, insert, Lane::Insert);
}

}  // namespace psm::items3732
