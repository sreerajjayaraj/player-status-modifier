#pragma once

#include <cstddef>
#include <cstdint>

namespace psm::hud {

enum class ScanStatus { Ready, InvalidImage, NotFound, Ambiguous, InvalidContract };
enum class ScanStage { None, Image, Visibility, Gauge, CostCommit };

struct ScanResult {
    ScanStatus status = ScanStatus::InvalidImage;
    ScanStage stage = ScanStage::Image;
    // After the native boolean call, before TEST AL,AL. RDI is the controller;
    // RBX is [[actor+0x68]+0x38], NOT the actor at this instruction.
    // Native code reads [RBX+0x08] before resolving a handle. If integration
    // treats it as actor, require actor+0x68->0x38 == RBX and independently
    // validate actor+0x68->0x20->0x18 against the current resource owner.
    std::uintptr_t visibility_after_call = 0;
    // R14=gauge, R15=actor, RDX=old current, RCX=old maximum. Before the
    // native R10 load of gauge+0x230; role byte +0x255 indexes native table 0..2.
    std::uintptr_t gauge_before_current_load = 0;
    // RSI=entry, RBX=sign (1=expense), [RSP+0x58]=evaluated pre-value.
    // The native commit has returned; post-value is [RSI+0x08].
    std::uintptr_t cost_after_commit = 0;
    std::uintptr_t gauge_update = 0;
    std::uintptr_t commit_target = 0;
    bool gauge_actor_rbp = false; // 3446 compiler register allocation; exact profile only.
    explicit operator bool() const noexcept { return status == ScanStatus::Ready; }
};

// Read-only scan of a PE32+ AMD64 image laid out by RVA, not raw file offsets.
// Caller supplies readable header/executable/.pdata spans for the call's duration.
// native_cost_site must be the existing ScanForNativeResourceCostAccess() result
// in this same image, captured BEFORE installing any overlapping hooks.
// No module lookup, game calls, allocation of executable memory, or patching.
// All addresses are zero on failure. Unknown/ambiguous contracts fail closed.
ScanResult ScanContracts(const void* image, std::size_t image_size,
                         std::uintptr_t native_cost_site);

// Optional sustained-activity prerequisite. Validates native mutation -> AB00
// ordering and the contribution helper; does not inspect a live modifier array.
// A false result must disable sustained observation, not weaken list bounds.
bool ValidateHudModifierListContract(const void* image, std::size_t image_size);

enum class MountedScanStage { None, Image, Root, Ride, Binding, Query };

struct MountedResourceScanResult {
    ScanStatus status = ScanStatus::InvalidImage;
    MountedScanStage stage = MountedScanStage::Image;
    // RSI=root, R15=active player equip actor, R13=resolved mount actor.
    // The native wrapper still owns the mount here. root+0x360 is a cache:
    // runtime must compare player+0x68->0x48->0x18->0x10 with mount+0x60.
    std::uintptr_t mount_actor_capture = 0;
    // After canonical slot lookup, before evaluation: R14=owner, BX=selector,
    // RAX=entry. Correlate with actor+0x68->0x20->0x18, never a guessed array.
    std::uintptr_t query_after_lookup = 0;
    std::uintptr_t root_update = 0;
    std::uintptr_t ride_refresh = 0;
    std::uintptr_t gauge_update = 0;
    std::uintptr_t current_query = 0;
    std::uintptr_t entry_lookup = 0;
    std::uintptr_t current_evaluator = 0;
    bool current_3446 = false; // owner=RSI, dead+293, controller ride-id+370.
    explicit operator bool() const noexcept { return status == ScanStatus::Ready; }
};

// Optional, independent sidecar; does not change ScanContracts or existing HUD.
// Same read-only RVA-mapped input contract. Scan before installing hooks.
// Addresses are observation/link evidence, NOT authorization to call game code.
// Static validation does not establish live ride freshness or mount category.
// No known-RVA fallback; all addresses remain zero on failure.
MountedResourceScanResult ScanMountedResourceContracts(const void* image, std::size_t image_size);

}  // namespace psm::hud
