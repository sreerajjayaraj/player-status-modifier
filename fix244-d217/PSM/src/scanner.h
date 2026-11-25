#pragma once

#include <cstddef>
#include <cstdint>

constexpr uintptr_t ResolveRelativeBranchTarget(
    const uintptr_t instruction_address,
    const std::int32_t displacement,
    const std::size_t instruction_size = 5) {
    return static_cast<uintptr_t>(
        static_cast<std::intptr_t>(instruction_address + instruction_size) +
        static_cast<std::intptr_t>(displacement));
}

enum class PlayerPointerCaptureKind {
    LegacyMarkerRsi,
    LegacyMarkerRdx,
    CurrentPlayerCaptureBaseRax,
};

constexpr bool IsAuthoritativePlayerPointerCapture(
    const PlayerPointerCaptureKind capture_kind) {
    return capture_kind == PlayerPointerCaptureKind::CurrentPlayerCaptureBaseRax;
}

struct PlayerPointerCaptureTarget {
    uintptr_t address = 0;
    PlayerPointerCaptureKind capture_kind = PlayerPointerCaptureKind::LegacyMarkerRsi;
};

struct MountPointerCaptureTarget {
    uintptr_t address = 0;
};

enum class StatWriteValueRegister {
    Rbx,
};

struct StatWriteCaptureTarget {
    uintptr_t address = 0;
    StatWriteValueRegister value_register = StatWriteValueRegister::Rbx;
    bool enforce_native_bounds = false; // Exact validated3446 writer only.
};

enum class ItemGainCaptureKind {
    LegacyAmountRcxTargetR8Rdi,
    CurrentAmountRaxTargetRsiRcx,
    CurrentAmountRcxTargetRdx,
    NativeItemReceipt,
};

enum class ItemMetadataKind { PointerRcx, KeyEdx3446, PointerRcx3663 };

struct ItemGainCaptureTarget {
    uintptr_t address = 0;
    ItemGainCaptureKind capture_kind = ItemGainCaptureKind::LegacyAmountRcxTargetR8Rdi;
    uintptr_t caller_return = 0;
    uintptr_t item_info_lookup = 0;
    uintptr_t push_function = 0;
    uintptr_t standalone_caller_return = 0;
    uintptr_t inventory_type_table_global = 0;
    uintptr_t plan_site = 0;
    uintptr_t plan_caller_return = 0;
    uintptr_t container_mapper = 0;
    ItemMetadataKind metadata_kind = ItemMetadataKind::PointerRcx;
    std::size_t stack_size_offset = 0x18;
    std::size_t weighted_offset = 0x3F0;
    std::size_t item_id_offset = 0;
    uintptr_t native_plan_function = 0; // Exact current Plan entry for borrowed-input scaling.
    uintptr_t native_inventory_owner_lookup = 0;
    uintptr_t native_move_parent = 0;
    uintptr_t move_parent_caller_return = 0;
    uintptr_t move_parent_plan_return = 0;
};

constexpr int64_t SelectItemGainAmount(const ItemGainCaptureKind capture_kind,
                                       const int64_t rax,
                                       const int64_t rcx) {
    switch (capture_kind) {
        case ItemGainCaptureKind::LegacyAmountRcxTargetR8Rdi:
            return rcx;
        case ItemGainCaptureKind::CurrentAmountRaxTargetRsiRcx:
            return rax;
        case ItemGainCaptureKind::CurrentAmountRcxTargetRdx:
            return rcx;
        case ItemGainCaptureKind::NativeItemReceipt:
            return 0; // Quantity belongs to the borrowed receipt, not a scalar register.
    }

    return rcx;
}

constexpr uintptr_t SelectItemGainTargetAddress(const ItemGainCaptureKind capture_kind,
                                                const uintptr_t rsi,
                                                const uintptr_t rcx,
                                                const uintptr_t r8,
                                                const uintptr_t rdi,
                                                const uintptr_t rdx = 0) {
    switch (capture_kind) {
        case ItemGainCaptureKind::LegacyAmountRcxTargetR8Rdi:
            return r8 + rdi + 0x10;
        case ItemGainCaptureKind::CurrentAmountRaxTargetRsiRcx:
            return rsi + rcx + 0x10;
        case ItemGainCaptureKind::CurrentAmountRcxTargetRdx:
            return rdx + 0x10;
        case ItemGainCaptureKind::NativeItemReceipt:
            return 0;
    }

    return 0;
}

constexpr int64_t SelectStatWriteObservedValue(const StatWriteValueRegister value_register,
                                               const int64_t rbx,
                                               const int64_t r8,
                                               const int64_t rsi) {
    static_cast<void>(r8);
    static_cast<void>(rsi);

    switch (value_register) {
        case StatWriteValueRegister::Rbx:
            return rbx;
    }

    return rbx;
}

uintptr_t ScanForDamageBattleAccess();
uintptr_t ScanForDragonFlyingRestrictWrite();
uintptr_t ScanForDragonRoofRestrictTest();
uintptr_t ScanForDragonVillageSummonJump();
uintptr_t ScanForAbyssDurabilityDeltaAccess();
uintptr_t ScanForAffinityGainPrepare();
uintptr_t ScanForAffinityCurrentWrite();
uintptr_t ScanForAffinityVaryFriendly();
uintptr_t ScanForAffinityVaryFriendlyWithLogout();
uintptr_t ScanForAffinityPetDiagnosticReloc();
uintptr_t ScanForAffinityPetDiagnosticRsrc();
uintptr_t ScanForDurabilityDeltaAccess();
uintptr_t ScanForDurabilityWriteAccess();
ItemGainCaptureTarget ScanForItemGainAccess();
bool ValidateItemNativeEntriesBeforeEnable(const ItemGainCaptureTarget& target);
MountPointerCaptureTarget ScanForMountPointerCapture();
PlayerPointerCaptureTarget ScanForPlayerPointerCapture();
uintptr_t ScanForPositionHeightAccess();
uintptr_t ScanForSpiritDeltaAccess();
uintptr_t ScanForSharedResourceDeltaAccess();
uintptr_t ScanForNativeResourceRateAccess();
uintptr_t ScanForNativeResourceCostAccess();
bool ValidateNativeResourceGroup(uintptr_t rate_site, uintptr_t cost_site);
#ifdef PSM_SCANNER_TESTING
void SetScannerTestModule(void* module);
#endif
uintptr_t ScanForSpiritValueWriteAccess();
uintptr_t ScanForStaminaAb00Access();
uintptr_t ScanForStatsAccess();
StatWriteCaptureTarget ScanForStatWriteAccess();
