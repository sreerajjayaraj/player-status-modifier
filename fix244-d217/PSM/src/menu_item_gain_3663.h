#pragma once
// Included inside psm::menu_gain512, after the common receipt ownership proof.
namespace current3663 {
inline constexpr std::size_t kMetadataGlobal = 0x7246370;
inline constexpr std::size_t kMenuMetadataSlot = 0x64CF00;
inline constexpr std::size_t kMenuClientHolderSlot = 0x64CF20;
inline constexpr std::size_t kMenuServerHolderSlot = 0x64CF28;
inline constexpr std::size_t kMenuServerClientWitness = 0x64D018;

struct ProfileAt {
    std::size_t rva;
    const v3446::Profile& profile;
    std::span<const Unwind> unwind;
};

inline bool ExactUnwinds(const v3446::Image& image, std::span<const Unwind> unwind) {
    for (const auto& expected : unwind) {
        RUNTIME_FUNCTION record{};
        if (!image.UniqueRecord(expected.start,record) || record.BeginAddress != expected.start ||
            record.UnwindData != expected.rva || !image.Range(expected.rva,expected.size) ||
            std::memcmp(image.data + expected.rva,expected.bytes,expected.size)) return false;
    }
    return true;
}

struct Slots {
    uintptr_t ctor = 0, insert = 0, commit = 0, free = 0, destroy = 0;
    uintptr_t limit = 0, special = 0, metadata = 0;
    bool operator==(const Slots&) const = default;
};
inline constexpr std::array<std::size_t,8> kSlotRvas{
    0x64CE98, 0x64CEA0, 0x64CEA8, 0x64CEB0, 0x64CEB8, 0x64CED0, 0x64CED8, kMenuMetadataSlot
};

inline constexpr ProfileAt kMenuProfiles[] = {
    {kMenuSubmitRva,kMenuSubmit,kMenuSubmitUnwinds}, {kMenuFrameRva,kMenuFrame,kMenuFrameUnwinds}, {kMenuDrainRva,kMenuDrain,kMenuDrainUnwinds},
    {kMenuWorkerRva,kMenuWorker,kMenuWorkerUnwinds}, {kMenuSnapshotHolderRva,kMenuSnapshotHolder,kMenuSnapshotHolderUnwinds},
    {kMenuVerifyQueuedHolderRva,kMenuVerifyQueuedHolder,kMenuVerifyQueuedHolderUnwinds}, {kMenuHolderPairRva,kMenuHolderPair,kMenuHolderPairUnwinds},
    {kMenuClientHolderRva,kMenuClientHolder,kMenuClientHolderUnwinds}, {kMenuServerHolderRva,kMenuServerHolder,kMenuServerHolderUnwinds},
    {kMenuHolderIdentityRva,kMenuHolderIdentity,kMenuHolderIdentityUnwinds}, {kMenuNativeHolderRva,kMenuNativeHolder,kMenuNativeHolderUnwinds},
    {kMenuPlanningContextRva,kMenuPlanningContext,kMenuPlanningContextUnwinds}, {kMenuHolderStorageRva,kMenuHolderStorage,kMenuHolderStorageUnwinds},
    {kMenuPlanRva,kMenuPlan,kMenuPlanUnwinds}, {kMenuVerifyExpectedRva,kMenuVerifyExpected,kMenuVerifyExpectedUnwinds},
    {kMenuSamePlacementsRva,kMenuSamePlacements,kMenuSamePlacementsUnwinds}, {kMenuCopySnapshotRva,kMenuCopySnapshot,kMenuCopySnapshotUnwinds},
    {kMenuCopyPlacementRva,kMenuCopyPlacement,kMenuCopyPlacementUnwinds}, {kMenuSnapshotEqualRva,kMenuSnapshotEqual,kMenuSnapshotEqualUnwinds},
    {kMenuCommitRva,kMenuCommit,kMenuCommitUnwinds}, {kMenuCleanupRva,kMenuCleanup,kMenuCleanupUnwinds}, {kMenuMetadataRva,kMenuMetadata,kMenuMetadataUnwinds},
    {kMenuBindingRecordRva,kMenuBindingRecord,kMenuBindingRecordUnwinds}, {kMenuInventoryRebindRva,kMenuInventoryRebind,kMenuInventoryRebindUnwinds},
    {kMenuStackRebindRva,kMenuStackRebind,kMenuStackRebindUnwinds}, {kMenuCatalogRebindRva,kMenuCatalogRebind,kMenuCatalogRebindUnwinds},
    {kMenuProfileRebindRva,kMenuProfileRebind,kMenuProfileRebindUnwinds}, {kMenuCapacityCallRva,kMenuCapacityCall,kMenuCapacityCallUnwinds},
    {kMenuWrapperValidateRva,kMenuWrapperValidate,kMenuWrapperValidateUnwinds}, {kMenuFingerprintRva,kMenuFingerprint,kMenuFingerprintUnwinds},
    {kMenuReadClientActorRva,kMenuReadClientActor,kMenuReadClientActorUnwinds}, {kMenuResolveClientHolderRva,kMenuResolveClientHolder,kMenuResolveClientHolderUnwinds}
};

namespace menu601 {
inline constexpr ProfileAt kMenuProfiles[] = {
    {kMenuSubmitRva,kMenuSubmit,kMenuSubmitUnwinds}, {kMenuFrameRva,kMenuFrame,kMenuFrameUnwinds}, {kMenuDrainRva,kMenuDrain,kMenuDrainUnwinds},
    {kMenuWorkerRva,kMenuWorker,kMenuWorkerUnwinds}, {kMenuSnapshotHolderRva,kMenuSnapshotHolder,kMenuSnapshotHolderUnwinds},
    {kMenuVerifyQueuedHolderRva,kMenuVerifyQueuedHolder,kMenuVerifyQueuedHolderUnwinds}, {kMenuHolderPairRva,kMenuHolderPair,kMenuHolderPairUnwinds},
    {kMenuClientHolderRva,kMenuClientHolder,kMenuClientHolderUnwinds}, {kMenuServerHolderRva,kMenuServerHolder,kMenuServerHolderUnwinds},
    {kMenuHolderIdentityRva,kMenuHolderIdentity,kMenuHolderIdentityUnwinds}, {kMenuNativeHolderRva,kMenuNativeHolder,kMenuNativeHolderUnwinds},
    {kMenuPlanningContextRva,kMenuPlanningContext,kMenuPlanningContextUnwinds}, {kMenuHolderStorageRva,kMenuHolderStorage,kMenuHolderStorageUnwinds},
    {kMenuPlanRva,kMenuPlan,kMenuPlanUnwinds}, {kMenuVerifyExpectedRva,kMenuVerifyExpected,kMenuVerifyExpectedUnwinds},
    {kMenuSamePlacementsRva,kMenuSamePlacements,kMenuSamePlacementsUnwinds}, {kMenuCopySnapshotRva,kMenuCopySnapshot,kMenuCopySnapshotUnwinds},
    {kMenuCopyPlacementRva,kMenuCopyPlacement,kMenuCopyPlacementUnwinds}, {kMenuSnapshotEqualRva,kMenuSnapshotEqual,kMenuSnapshotEqualUnwinds},
    {kMenuCommitRva,kMenuCommit,kMenuCommitUnwinds}, {kMenuCleanupRva,kMenuCleanup,kMenuCleanupUnwinds}, {kMenuMetadataRva,kMenuMetadata,kMenuMetadataUnwinds},
    {kMenuBindingRecordRva,kMenuBindingRecord,kMenuBindingRecordUnwinds}, {kMenuInventoryRebindRva,kMenuInventoryRebind,kMenuInventoryRebindUnwinds},
    {kMenuStackRebindRva,kMenuStackRebind,kMenuStackRebindUnwinds}, {kMenuCatalogRebindRva,kMenuCatalogRebind,kMenuCatalogRebindUnwinds},
    {kMenuProfileRebindRva,kMenuProfileRebind,kMenuProfileRebindUnwinds}, {kMenuCapacityCallRva,kMenuCapacityCall,kMenuCapacityCallUnwinds},
    {kMenuWrapperValidateRva,kMenuWrapperValidate,kMenuWrapperValidateUnwinds}, {kMenuFingerprintRva,kMenuFingerprint,kMenuFingerprintUnwinds},
    {kMenuReadClientActorRva,kMenuReadClientActor,kMenuReadClientActorUnwinds}, {kMenuResolveClientHolderRva,kMenuResolveClientHolder,kMenuResolveClientHolderUnwinds}
};
}

struct MenuLayout {
    const item_insert_3663::CaptureLayout* capture;
    const char* name;
    std::array<std::size_t, 8> slots;
    std::size_t client_holder, server_holder, pair_witness;
    std::size_t capacity, worker, worker_call;
    const v3446::Profile* worker_profile;
    std::span<const ProfileAt> profiles;
    std::span<const Edge> edges;
};

inline constexpr MenuLayout kMenu60Layout{
    &item_insert_3663::kMenu60Capture, "Menu6.0 queue contract", kSlotRvas,
    kMenuClientHolderSlot, kMenuServerHolderSlot, kMenuServerClientWitness,
    kMenuCapacityCallRva, kMenuWorkerRva, 0x7F23A, &kMenuWorker, kMenuProfiles, kMenuEdges
};
inline constexpr MenuLayout kMenu601Layout{
    &item_insert_3663::kMenu601Capture, "Menu6.0.1 queue contract",
    {0x64BC68, 0x64BC70, 0x64BC78, 0x64BC80, 0x64BC88, 0x64BCA0, 0x64BCA8, 0x64BCD0},
    0x64BCF0, 0x64BCF8, 0x64BDE8,
    menu601::kMenuCapacityCallRva, menu601::kMenuWorkerRva, 0x7F35A,
    &menu601::kMenuWorker, menu601::kMenuProfiles, menu601::kMenuEdges
};

inline constexpr std::array kMenuLayouts{&kMenu60Layout, &kMenu601Layout};

inline const MenuLayout* FindMenuLayout(const v3446::Image& image) {
    const auto* capture = item_insert_3663::FindCaptureLayout(image);
    for (const auto* layout : kMenuLayouts)
        if (layout->capture == capture) return layout;
    return nullptr;
}

inline const MenuLayout* LayoutForWorker(const v3446::Profile* profile) {
    for (const auto* layout : kMenuLayouts)
        if (layout->worker_profile == profile) return layout;
    return nullptr;
}

inline bool ReadSlots(const v3446::Image& menu, const MenuLayout& layout, Slots& out) {
    std::array<uintptr_t,8> slots{};
    for (std::size_t i = 0; i < slots.size(); ++i) {
        if (!capture_compat::Context::DataSection(menu, layout.slots[i], sizeof(uintptr_t), true) ||
            !capture_compat::SafeCopy(reinterpret_cast<uintptr_t>(menu.data) + layout.slots[i],
                                      &slots[i], sizeof(uintptr_t))) return false;
    }
    out = {slots[0], slots[1], slots[2], slots[3], slots[4], slots[5], slots[6], slots[7]};
    return true;
}

inline std::size_t BadBinding(uintptr_t game, uintptr_t menu, const MenuLayout& layout, const Slots& slots) {
    if (slots.ctor != game + kNativeCtorRva) return layout.slots[0];
    if (slots.insert != game + item_insert_3663::kNativeInsertRva &&
        slots.insert != menu + layout.capacity) return layout.slots[1];
    if (slots.commit != game + kNativeCommitRva) return layout.slots[2];
    if (slots.free != game + 0x3DB570 &&
        slots.free != game + kNativeVectorFreeRva) return layout.slots[3];
    if (slots.destroy != game + 0x266FED0 &&
        slots.destroy != game + kNativeItemDestructorRva) return layout.slots[4];
    if (slots.limit != game + kNativeStackLimitRva) return layout.slots[5];
    if (slots.special != game + kNativeSpecialRva) return layout.slots[6];
    if (slots.metadata != game + kMetadataGlobal) return layout.slots[7];
    return 0;
}

inline bool CurrentReceiptReady(const v3446::Image& native, const OwnedReceipt& own) {
    if (!ExactUnwinds(native, kNativeReceiptUnwinds)) return false;
    if (ExactAt(native, kNativeReceiptRva, kNativeReceipt)) return true;
    ReceiptSnapshot first{}, second{};
    return ReceiptOwnObservation(native, own, first, {}, kNativeReceiptRva, kNativeReceipt) &&
           ReceiptOwnObservation(native, own, second, {}, kNativeReceiptRva, kNativeReceipt) &&
           first == second;
}

struct GrantContext {
    uintptr_t holder_client = 0;
    uintptr_t holder_server = 0;
    uintptr_t actor_client = 0;
    uintptr_t actor_server = 0;
    uintptr_t metadata_manager = 0;
    uintptr_t metadata_records = 0;
    uintptr_t metadata = 0;
    uint32_t metadata_count = 0;
    int64_t stack_size = 0;
    uint32_t item_id = 0;
    bool operator==(const GrantContext&) const = default;
};

template<class Read>
bool ReadGrantContext(const Read& read, uintptr_t game, uintptr_t menu,
                      const MenuLayout& layout, uintptr_t player,
                      uint16_t key, GrantContext& out) {
    constexpr uintptr_t min = 0x10000000;
    uintptr_t witness = 0, holder_buckets = 0;
    uint32_t count = 0;
    const auto field = [&](uintptr_t address, std::size_t offset, auto& value) {
        return address >= min && address <= UINTPTR_MAX - offset &&
               read(address + offset, &value, sizeof(value));
    };
    if (!field(menu, layout.client_holder, out.holder_client) ||
        !field(menu, layout.server_holder, out.holder_server) ||
        !field(menu, layout.pair_witness, witness) || witness != out.holder_client ||
        out.holder_server == out.holder_client ||
        !field(out.holder_client, 8, out.actor_client) ||
        !field(out.holder_server, 8, out.actor_server) ||
        !field(out.holder_client, 0x18, holder_buckets) || holder_buckets < min ||
        !field(out.holder_client, 0x20, count) || !count || count > 4096 ||
        !field(out.holder_server, 0x18, holder_buckets) || holder_buckets < min ||
        !field(out.holder_server, 0x20, count) || !count || count > 4096 ||
        player < min || out.actor_client < min || out.actor_server < min ||
        (player != out.actor_server && player != out.actor_client)) return false;

    const auto row_slot = static_cast<std::size_t>(key) * 24 + 8;
    return field(game, kMetadataGlobal, out.metadata_manager) &&
           field(out.metadata_manager, 0x28, out.metadata_count) && key < out.metadata_count &&
           field(out.metadata_manager, 0x20, out.metadata_records) &&
           field(out.metadata_records, row_slot, out.metadata) &&
           field(out.metadata, 0x10, out.stack_size) && field(out.metadata, 8, out.item_id);
}

inline bool GrantReady(uintptr_t game, uintptr_t menu, const MenuLayout& layout,
                       uintptr_t player, uint16_t key, uintptr_t metadata_getter, GrantContext& out) {
    v3446::Image native, provider;
    if (!native.Init(reinterpret_cast<void*>(game), v3446::ModuleSize(reinterpret_cast<void*>(game))) ||
        !v3446::HasCurrentResourceLayout(native) || metadata_getter != game + kNativeMetadataRva ||
        !provider.Init(reinterpret_cast<void*>(menu), v3446::ModuleSize(reinterpret_cast<void*>(menu)))) return false;
    Slots first{}, second{};
    if (!ReadSlots(provider, layout, first) || BadBinding(game, menu, layout, first)) return false;
    if (!item_insert_3663::Bytes(native, kNativeMetadataRva, kNativeMetadataCode) ||
        provider.Branch(layout.worker_call) != layout.worker) return false;
    for (const auto& edge : kGameEdges)
        if (native.Branch(edge.at,edge.opcode) != edge.target) return false;
    GrantContext repeated{};
    const auto read = capture_compat::SafeCopy;
    if (!ReadGrantContext(read, game, menu, layout, player, key, out)) return false;
    if (!ReadSlots(provider, layout, second) || first != second) return false;
    return ReadGrantContext(read, game, menu, layout, player, key, repeated) && repeated == out;
}

inline Target Resolve(const void* game, const void* menu, const OwnedReceipt& own) {
    Target out{};
    v3446::Image native, provider;
    if (!menu || !provider.Init(menu, v3446::ModuleSize(menu)) ||
        !native.Init(game, v3446::ModuleSize(game)) || !v3446::HasCurrentResourceLayout(native)) return out;
    const auto* verified_layout = FindMenuLayout(provider);
    if (!verified_layout) return out;
    const auto& layout = *verified_layout;
    out.state = State::Unsupported;
    const auto fail = [&](const char* reason, std::size_t rva) {
        out.probe = {reason, rva};
        return out;
    };
    const auto pending = [&](const char* reason, std::size_t rva) {
        out.state = State::NotReady;
        return fail(reason, rva);
    };
    for (const auto& [rva, profile, unwind] : layout.profiles)
        if (!ExactAt(provider,rva,profile) || !ExactUnwinds(provider,unwind))
            return fail("menu-queue-code",rva);
    constexpr ProfileAt game_profiles[] = {
        {kNativeCtorRva,kNativeCtor,kNativeCtorUnwinds}, {kNativeCommitRva,kNativeCommit,kNativeCommitUnwinds},
        {kNativeVectorFreeRva,kNativeVectorFree,kNativeVectorFreeUnwinds}, {kNativeItemDestructorRva,kNativeItemDestructor,kNativeItemDestructorUnwinds},
        {kNativeSpecialRva,kNativeSpecial,kNativeSpecialUnwinds}, {kNativeMetadataRva,kNativeMetadata,kNativeMetadataUnwinds},
        {kNativeCleanupCallerRva,kNativeCleanupCaller,kNativeCleanupCallerUnwinds}, {kNativeStackLimitCallerRva,kNativeStackLimitCaller,kNativeStackLimitCallerUnwinds}
    };
    for (const auto& [rva, profile, unwind] : game_profiles)
        if (!ExactAt(native,rva,profile) || !ExactUnwinds(native,unwind))
            return fail("menu-queue-native-code",rva);
    if (!native.Exec(kNativeStackLimitRva, sizeof(kNativeStackLimitCode)) ||
        !item_insert_3663::Bytes(native, kNativeStackLimitRva, kNativeStackLimitCode))
        return fail("menu-queue-native-limit", kNativeStackLimitRva);
    for (const auto& edge : layout.edges)
        if (provider.Branch(edge.at,edge.opcode) != edge.target) return fail("menu-queue-edge",edge.at);
    for (const auto& edge : kGameEdges)
        if (native.Branch(edge.at,edge.opcode) != edge.target) return fail("menu-queue-native-edge",edge.at);
    if (!CurrentReceiptReady(native, own)) return fail("menu-queue-receipt-owner",kNativeReceiptRva);

    Slots first{},second{};
    if (!ReadSlots(provider, layout, first)) return fail("menu-queue-binding-read",layout.slots.front());
    const auto native_base = reinterpret_cast<uintptr_t>(game);
    const auto menu_base = reinterpret_cast<uintptr_t>(menu);
    const std::array values{first.ctor, first.insert, first.commit, first.free,
                            first.destroy, first.limit, first.special, first.metadata};
    for (std::size_t i = 0; i < values.size(); ++i)
        if (!values[i]) return pending("menu-queue-binding-pending", layout.slots[i]);
    if (const auto bad = BadBinding(native_base, menu_base, layout, first))
        return fail("menu-queue-binding-target",bad);

    const auto insert = item_insert_3663::ObserveStable(native, menu, item_insert_3663::Lane::Insert);
    if (!insert) return pending("menu-queue-insert-owner",item_insert_3663::kNativeInsertRva);
    const bool capacity_wrapper = first.insert == menu_base + layout.capacity;
    if (capacity_wrapper && insert.route != item_insert_3663::Route::Menu)
        return pending("menu-queue-insert-wrapper-unbound",layout.slots[1]);
    if (!ReadSlots(provider, layout, second) || first != second || !CurrentReceiptReady(native, own))
        return pending("menu-queue-binding-changed",layout.slots.front());

    out.worker = menu_base + layout.worker;
    out.caller = menu_base + layout.worker_call + 5;
    out.worker_profile = layout.worker_profile;
    out.capacity_wrapper = capacity_wrapper;
    out.provider_name = layout.name;
    out.state = State::Ready;
    out.probe = {"ready", 0};
    return out;
}
}
