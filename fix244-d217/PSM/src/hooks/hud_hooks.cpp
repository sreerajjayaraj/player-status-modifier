#include "hooks/game_hooks.h"
#include "hooks/hud_hooks.h"
#include "runtime/resource_hud.h"
#include "logger.h"
#include <Windows.h>
#include <safetyhook.hpp>
#include <cstring>

namespace {
SafetyHookMid g_visibility, g_gauge, g_committed_cost;
bool g_gauge_actor_rbp = false;

bool ReadBefore(uintptr_t address, int64_t* value) {
    __try { *value = *reinterpret_cast<const int64_t*>(address); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void Visibility(SafetyHookContext& ctx) {
    // The native actor/dead/mount/gauge guards have already passed. Only add
    // an activity result here; leave native timer/reset and showing to the game.
    if (!(ctx.rax & 0xFF) && ResourceHudStaminaActive(ctx.rbx))
        ctx.rax = (ctx.rax & ~uintptr_t{0xFF}) | 1;
}

void Gauge(SafetyHookContext& ctx) {
    ApplyResourceHudPresentation(ctx.r14, g_gauge_actor_rbp ? ctx.rbp : ctx.r15);
}

void CommittedCost(SafetyHookContext& ctx) {
    int64_t before = 0;
    if (ctx.rbx == 1 && ReadBefore(ctx.rsp + 0x58, &before))
        ObserveResourceHudCommittedCost(ctx.rsi, before);
}

PreparedResourceHud Prepare(uintptr_t cost) {
    PreparedResourceHud result{};
    __try {
        const auto module = reinterpret_cast<uintptr_t>(GetModuleHandleW(L"CrimsonDesert.exe"));
        if (!module) return result;
        const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x100000)
            return result;
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
            return result;
        result.sites = psm::hud::ScanContracts(reinterpret_cast<const void*>(module),
                                              nt->OptionalHeader.SizeOfImage, cost);
        if (result.sites) {
            const std::array sites{result.sites.visibility_after_call,
                result.sites.gauge_before_current_load, result.sites.cost_after_commit};
            for (std::size_t i = 0; i < sites.size(); ++i) {
                auto& witness = result.original[i];
                if (!psm::game::ExecutableSpan(reinterpret_cast<HMODULE>(module), sites[i], witness.size()))
                    return {};
                std::memcpy(witness.data(), reinterpret_cast<const void*>(sites[i]), witness.size());
            }
            result.modifiers = psm::hud::ValidateHudModifierListContract(
                reinterpret_cast<const void*>(module), nt->OptionalHeader.SizeOfImage);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { result = {}; }
    return result;
}
}

PreparedResourceHud PrepareResourceHudHooks(uintptr_t cost) {
    const auto prepared = Prepare(cost);
    if (!prepared.sites)
        Log("hud: native contract unavailable status=%d stage=%d; extension stays disabled, d159 mechanics retained",
            static_cast<int>(prepared.sites.status), static_cast<int>(prepared.sites.stage));
    return prepared;
}

bool InstallResourceHudHooks(const PreparedResourceHud& prepared) {
    if (!prepared.sites) return false;
    const auto& s = prepared.sites;
    const auto host = psm::game::MainModule();
    const auto make = [&](std::size_t index, uintptr_t site, auto callback) {
        const auto& witness = prepared.original[index];
        if (!psm::game::ExecutableSpan(host, site, witness.size()) ||
            std::memcmp(reinterpret_cast<const void*>(site), witness.data(), witness.size()))
            return SafetyHookMid{};
        auto hook = psm::game::CreateMid(reinterpret_cast<void*>(site), callback, SafetyHookMid::StartDisabled);
        if (!hook || hook->original_bytes().empty() ||
            hook->original_bytes().size() > witness.size() ||
            std::memcmp(hook->original_bytes().data(), witness.data(), hook->original_bytes().size()))
            return SafetyHookMid{};
        return std::move(*hook);
    };
    auto visibility = make(0, s.visibility_after_call, Visibility);
    auto gauge = make(1, s.gauge_before_current_load, Gauge);
    auto cost = make(2, s.cost_after_commit, CommittedCost);
    g_gauge_actor_rbp = s.gauge_actor_rbp;
    if (!visibility || !gauge || !cost || !psm::game::EnablePreparedHook(visibility) ||
        !psm::game::EnablePreparedHook(gauge) || !psm::game::EnablePreparedHook(cost)) {
        Log("hud: optional hook group validation or activation failed; rolling back HUD extension");
        return false;
    }
    g_visibility = std::move(visibility);
    g_gauge = std::move(gauge);
    g_committed_cost = std::move(cost);
    SetResourceHudEnabled(true, prepared.modifiers);
    Log("hud: installed visibility=0x%p gauge=0x%p committed-cost=0x%p sustained-contract=%d feedback-ms=120 gameplay-writes=0",
        reinterpret_cast<void*>(s.visibility_after_call), reinterpret_cast<void*>(s.gauge_before_current_load),
        reinterpret_cast<void*>(s.cost_after_commit), prepared.modifiers ? 1 : 0);
    return true;
}

void RemoveResourceHudHooks() {
    SetResourceHudEnabled(false, false);
    g_visibility.reset();
    g_gauge.reset();
    g_committed_cost.reset();
}
