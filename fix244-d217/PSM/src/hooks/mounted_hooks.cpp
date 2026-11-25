#include "hooks/game_hooks.h"
#include "hooks/mounted_hooks.h"
#include "runtime/mounted_resource.h"
#include "runtime/ship_trace.h"
#include "runtime/runtime_state.h"
#include "runtime/actor_resolve.h"
#include "ship_trace_scanner.h"
#include "logger.h"
#include <Windows.h>
#include <safetyhook.hpp>
#include <immintrin.h>

namespace {
struct CallbackStatus {DWORD error=GetLastError();unsigned csr=_mm_getcsr();~CallbackStatus(){_mm_setcsr(csr);SetLastError(error);}};
bool ReadNativeVehicleId(uintptr_t frame,uint16_t*out){if(!out||frame<kMinimumPointerAddress||frame>UINTPTR_MAX-0x1A0-sizeof(*out))return false;__try{*out=*reinterpret_cast<const uint16_t*>(frame+0x1A0);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
SafetyHookMid g_actor, g_query, g_ship_info, g_ship_unridden;
bool g_query_owner_rsi = false;
std::array<uintptr_t,3> g_catalog_globals{};
std::array<uintptr_t,2> g_server_vtables{};

void Actor(SafetyHookContext& ctx) {
    CallbackStatus status;
    ObserveNativeMountedActor(ctx.rsi, ctx.r15, ctx.r13);
    psm::combat_health::ObserveServerActor(ctx.r15);
    psm::shiptrace::ObserveActor(ctx.rsi, ctx.r15, ctx.r13,psm::shiptrace::ActorOrigin::Mounted,ctx.rbp);
}
void Query(SafetyHookContext& ctx) {
    CallbackStatus status;
    const auto selector = static_cast<uint16_t>(ctx.rbx);
    // This observer owns stamina only. A selector0 query can arrive while the
    // player resolver holds g_state_mutex; do not enter its mount-state lock.
    if (selector)
        ObserveNativeMountedResource(g_query_owner_rsi ? ctx.rsi : ctx.r14, selector, ctx.rax);
    psm::shiptrace::ObserveQuery(g_query_owner_rsi ? ctx.rsi : ctx.r14, selector, ctx.rax);
}

void ShipUnridden(SafetyHookContext& ctx) {
    CallbackStatus status;
    psm::combat_health::ObserveServerActor(ctx.r15);
    psm::shiptrace::ObserveActor(ctx.rsi, ctx.r15, 0, psm::shiptrace::ActorOrigin::Unridden,ctx.rbp);
}

void ShipInfo(SafetyHookContext& ctx) {
    CallbackStatus status;
    uint16_t native_id=0xFFFF;
    ReadNativeVehicleId(ctx.rbp,&native_id);
    psm::shiptrace::ObserveVehicleInfo(ctx.rsi,ctx.r15,ctx.r13,ctx.rax,native_id,ctx.rbp);
}

uintptr_t ShipSite(const psm::hud::MountedResourceScanResult& sites) {
    __try {
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(L"CrimsonDesert.exe"));
        if (!base || !sites.current_3446 || sites.mount_actor_capture < base) return 0;
        const auto size = psm::v3446::ModuleSize(reinterpret_cast<const void*>(base));
        const auto rva = ShipTraceVehicleSite(reinterpret_cast<const void*>(base), size, sites.mount_actor_capture - base);
        const auto globals=ShipTraceCatalogGlobals(reinterpret_cast<const void*>(base),size,sites.mount_actor_capture-base);
        const auto server=ShipTraceServerActorVtables(reinterpret_cast<const void*>(base),size,base);
        if(!rva||!server[0]||!server[1])return 0;
        g_server_vtables={base+server[0],base+server[1]};
        g_catalog_globals = RelocateShipTraceCatalogGlobals(base, globals);
        return base + rva;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

psm::hud::MountedResourceScanResult Prepare() {
    __try {
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(L"CrimsonDesert.exe"));
        if (!base) return {};
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x100000)
            return {};
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
            return {};
        return psm::hud::ScanMountedResourceContracts(reinterpret_cast<const void*>(base),
                                                     nt->OptionalHeader.SizeOfImage);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return {}; }
}
}

#ifdef PSM_STAT_WRITE_TESTING
void TestShipActorCallback(SafetyHookContext&ctx,bool unridden){if(unridden)ShipUnridden(ctx);else Actor(ctx);}
void TestShipInfoCallback(SafetyHookContext&ctx){ShipInfo(ctx);}
void TestShipQueryCallback(SafetyHookContext&ctx){g_query_owner_rsi=true;Query(ctx);}
#endif

psm::hud::MountedResourceScanResult PrepareNativeMountedResourceHooks() {
    const auto sites = Prepare();
    if (!sites)
        Log("mount-native: optional contract unavailable status=%d stage=%d; existing policies unchanged",
            static_cast<int>(sites.status), static_cast<int>(sites.stage));
    return sites;
}

bool InstallNativeMountedResourceHooks(const psm::hud::MountedResourceScanResult& sites) {
    if (!sites) return false;
    const auto ship_site = ShipSite(sites); // Validate the entire native group before any observation hook changes it.
    g_query_owner_rsi = false;
    auto actor = psm::game::CreateMid(reinterpret_cast<void*>(sites.mount_actor_capture), Actor);
    auto query = psm::game::CreateMid(reinterpret_cast<void*>(sites.query_after_lookup), Query);
    if (!actor || !query) {
        Log("mount-native: optional observation hook group failed; rolling back mount observers only");
        return false;
    }
    g_query_owner_rsi = false;
    g_actor = std::move(*actor); g_query = std::move(*query);
    SetNativeMountedResourceEnabled(true, sites.current_3446);
    if (ship_site) {
        auto info = psm::game::CreateMid(reinterpret_cast<void*>(ship_site), ShipInfo);
        auto unridden = psm::game::CreateMid(reinterpret_cast<void*>(ship_site + 0x2A1), ShipUnridden);
        if (info && unridden) {
            g_ship_info = std::move(*info);
            g_ship_unridden = std::move(*unridden);
            psm::combat_health::SetServerActorVtable(g_server_vtables[0]);
            const bool have_catalog = g_catalog_globals[0] && g_catalog_globals[1] && g_catalog_globals[2];
            Log("ship-trace: 3663 server player observer installed; native vessel catalog=%s",
                have_catalog ? "validated" : "unavailable");
            if (have_catalog) {
                psm::shiptrace::ConfigureNativeMetadataCatalogs(g_catalog_globals[0],g_catalog_globals[1],g_catalog_globals[2],g_server_vtables);
                psm::shiptrace::Enable(true);
            }
        } else { Log("ship-trace: optional diagnostic observer failed; baseline modifiers retained"); }
    } else { Log("ship-trace: exact diagnostic native contract unavailable; baseline modifiers retained"); }
    Log("mount-native: installed actor=0x%p query=0x%p identity=active-ride-id policy=both-mount-locks no-native-calls no-stored-value-writes",
        reinterpret_cast<void*>(sites.mount_actor_capture), reinterpret_cast<void*>(sites.query_after_lookup));
    return true;
}

void RemoveNativeMountedResourceHooks() {
    psm::combat_health::SetServerActorVtable(0);
    psm::shiptrace::Enable(false);
    g_ship_info.reset();
    g_ship_unridden.reset();
    SetNativeMountedResourceEnabled(false);
    g_actor.reset(); g_query.reset();
}
