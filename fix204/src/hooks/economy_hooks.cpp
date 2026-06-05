#include "hooks/hooks_internal.h"

#include "config.h"
#include "logger.h"
#include "mod_logic.h"
#include "position_control.h"
#include "scanner.h"

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <utility>

namespace {

constexpr int32_t kHealthStatusId = 0;
std::atomic<std::uint32_t> g_item_gain_affinity_diagnostic_samples{0};
std::atomic<std::uint32_t> g_item_gain_aggressive_samples{0};
std::atomic<std::uint32_t> g_damage_aggressive_samples{0};

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

void LogItemGainAffinityDiagnostic(SafetyHookContext& ctx, const int count, const bool player_ready) {
    const uintptr_t target = ctx.r8 + ctx.rdi + 0x10;

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

    const int64_t amount = static_cast<int64_t>(ctx.rcx);
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

void DamageCallback(SafetyHookContext& ctx) {
    uintptr_t return_address = 0;
    uintptr_t source_context = 0;
    const bool aggressive_sample = kEconomyOnlyBuild && ShouldLogAggressive(g_damage_aggressive_samples, 512);
    if (aggressive_sample) {
        Log("hooks: damage aggressive entry rcx=0x%p rdx=0x%p r9=%lld rsp=0x%p rip=0x%p",
            reinterpret_cast<void*>(ctx.rcx),
            reinterpret_cast<void*>(ctx.rdx),
            static_cast<long long>(static_cast<int64_t>(ctx.r9)),
            reinterpret_cast<void*>(ctx.rsp),
            reinterpret_cast<void*>(ctx.rip));
    }

    __try {
        return_address = *reinterpret_cast<const uintptr_t*>(ctx.rsp);
        source_context = *reinterpret_cast<const uintptr_t*>(ctx.rsp + 0x28);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (aggressive_sample) {
            Log("hooks: damage aggressive stack-read exception code=0x%08lX rsp=0x%p",
                GetExceptionCode(),
                reinterpret_cast<void*>(ctx.rsp));
        }
        return;
    }

    if (ctx.rcx < kMinimumPointerAddress) {
        if (aggressive_sample) {
            Log("hooks: damage aggressive skipped invalid target rcx=0x%p ret=0x%p sourceCtx=0x%p",
                reinterpret_cast<void*>(ctx.rcx),
                reinterpret_cast<void*>(return_address),
                reinterpret_cast<void*>(source_context));
        }
        return;
    }

    __try {
            const int32_t status_id = static_cast<int32_t>(ctx.rdx & 0xFFFFu);
            int64_t adjusted_delta = static_cast<int64_t>(ctx.r9);
            const bool relevant_event =
                IsRelevantDamageEvent(ctx.rcx, status_id, source_context, adjusted_delta);
            const bool log_sample = relevant_event && ShouldLogSample(g_damage_samples, 64);
            if (aggressive_sample) {
                Log("hooks: damage aggressive decoded target=0x%p status=%d delta=%lld relevant=%d sourceCtx=0x%p ret=0x%p",
                    reinterpret_cast<void*>(ctx.rcx),
                    status_id,
                    static_cast<long long>(adjusted_delta),
                    relevant_event ? 1 : 0,
                    reinterpret_cast<void*>(source_context),
                    reinterpret_cast<void*>(return_address));
            }
            if (log_sample) {
                Log("hooks: damage callback target=0x%p delta=%lld sourceCtx=0x%p ret=0x%p",
                    reinterpret_cast<void*>(ctx.rcx),
                    static_cast<long long>(adjusted_delta),
                    reinterpret_cast<void*>(source_context),
                    reinterpret_cast<void*>(return_address));
        }

            if (TryScalePlayerDamage(ctx.rcx,
                                     status_id,
                                     return_address,
                                     source_context,
                                     &adjusted_delta)) {
                ctx.r9 = static_cast<uintptr_t>(adjusted_delta);
                if (log_sample) {
                    Log("hooks: damage scaled target=0x%p final=%lld sourceCtx=0x%p ret=0x%p",
                        reinterpret_cast<void*>(ctx.rcx),
                        static_cast<long long>(adjusted_delta),
                        reinterpret_cast<void*>(source_context),
                        reinterpret_cast<void*>(return_address));
            }
        } else if (aggressive_sample) {
            Log("hooks: damage aggressive unchanged target=0x%p status=%d delta=%lld sourceCtx=0x%p ret=0x%p",
                reinterpret_cast<void*>(ctx.rcx),
                status_id,
                static_cast<long long>(adjusted_delta),
                reinterpret_cast<void*>(source_context),
                reinterpret_cast<void*>(return_address));
        }

    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("hooks: damage aggressive exception code=0x%08lX target=0x%p rdx=0x%p r9=%lld sourceCtx=0x%p ret=0x%p",
            GetExceptionCode(),
            reinterpret_cast<void*>(ctx.rcx),
            reinterpret_cast<void*>(ctx.rdx),
            static_cast<long long>(static_cast<int64_t>(ctx.r9)),
            reinterpret_cast<void*>(source_context),
            reinterpret_cast<void*>(return_address));
        if (!g_reported_damage_exception.exchange(true, std::memory_order_acq_rel)) {
            Log("hooks: exception 0x%08lX inside damage hook", GetExceptionCode());
        }
    }
}

void ItemGainCallback(SafetyHookContext& ctx) {
    // ALWAYS log first few calls to verify hook is working
    static std::atomic<int> call_count{0};
    const int count = call_count.fetch_add(1, std::memory_order_relaxed);
    const bool aggressive_sample = kEconomyOnlyBuild && ShouldLogAggressive(g_item_gain_aggressive_samples, 1024);
    const uintptr_t target = ctx.r8 + ctx.rdi + 0x10;
    
    if (count < 10 || aggressive_sample) {
        Log("hooks: item-gain callback TRIGGERED #%d rcx=%lld r8=0x%p rdi=0x%p target=0x%p rip=0x%p",
            count,
            static_cast<long long>(ctx.rcx),
            reinterpret_cast<void*>(ctx.r8),
            reinterpret_cast<void*>(ctx.rdi),
            reinterpret_cast<void*>(target),
            reinterpret_cast<void*>(ctx.rip));
    }
    
    const bool player_ready = GetTrackedPlayerStatusMarker() >= kMinimumPointerAddress;
    const bool log_sample = player_ready && ShouldLogSample(g_item_gain_samples, 96);
    if (log_sample) {
        Log("hooks: item-gain callback target=0x%p amount=%lld rip=0x%p",
            reinterpret_cast<void*>(ctx.r8 + ctx.rdi + 0x10),
            static_cast<long long>(ctx.rcx),
            reinterpret_cast<void*>(ctx.rip));
    }

    const bool affinity_diagnostic_sample = !kEconomyOnlyBuild &&
        ((player_ready && ShouldLogSample(g_item_gain_affinity_diagnostic_samples, 64)) || count < 10);
    if (affinity_diagnostic_sample) {
        LogItemGainAffinityDiagnostic(ctx, count, player_ready);
    }

    __try {
        int64_t adjusted_amount = static_cast<int64_t>(ctx.rcx);
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
        if (TryScaleItemGain(static_cast<int64_t>(ctx.rcx), &adjusted_amount)) {
            ctx.rcx = static_cast<uintptr_t>(adjusted_amount);
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

bool InstallDamageHook() {
    const uintptr_t target = ScanForDamageBattleAccess();
    if (target == 0) {
        return false;
    }

    auto hook_result = SafetyHookMid::create(reinterpret_cast<void*>(target), DamageCallback);
    if (!hook_result.has_value()) {
        Log("hooks: failed to create damage mid hook");
        return false;
    }

    g_damage_hook = std::move(*hook_result);
    Log("hooks: installed damage hook at 0x%p", reinterpret_cast<void*>(target));
    return true;
}

bool InstallItemGainHook() {
    const uintptr_t target = ScanForItemGainAccess();
    if (target == 0) {
        return false;
    }

    auto hook_result = SafetyHookMid::create(reinterpret_cast<void*>(target), ItemGainCallback);
    if (!hook_result.has_value()) {
        Log("hooks: failed to create item-gain mid hook");
        return false;
    }

    g_item_gain_hook = std::move(*hook_result);
    Log("hooks: installed item-gain hook at 0x%p", reinterpret_cast<void*>(target));
    return true;
}

}  // namespace

bool InstallEconomyHooks() {
    const auto config = GetConfig();

    if (ShouldInstallDamageHook(config) && !InstallDamageHook()) {
        Log("hooks: damage hook unavailable; continuing without damage scaling");
    }

    if (ShouldInstallItemGainHook(config) && !InstallItemGainHook()) {
        Log("hooks: item-gain hook unavailable; continuing without item gain scaling");
    }

    return true;
}

void RemoveEconomyHooks() {
    if (g_position_height_hook) {
        g_position_height_hook.reset();
        Log("hooks: removed position-height hook");
    }

    if (g_item_gain_hook) {
        g_item_gain_hook.reset();
        Log("hooks: removed item-gain hook");
    }

    if (g_damage_hook) {
        g_damage_hook.reset();
        Log("hooks: removed damage hook");
    }
}

bool InstallOptionalPositionHeightHook() {
    const uintptr_t target = ScanForPositionHeightAccess();
    if (target == 0) {
        return false;
    }

    auto hook_result = SafetyHookMid::create(reinterpret_cast<void*>(target), PositionHeightCallback);
    if (!hook_result.has_value()) {
        Log("hooks: failed to create position-height mid hook");
        return false;
    }

    g_position_height_hook = std::move(*hook_result);
    Log("hooks: installed position-height hook at 0x%p", reinterpret_cast<void*>(target));
    return true;
}
