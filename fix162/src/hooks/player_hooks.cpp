#include "hooks/hooks_internal.h"

#include "config.h"
#include "logger.h"
#include "mod_logic.h"
#include "runtime/actor_resolve.h"
#include "runtime/stat_logic.h"
#include "scanner.h"

#include <Windows.h>

#include <utility>

namespace {

std::atomic<std::uint32_t> g_player_pointer_change_logs{0};
std::atomic<uintptr_t> g_last_player_pointer_actor{0};
std::atomic<uintptr_t> g_last_player_pointer_marker{0};
std::atomic<int> g_player_pointer_marker_register{0};
std::atomic<uintptr_t> g_last_stats_entry{0};
std::atomic<uintptr_t> g_last_stats_component{0};
std::atomic<uintptr_t> g_last_stats_rip{0};
std::atomic<std::uint32_t> g_special_mount_health_write_samples{0};
std::atomic<std::uint32_t> g_focus_guard_logs{0};
std::atomic<DWORD> g_last_focus_combo_tick{0};
std::atomic<std::uint32_t> g_resource_write_candidate_logs{0};
std::atomic<std::uint32_t> g_untracked_spirit_delta_logs{0};
std::atomic<std::uint32_t> g_spirit_register_scan_logs{0};

#ifndef VK_GAMEPAD_LEFT_THUMBSTICK_BUTTON
#define VK_GAMEPAD_LEFT_THUMBSTICK_BUTTON 0xC8
#endif

#ifndef VK_GAMEPAD_RIGHT_THUMBSTICK_BUTTON
#define VK_GAMEPAD_RIGHT_THUMBSTICK_BUTTON 0xC9
#endif

bool IsFocusComboWindowActive() {
    constexpr DWORD kFocusComboWindowMs = 750;
    const SHORT keyboard_focus = GetAsyncKeyState('X');
    const SHORT left_stick = GetAsyncKeyState(VK_GAMEPAD_LEFT_THUMBSTICK_BUTTON);
    const SHORT right_stick = GetAsyncKeyState(VK_GAMEPAD_RIGHT_THUMBSTICK_BUTTON);
    const DWORD now = GetTickCount();

    if ((keyboard_focus & 0x8000) != 0 ||
        ((left_stick & 0x8000) != 0 && (right_stick & 0x8000) != 0)) {
        g_last_focus_combo_tick.store(now, std::memory_order_release);
        RecordFocusRecoveryWindow();
        return true;
    }

    const DWORD last_tick = g_last_focus_combo_tick.load(std::memory_order_acquire);
    return last_tick != 0 && now - last_tick <= kFocusComboWindowMs;
}

bool TryReadHookStatEntry(const uintptr_t entry,
                          int32_t* const stat_type,
                          int64_t* const current_value,
                          int64_t* const max_value) {
    if (stat_type == nullptr ||
        current_value == nullptr ||
        max_value == nullptr ||
        entry < kMinimumPointerAddress) {
        return false;
    }

    __try {
        *stat_type = *reinterpret_cast<const int32_t*>(entry);
        *current_value = *reinterpret_cast<const int64_t*>(entry + 0x08);
        *max_value = *reinterpret_cast<const int64_t*>(entry + 0x18);
        return *max_value > 0 && *current_value >= 0 && *current_value <= *max_value;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void LogSpiritSizedValuesNearPointer(const char* const name,
                                     const uintptr_t base,
                                     const uintptr_t spirit_entry,
                                     const int64_t spirit_current,
                                     const int64_t spirit_max,
                                     std::uint32_t* const emitted) {
    if (name == nullptr || emitted == nullptr || *emitted >= 96 || base < kMinimumPointerAddress) {
        return;
    }

    for (uintptr_t offset = 0; offset <= 0x1800 && *emitted < 160; offset += 4) {
        const uintptr_t address = base + offset;
        __try {
            const int32_t dword_value = *reinterpret_cast<const int32_t*>(address);
            const int64_t qword_value = *reinterpret_cast<const int64_t*>(address);
            uintptr_t ptr_value = 0;
            if ((offset % sizeof(uintptr_t)) == 0) {
                ptr_value = *reinterpret_cast<const uintptr_t*>(address);
            }

            const bool dword_match =
                dword_value == 170 ||
                dword_value == 100 ||
                dword_value == 170000 ||
                dword_value == 100000 ||
                dword_value == static_cast<int32_t>(spirit_current) ||
                dword_value == static_cast<int32_t>(spirit_max);
            const bool qword_match =
                qword_value == spirit_current ||
                qword_value == spirit_max ||
                qword_value == 170 ||
                qword_value == 100 ||
                qword_value == 170000 ||
                qword_value == 100000;
            const bool ptr_match = ptr_value == spirit_entry;

            if (!dword_match && !qword_match && !ptr_match) {
                continue;
            }

            Log("hooks: spirit register scan base=%s ptr=0x%p offset=0x%llX addr=0x%p dword=%d qword=%lld ptr_value=0x%p match=%s%s%s",
                name,
                reinterpret_cast<void*>(base),
                static_cast<unsigned long long>(offset),
                reinterpret_cast<void*>(address),
                dword_value,
                static_cast<long long>(qword_value),
                reinterpret_cast<void*>(ptr_value),
                dword_match ? "dword" : "",
                qword_match ? "|qword" : "",
                ptr_match ? "|ptr" : "");
            ++(*emitted);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
}

void LogSpiritDeltaRegisterContext(const SafetyHookContext& ctx,
                                   const uintptr_t entry,
                                   const int64_t original_delta) {
    if (original_delta >= 0) {
        return;
    }

    const auto scan_index = g_spirit_register_scan_logs.fetch_add(1, std::memory_order_acq_rel);
    if (scan_index >= 4) {
        return;
    }

    int64_t spirit_current = 0;
    int64_t spirit_max = 0;
    int32_t spirit_type = 0;
    TryReadHookStatEntry(entry, &spirit_type, &spirit_current, &spirit_max);

    Log("hooks: spirit-delta regs entry=0x%p delta=%lld type=%d current=%lld max=%lld rax=0x%p rbx=0x%p rcx=0x%p rdx=0x%p rsi=0x%p rdi=0x%p r8=0x%p r9=0x%p r10=0x%p r11=0x%p r12=0x%p r13=0x%p r14=0x%p r15=0x%p rip=0x%p",
        reinterpret_cast<void*>(entry),
        static_cast<long long>(original_delta),
        spirit_type,
        static_cast<long long>(spirit_current),
        static_cast<long long>(spirit_max),
        reinterpret_cast<void*>(ctx.rax),
        reinterpret_cast<void*>(ctx.rbx),
        reinterpret_cast<void*>(ctx.rcx),
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
        reinterpret_cast<void*>(ctx.rip));

    std::uint32_t emitted = 0;
    LogSpiritSizedValuesNearPointer("rdx", ctx.rdx, entry, spirit_current, spirit_max, &emitted);
    LogSpiritSizedValuesNearPointer("rsi", ctx.rsi, entry, spirit_current, spirit_max, &emitted);
    LogSpiritSizedValuesNearPointer("rdi", ctx.rdi, entry, spirit_current, spirit_max, &emitted);
    LogSpiritSizedValuesNearPointer("r14", ctx.r14, entry, spirit_current, spirit_max, &emitted);
    LogSpiritSizedValuesNearPointer("r15", ctx.r15, entry, spirit_current, spirit_max, &emitted);
    LogSpiritSizedValuesNearPointer("rbx", ctx.rbx, entry, spirit_current, spirit_max, &emitted);
    LogSpiritSizedValuesNearPointer("r13", ctx.r13, entry, spirit_current, spirit_max, &emitted);
    LogSpiritSizedValuesNearPointer("tracked-player-actor", GetTrackedPlayerActor(), entry, spirit_current, spirit_max, &emitted);
    LogSpiritSizedValuesNearPointer("tracked-player-root", GetTrackedPlayerStatRoot(), entry, spirit_current, spirit_max, &emitted);
    LogSpiritSizedValuesNearPointer("tracked-player-marker", GetTrackedPlayerStatusMarker(), entry, spirit_current, spirit_max, &emitted);
    LogSpiritSizedValuesNearPointer("tracked-player-health", GetTrackedPlayerHealthEntry(), entry, spirit_current, spirit_max, &emitted);
    Log("hooks: spirit register scan complete entry=0x%p emitted=%u", reinterpret_cast<void*>(entry), emitted);
}

void LogSpecialMountHealthWriteCandidate(const SafetyHookContext& ctx) {
    int32_t stat_type = 0;
    int64_t current_value = 0;
    int64_t max_value = 0;
    if (!TryReadHookStatEntry(ctx.rdi, &stat_type, &current_value, &max_value) ||
        stat_type != kHealthId ||
        max_value < 10000 ||
        max_value > 750000) {
        return;
    }

    const int64_t requested_value = static_cast<int64_t>(ctx.rbx);
    if (requested_value >= current_value || requested_value < 0) {
        return;
    }

    const uintptr_t tracked_player_health = GetTrackedPlayerHealthEntry();
    const uintptr_t tracked_mount_health = g_mount_resolve.health_entry;
    if (ctx.rdi == tracked_player_health || ctx.rdi == tracked_mount_health) {
        return;
    }

    const auto current = g_special_mount_health_write_samples.fetch_add(1, std::memory_order_acq_rel);
    if (current >= 64) {
        return;
    }

    Log("hooks: special-mount health candidate entry=0x%p old=%lld requested=%lld max=%lld tracked_mount_health=0x%p tracked_mount_stamina=0x%p regs rcx=0x%p rdx=0x%p r8=0x%p r9=0x%p rsi=0x%p r14=0x%p rip=0x%p",
        reinterpret_cast<void*>(ctx.rdi),
        static_cast<long long>(current_value),
        static_cast<long long>(requested_value),
        static_cast<long long>(max_value),
        reinterpret_cast<void*>(tracked_mount_health),
        reinterpret_cast<void*>(g_mount_resolve.stamina_entry),
        reinterpret_cast<void*>(ctx.rcx),
        reinterpret_cast<void*>(ctx.rdx),
        reinterpret_cast<void*>(ctx.r8),
        reinterpret_cast<void*>(ctx.r9),
        reinterpret_cast<void*>(ctx.rsi),
        reinterpret_cast<void*>(ctx.r14),
        reinterpret_cast<void*>(ctx.rip));
}

bool TryRedirectPlayerStaminaCallToSpirit(SafetyHookContext& ctx,
                                          const uintptr_t stamina_entry,
                                          const int64_t original_delta) {
    static_cast<void>(ctx);
    static_cast<void>(stamina_entry);
    static_cast<void>(original_delta);
    return false;
}


void LogResourceWriteCandidate(const SafetyHookContext& ctx,
                               const uintptr_t tracked_player_health,
                               const uintptr_t tracked_player_stamina,
                               const uintptr_t tracked_player_spirit,
                               const bool player_stat_context) {
    if (player_stat_context) {
        return;
    }

    int32_t stat_type = 0;
    int64_t current_value = 0;
    int64_t max_value = 0;
    if (!TryReadHookStatEntry(ctx.rdi, &stat_type, &current_value, &max_value) ||
        (!IsStaminaStatId(stat_type) && !IsSpiritStatId(stat_type))) {
        return;
    }

    const int64_t requested_value = static_cast<int64_t>(ctx.rbx);
    const bool player_sized_resource =
        (IsStaminaStatId(stat_type) && max_value >= 250000 && max_value <= 600000) ||
        (IsSpiritStatId(stat_type) && max_value >= 100000 && max_value <= 220000);
    if (!player_sized_resource && requested_value == current_value) {
        return;
    }

    const auto current = g_resource_write_candidate_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current >= 96) {
        return;
    }

    const int64_t delta = requested_value - current_value;
    Log("hooks: resource-write candidate entry=0x%p type=%d current=%lld requested=%lld delta=%lld max=%lld tracked_health=0x%p tracked_stamina=0x%p tracked_spirit=0x%p mount_health=0x%p mount_stamina=0x%p mount_spirit=0x%p regs rcx=0x%p rdx=0x%p rsi=0x%p r8=0x%p r9=0x%p r14=0x%p rip=0x%p",
        reinterpret_cast<void*>(ctx.rdi),
        stat_type,
        static_cast<long long>(current_value),
        static_cast<long long>(requested_value),
        static_cast<long long>(delta),
        static_cast<long long>(max_value),
        reinterpret_cast<void*>(tracked_player_health),
        reinterpret_cast<void*>(tracked_player_stamina),
        reinterpret_cast<void*>(tracked_player_spirit),
        reinterpret_cast<void*>(g_mount_resolve.health_entry),
        reinterpret_cast<void*>(g_mount_resolve.stamina_entry),
        reinterpret_cast<void*>(g_mount_resolve.spirit_entry),
        reinterpret_cast<void*>(ctx.rcx),
        reinterpret_cast<void*>(ctx.rdx),
        reinterpret_cast<void*>(ctx.rsi),
        reinterpret_cast<void*>(ctx.r8),
        reinterpret_cast<void*>(ctx.r9),
        reinterpret_cast<void*>(ctx.r14),
        reinterpret_cast<void*>(ctx.rip));
}
void PlayerPointerCallback(SafetyHookContext& ctx) {
    // ALWAYS log first few calls to verify hook is working
    static std::atomic<int> call_count{0};
    const int count = call_count.fetch_add(1, std::memory_order_relaxed);
    const bool marker_in_rdx = g_player_pointer_marker_register.load(std::memory_order_acquire) != 0;

    const uintptr_t actor = ctx.rax;
    const uintptr_t status_marker = marker_in_rdx ? ctx.rdx : ctx.rsi;

    if (count < 10) {
        Log("hooks: player-pointer callback TRIGGERED #%d source=%s rax=0x%p rsi=0x%p rdx=0x%p marker=0x%p",
            count,
            marker_in_rdx ? "rdx" : "rsi",
            reinterpret_cast<void*>(ctx.rax),
            reinterpret_cast<void*>(ctx.rsi),
            reinterpret_cast<void*>(ctx.rdx),
            reinterpret_cast<void*>(status_marker));
    }

    if (actor < kMinimumPointerAddress || status_marker < kMinimumPointerAddress) {
        if (ShouldLogSample(g_player_pointer_samples, 8) || count < 10) {
            Log("hooks: player-pointer skipped, actor/marker below threshold source=%s rax=0x%p rsi=0x%p rdx=0x%p marker=0x%p",
                marker_in_rdx ? "rdx" : "rsi",
                reinterpret_cast<void*>(ctx.rax),
                reinterpret_cast<void*>(ctx.rsi),
                reinterpret_cast<void*>(ctx.rdx),
                reinterpret_cast<void*>(status_marker));
        }
        return;
    }

    const uintptr_t tracked_actor = GetTrackedPlayerActor();
    const uintptr_t tracked_marker = GetTrackedPlayerStatusMarker();
    const uintptr_t last_logged_actor = g_last_player_pointer_actor.load(std::memory_order_acquire);
    const uintptr_t last_logged_marker = g_last_player_pointer_marker.load(std::memory_order_acquire);
    if ((tracked_actor != actor || tracked_marker != status_marker) &&
        (last_logged_actor != actor || last_logged_marker != status_marker) &&
        ShouldLogSample(g_player_pointer_change_logs, 8)) {
        g_last_player_pointer_actor.store(actor, std::memory_order_release);
        g_last_player_pointer_marker.store(status_marker, std::memory_order_release);
        Log("hooks: player-pointer callback actor=0x%p marker=0x%p source=%s rax=0x%p rsi=0x%p rdx=0x%p",
            reinterpret_cast<void*>(actor),
            reinterpret_cast<void*>(status_marker),
            marker_in_rdx ? "rdx" : "rsi",
            reinterpret_cast<void*>(ctx.rax),
            reinterpret_cast<void*>(ctx.rsi),
            reinterpret_cast<void*>(ctx.rdx));
    }

    UpdateTrackedPlayerStatusComponent(actor, status_marker);
}

void StatsCallback(SafetyHookContext& ctx) {
    const uintptr_t entry = ctx.rax;
    const uintptr_t component = ctx.rsi;
    const uintptr_t rip = ctx.rip;
    const bool player_ready = GetTrackedPlayerStatusMarker() >= kMinimumPointerAddress;
    const uintptr_t last_entry = g_last_stats_entry.load(std::memory_order_acquire);
    const uintptr_t last_component = g_last_stats_component.load(std::memory_order_acquire);
    const uintptr_t last_rip = g_last_stats_rip.load(std::memory_order_acquire);
    if (player_ready &&
        (last_entry != entry || last_component != component || last_rip != rip) &&
        ShouldLogSample(g_stats_samples, 24)) {
        g_last_stats_entry.store(entry, std::memory_order_release);
        g_last_stats_component.store(component, std::memory_order_release);
        g_last_stats_rip.store(rip, std::memory_order_release);
        Log("hooks: stats callback entry=0x%p component=0x%p rip=0x%p",
            reinterpret_cast<void*>(entry),
            reinterpret_cast<void*>(component),
            reinterpret_cast<void*>(rip));
    }

    if (entry < kMinimumPointerAddress || component < kMinimumPointerAddress) {
        return;
    }

    __try {
        ObserveStatEntry(entry, component);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (!g_reported_stats_exception.exchange(true, std::memory_order_acq_rel)) {
            Log("hooks: exception 0x%08lX inside stats hook, disabling runtime processing", GetExceptionCode());
        }

        DisableRuntimeProcessing();
    }
}

void StatWriteCallback(SafetyHookContext& ctx) {
    if (ctx.rdi < kMinimumPointerAddress) {
        return;
    }

    const uintptr_t tracked_player_health = GetTrackedPlayerHealthEntry();
    const uintptr_t tracked_player_stamina = GetTrackedPlayerStaminaEntry();
    const uintptr_t tracked_player_spirit = GetTrackedPlayerSpiritEntry();
    const bool player_stat_context =
        (tracked_player_health >= kMinimumPointerAddress && ctx.rdi == tracked_player_health) ||
        (tracked_player_stamina >= kMinimumPointerAddress && ctx.rdi == tracked_player_stamina) ||
        (tracked_player_spirit >= kMinimumPointerAddress && ctx.rdi == tracked_player_spirit);
    const bool log_sample = player_stat_context && ShouldLogSample(g_stat_write_samples, 24);
    if (log_sample) {
        Log("hooks: stat-write callback entry=0x%p rbx=%lld tracked_player_health=0x%p tracked_player_stamina=0x%p tracked_player_spirit=0x%p matched=%d rip=0x%p",
            reinterpret_cast<void*>(ctx.rdi),
            static_cast<long long>(ctx.rbx),
            reinterpret_cast<void*>(tracked_player_health),
            reinterpret_cast<void*>(tracked_player_stamina),
            reinterpret_cast<void*>(tracked_player_spirit),
            player_stat_context ? 1 : 0,
            reinterpret_cast<void*>(ctx.rip));
    }

    __try {
        LogResourceWriteCandidate(ctx, tracked_player_health, tracked_player_stamina, tracked_player_spirit, player_stat_context);
        LogSpecialMountHealthWriteCandidate(ctx);
        int64_t adjusted_value = static_cast<int64_t>(ctx.rbx);
        if (TryAdjustStatWrite(ctx.rdi, &adjusted_value)) {
            ctx.rbx = static_cast<uintptr_t>(adjusted_value);
            if (log_sample) {
                Log("hooks: stat-write adjusted entry=0x%p final=%lld",
                    reinterpret_cast<void*>(ctx.rdi),
                    static_cast<long long>(adjusted_value));
            }
        }
        const bool exact_tracked_player_entry =
            (tracked_player_health >= kMinimumPointerAddress && ctx.rdi == tracked_player_health) ||
            (tracked_player_stamina >= kMinimumPointerAddress && ctx.rdi == tracked_player_stamina) ||
            (tracked_player_spirit >= kMinimumPointerAddress && ctx.rdi == tracked_player_spirit);
        if (!exact_tracked_player_entry && ctx.r8 == ctx.rsi && ctx.r8 != ctx.rbx) {
            const int64_t original_arg_value = static_cast<int64_t>(ctx.r8);
            int64_t adjusted_arg_value = static_cast<int64_t>(ctx.r8);
            if (adjusted_arg_value >= 0 && TryAdjustStatWrite(ctx.rdi, &adjusted_arg_value)) {
                ctx.r8 = static_cast<uintptr_t>(adjusted_arg_value);
                ctx.rsi = static_cast<uintptr_t>(adjusted_arg_value);
                const auto current = g_resource_write_candidate_logs.fetch_add(1, std::memory_order_acq_rel);
                if (current < 160) {
                    Log("hooks: stat-write adjusted r8/rsi value entry=0x%p old_arg=%lld final_arg=%lld rbx=%lld rip=0x%p",
                        reinterpret_cast<void*>(ctx.rdi),
                        static_cast<long long>(original_arg_value),
                        static_cast<long long>(adjusted_arg_value),
                        static_cast<long long>(ctx.rbx),
                        reinterpret_cast<void*>(ctx.rip));
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (!g_reported_stat_write_exception.exchange(true, std::memory_order_acq_rel)) {
            Log("hooks: exception 0x%08lX inside stat-write hook, disabling runtime processing", GetExceptionCode());
        }

        DisableRuntimeProcessing();
    }
}

      void SpiritDeltaCallback(SafetyHookContext& ctx) {
          const uintptr_t entry = ctx.rcx;
    if (entry < kMinimumPointerAddress) {
        return;
    }

    __try {
        const int32_t actual_type = *reinterpret_cast<const int32_t*>(entry);
        if (!IsSpiritStatId(actual_type)) {
            return;
        }

        uintptr_t tracked_player_spirit = GetTrackedPlayerSpiritEntry();
        if (tracked_player_spirit < kMinimumPointerAddress) {
            TryBootstrapPlayerSpiritFromEntry(entry);
            tracked_player_spirit = GetTrackedPlayerSpiritEntry();
        }

        const int64_t original_delta = static_cast<int64_t>(ctx.r8);
        const auto& config = GetConfig();
        const uintptr_t tracked_mount_spirit = g_mount_resolve.spirit_entry;
        bool is_player_spirit = entry == tracked_player_spirit;
        bool is_mount_spirit = tracked_mount_spirit >= kMinimumPointerAddress &&
                               entry == tracked_mount_spirit;
        bool is_untracked_mp_candidate =
            !is_player_spirit &&
            !is_mount_spirit &&
            config.spirit.enabled &&
            original_delta < 0;
        bool is_untracked_focus_recovery_candidate =
            !is_player_spirit &&
            !is_mount_spirit &&
            config.spirit.enabled &&
            original_delta > 0 &&
            IsFocusRecoveryWindowActive();
        if (!is_player_spirit &&
            !is_mount_spirit &&
            !is_untracked_mp_candidate &&
            !is_untracked_focus_recovery_candidate) {
            if (config.spirit.enabled && IsFocusRecoveryWindowActive() && original_delta > 0) {
                int64_t current_value = 0;
                int64_t max_value = 0;
                int32_t stat_type = 0;
                if (TryReadHookStatEntry(entry, &stat_type, &current_value, &max_value)) {
                    const auto current = g_untracked_spirit_delta_logs.fetch_add(1, std::memory_order_acq_rel);
                    if (!is_player_spirit && current < 48) {
                        Log("hooks: untracked focus-spirit candidate entry=0x%p type=%d delta=%lld current=%lld max=%lld tracked_player_spirit=0x%p tracked_mount_spirit=0x%p rip=0x%p",
                            reinterpret_cast<void*>(entry),
                            stat_type,
                            static_cast<long long>(original_delta),
                            static_cast<long long>(current_value),
                            static_cast<long long>(max_value),
                            reinterpret_cast<void*>(tracked_player_spirit),
                            reinterpret_cast<void*>(tracked_mount_spirit),
                            reinterpret_cast<void*>(ctx.rip));
                    }
                }
            }
            if (!is_player_spirit) {
                return;
            }
        }

        const char* const match_name = is_mount_spirit
            ? "mount"
            : (is_player_spirit
                ? "player"
                : (is_untracked_focus_recovery_candidate ? "untracked-focus-recovery-candidate" : "untracked-mp-candidate"));
        const bool log_sample = ShouldLogSample(g_spirit_delta_samples, 24);
        if (log_sample) {
            Log("hooks: spirit-delta callback entry=0x%p delta=%lld matched=%s tracked_player_spirit=0x%p tracked_mount_spirit=0x%p rip=0x%p",
                reinterpret_cast<void*>(entry),
                static_cast<long long>(original_delta),
                match_name,
                reinterpret_cast<void*>(tracked_player_spirit),
                reinterpret_cast<void*>(tracked_mount_spirit),
                reinterpret_cast<void*>(ctx.rip));
        }
        LogSpiritDeltaRegisterContext(ctx, entry, original_delta);

        int64_t adjusted_delta = original_delta;
        if (TryAdjustSpiritDelta(entry, &adjusted_delta)) {
            ctx.r8 = static_cast<uintptr_t>(adjusted_delta);
            if (log_sample) {
                Log("hooks: spirit-delta adjusted entry=0x%p final-delta=%lld",
                    reinterpret_cast<void*>(entry),
                    static_cast<long long>(adjusted_delta));
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (!g_reported_spirit_delta_exception.exchange(true, std::memory_order_acq_rel)) {
            Log("hooks: exception 0x%08lX inside spirit-delta hook, disabling runtime processing", GetExceptionCode());
        }

              DisableRuntimeProcessing();
          }
      }

      void StaminaAb00Callback(SafetyHookContext& ctx) {
          static std::atomic<int> raw_call_count{0};
          const uintptr_t entry = ctx.rax;
    if (entry < kMinimumPointerAddress) {
        return;
    }

    const int raw_count = raw_call_count.fetch_add(1, std::memory_order_relaxed);
    if (raw_count < 16) {
        Log("hooks: stamina-ab00 raw callback #%d rax=0x%p rbx=%lld r14=0x%p rdi=0x%p rip=0x%p",
            raw_count,
            reinterpret_cast<void*>(ctx.rax),
            static_cast<long long>(ctx.rbx),
            reinterpret_cast<void*>(ctx.r14),
            reinterpret_cast<void*>(ctx.rdi),
            reinterpret_cast<void*>(ctx.rip));
    }

    __try {
        const int32_t actual_type = *reinterpret_cast<const int32_t*>(entry);
        if (!IsStaminaStatId(actual_type)) {
            if (IsSpiritStatId(actual_type)) {
                const bool log_sample = ShouldLogSample(g_stamina_ab00_samples, 24);
                const int64_t original_delta = static_cast<int64_t>(ctx.rbx);
                const auto& config = GetConfig();
                const uintptr_t tracked_player_spirit = GetTrackedPlayerSpiritEntry();
                const uintptr_t tracked_mount_spirit = g_mount_resolve.spirit_entry;
                int64_t adjusted_delta = original_delta;
                if (TryAdjustSpiritDelta(entry, &adjusted_delta)) {
                    ctx.rbx = static_cast<uintptr_t>(adjusted_delta);
                    if (log_sample || raw_count < 16) {
                        Log("hooks: stamina-ab00 spirit adjusted entry=0x%p old-delta=%lld final-delta=%lld owner=0x%p tracked_player_spirit=0x%p tracked_mount_spirit=0x%p rip=0x%p",
                            reinterpret_cast<void*>(entry),
                            static_cast<long long>(original_delta),
                            static_cast<long long>(adjusted_delta),
                            reinterpret_cast<void*>(ctx.r14),
                            reinterpret_cast<void*>(tracked_player_spirit),
                            reinterpret_cast<void*>(g_mount_resolve.spirit_entry),
                            reinterpret_cast<void*>(ctx.rip));
                    }
                    return;
                }

                const bool mounted_spirit_stamina_cost =
                    config.mount.enabled &&
                    (config.mount.lock_spirit_stamina ||
                     (config.mount.special_enabled && config.mount.lock_special_spirit_stamina)) &&
                    original_delta < 0 &&
                    g_mount_resolve.valid() &&
                    entry == tracked_mount_spirit;
                if (mounted_spirit_stamina_cost) {
                    ctx.rbx = 0;
                    if (log_sample || raw_count < 16) {
                        Log("hooks: stamina-ab00 mounted spirit-stamina locked entry=0x%p old-delta=%lld final-delta=0 owner=0x%p tracked_player_spirit=0x%p tracked_mount_spirit=0x%p matched=%d rip=0x%p",
                            reinterpret_cast<void*>(entry),
                            static_cast<long long>(original_delta),
                            reinterpret_cast<void*>(ctx.r14),
                            reinterpret_cast<void*>(tracked_player_spirit),
                            reinterpret_cast<void*>(tracked_mount_spirit),
                            entry == tracked_player_spirit ? 1 : (entry == tracked_mount_spirit ? 2 : 0),
                            reinterpret_cast<void*>(ctx.rip));
                    }
                    return;
                }

                if (raw_count < 16) {
                    Log("hooks: stamina-ab00 raw skipped spirit type=%d entry=0x%p owner=0x%p tracked_player_spirit=0x%p tracked_mount_spirit=0x%p",
                        actual_type,
                        reinterpret_cast<void*>(entry),
                        reinterpret_cast<void*>(ctx.r14),
                        reinterpret_cast<void*>(tracked_player_spirit),
                        reinterpret_cast<void*>(g_mount_resolve.spirit_entry));
                }
                return;
            }
            if (raw_count < 16) {
                Log("hooks: stamina-ab00 raw skipped non-stamina type=%d entry=0x%p", actual_type, reinterpret_cast<void*>(entry));
            }
            return;
        }

        uintptr_t tracked_player_stamina = GetTrackedPlayerStaminaEntry();
        if (tracked_player_stamina < kMinimumPointerAddress) {
            TryBootstrapPlayerStaminaFromEntry(entry);
            tracked_player_stamina = GetTrackedPlayerStaminaEntry();
        }

        const bool log_sample = ShouldLogSample(g_stamina_ab00_samples, 24);
        if (log_sample) {
            Log("hooks: stamina-ab00 callback entry=0x%p delta=%lld owner=0x%p status=%u tracked_player_stamina=0x%p rip=0x%p",
                reinterpret_cast<void*>(entry),
                static_cast<long long>(ctx.rbx),
                reinterpret_cast<void*>(ctx.r14),
                static_cast<unsigned>(ctx.rdi & 0xFFFFu),
                reinterpret_cast<void*>(tracked_player_stamina),
                reinterpret_cast<void*>(ctx.rip));
        }

        if (entry != tracked_player_stamina) {
            int64_t redirected_delta = static_cast<int64_t>(ctx.rbx);
            if (TryRedirectCombatStaminaCostToAdjacentSpirit(entry, &redirected_delta)) {
                ctx.rbx = static_cast<uintptr_t>(redirected_delta);
                if (log_sample) {
                    Log("hooks: stamina-ab00 redirected adjacent combat resource entry=0x%p final-delta=%lld",
                        reinterpret_cast<void*>(entry),
                        static_cast<long long>(redirected_delta));
                }
                return;
            }
            UpdateTrackedMountFromStaminaContext(entry, ctx.r14);
        }

        if (entry == tracked_player_stamina &&
            static_cast<int64_t>(ctx.rbx) < 0 &&
            IsFocusComboWindowActive()) {
            const int64_t focus_delta = static_cast<int64_t>(ctx.rbx);
            ctx.rbx = 0;
            const auto focus_log = g_focus_guard_logs.fetch_add(1, std::memory_order_acq_rel);
            if (focus_log < 16) {
                Log("hooks: stamina-ab00 suppressed focus combo stamina-shaped cost entry=0x%p old_delta=%lld rip=0x%p",
                    reinterpret_cast<void*>(entry),
                    static_cast<long long>(focus_delta),
                    reinterpret_cast<void*>(ctx.rip));
            }
            return;
        }

        if (entry == tracked_player_stamina &&
            TryRedirectPlayerStaminaCallToSpirit(ctx, entry, static_cast<int64_t>(ctx.rbx))) {
            return;
        }

        if (entry == tracked_player_stamina) {
            if (log_sample) {
                Log("hooks: stamina-ab00 player stamina passthrough entry=0x%p delta=%lld note=stat-write-owns-player-stamina-scaling",
                    reinterpret_cast<void*>(entry),
                    static_cast<long long>(ctx.rbx));
            }
            return;
        }

        int64_t adjusted_delta = static_cast<int64_t>(ctx.rbx);
        if (TryAdjustStaminaDelta(entry, &adjusted_delta)) {
            ctx.rbx = static_cast<uintptr_t>(adjusted_delta);
            if (log_sample) {
                Log("hooks: stamina-ab00 adjusted entry=0x%p final-delta=%lld",
                    reinterpret_cast<void*>(entry),
                    static_cast<long long>(adjusted_delta));
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (!g_reported_stamina_ab00_exception.exchange(true, std::memory_order_acq_rel)) {
            Log("hooks: exception 0x%08lX inside stamina-ab00 hook, disabling runtime processing", GetExceptionCode());
        }

        DisableRuntimeProcessing();
    }
}

bool InstallSpiritDeltaHook() {
    const uintptr_t target = ScanForSpiritDeltaAccess();
    if (target == 0) {
        return false;
    }

    auto hook_result = SafetyHookMid::create(reinterpret_cast<void*>(target), SpiritDeltaCallback);
    if (!hook_result.has_value()) {
        Log("hooks: failed to create spirit-delta mid hook");
        return false;
    }

    g_spirit_delta_hook = std::move(*hook_result);
    Log("hooks: installed spirit-delta hook at 0x%p", reinterpret_cast<void*>(target));
    return true;
}

bool InstallPlayerPointerHook() {
    const auto target = ScanForPlayerPointerCapture();
    if (target.address == 0) {
        return false;
    }

    g_player_pointer_marker_register.store(
        target.marker_register == PlayerPointerMarkerRegister::Rdx ? 1 : 0,
        std::memory_order_release);

    auto hook_result = SafetyHookMid::create(reinterpret_cast<void*>(target.address), PlayerPointerCallback);
    if (!hook_result.has_value()) {
        Log("hooks: failed to create player-pointer mid hook");
        return false;
    }

    g_player_pointer_hook = std::move(*hook_result);
    Log("hooks: installed player-pointer hook at 0x%p marker-source=%s",
        reinterpret_cast<void*>(target.address),
        target.marker_register == PlayerPointerMarkerRegister::Rdx ? "rdx" : "rsi");
    return true;
}

bool InstallStatsHook() {
    const uintptr_t target = ScanForStatsAccess();
    if (target == 0) {
        return false;
    }

    auto hook_result = SafetyHookMid::create(reinterpret_cast<void*>(target), StatsCallback);
    if (!hook_result.has_value()) {
        Log("hooks: failed to create stats mid hook");
        return false;
    }

    g_stats_hook = std::move(*hook_result);
    Log("hooks: installed stats hook at 0x%p", reinterpret_cast<void*>(target));
    return true;
}

bool InstallStatWriteHook() {
    const uintptr_t target = ScanForStatWriteAccess();
    if (target == 0) {
        return false;
    }

    auto hook_result = SafetyHookMid::create(reinterpret_cast<void*>(target), StatWriteCallback);
    if (!hook_result.has_value()) {
        Log("hooks: failed to create stat-write mid hook");
        return false;
    }

    g_stat_write_hook = std::move(*hook_result);
    Log("hooks: installed stat-write pre-notify hook at 0x%p", reinterpret_cast<void*>(target));
    return true;
}

      bool InstallStaminaAb00Hook() {
          const uintptr_t target = ScanForStaminaAb00Access();
          if (target == 0) {
              return false;
    }

    auto hook_result = SafetyHookMid::create(reinterpret_cast<void*>(target), StaminaAb00Callback);
    if (!hook_result.has_value()) {
        Log("hooks: failed to create stamina-ab00 mid hook");
        return false;
    }

          g_stamina_ab00_hook = std::move(*hook_result);
          Log("hooks: installed stamina-ab00 hook at 0x%p", reinterpret_cast<void*>(target));
          return true;
      }

}  // namespace

bool InstallPlayerHooks() {
    return InstallPlayerPointerHook();
}

bool InstallPlayerStatHooks() {
    const auto config = GetConfig();
    if (!InstallStatsHook()) {
        return false;
    }

      if (ShouldInstallSpiritHook(config) && !InstallSpiritDeltaHook()) {
          return false;
      }

      if (ShouldInstallLegacyStatWriteHook(config) && !InstallStatWriteHook()) {
          return false;
      }

      if (ShouldInstallMountStaminaHook(config) && !InstallStaminaAb00Hook()) {
          return false;
          }
      
          return true;
      }
      
void RemovePlayerStatHooks() {
      if (g_stamina_ab00_hook) {
          g_stamina_ab00_hook.reset();
          Log("hooks: removed stamina-ab00 hook");
    }

    if (g_spirit_delta_hook) {
        g_spirit_delta_hook.reset();
        Log("hooks: removed spirit-delta hook");
    }

    if (g_stat_write_hook) {
        g_stat_write_hook.reset();
        Log("hooks: removed stat-write hook");
    }

    if (g_stats_hook) {
        g_stats_hook.reset();
        Log("hooks: removed stats hook");
    }
}

void RemovePlayerHooks() {
    RemovePlayerStatHooks();

    if (g_player_pointer_hook) {
        g_player_pointer_hook.reset();
        Log("hooks: removed player-pointer hook");
    }
}


