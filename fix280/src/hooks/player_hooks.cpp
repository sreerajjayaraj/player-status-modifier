#include "hooks/hooks_internal.h"

#include "config.h"
#include "logger.h"
#include "mod_logic.h"
#include "runtime/actor_resolve.h"
#include "runtime/stat_logic.h"
#include "scanner.h"

#include <Windows.h>

#include <algorithm>
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
bool TryReadHookStatEntry(uintptr_t entry, int32_t* stat_type, int64_t* current_value, int64_t* max_value);

std::atomic<std::uint32_t> g_stamina_prenotify_logs{0};
std::atomic<uintptr_t> g_last_prenotify_stamina_entry{0};
std::atomic<int64_t> g_last_prenotify_stamina_value{0};
std::atomic<std::uint32_t> g_stamina_ab00_visual_write_logs{0};
std::atomic<std::uint32_t> g_stamina_prenotify_full_refresh_logs{0};
std::atomic<std::uint32_t> g_stamina_ab00_real_write_skip_logs{0};
std::atomic<std::uint32_t> g_stamina_prenotify_active_recovery_block_logs{0};
std::atomic<std::uint32_t> g_stamina_ab00_pair_cancel_logs{0};
std::atomic<std::uint32_t> g_stamina_ab00_pending_logs{0};
std::atomic<uintptr_t> g_stamina_ab00_pending_entry{0};
std::atomic<int64_t> g_stamina_ab00_pending_delta{0};
std::atomic<DWORD> g_stamina_ab00_pending_tick{0};
std::atomic<DWORD> g_stamina_ab00_activity_tick{0};

int64_t ScaleHookDelta(const int64_t delta, const double multiplier) {
    if (multiplier == 1.0 || delta == 0) {
        return delta;
    }

    const double scaled = static_cast<double>(delta) * multiplier;
    if (scaled >= 0.0) {
        return static_cast<int64_t>(scaled + 0.5);
    }
    return static_cast<int64_t>(scaled - 0.5);
}

bool TryAdjustTrackedStaminaPreNotify(const uintptr_t entry,
                                      const int64_t observed_value,
                                      int64_t* const adjusted_value) {
    if (adjusted_value == nullptr || entry < kMinimumPointerAddress) {
        return false;
    }

    const auto& config = GetConfig();
    if (!config.general.enabled || !config.stamina.enabled) {
        return false;
    }

    int32_t stat_type = 0;
    int64_t current_value = 0;
    int64_t max_value = 0;
    if (!TryReadHookStatEntry(entry, &stat_type, &current_value, &max_value) || stat_type != kStaminaId || max_value <= 0) {
        return false;
    }

    uintptr_t last_entry = g_last_prenotify_stamina_entry.load(std::memory_order_acquire);
    int64_t last_value = g_last_prenotify_stamina_value.load(std::memory_order_acquire);
    if (last_entry != entry || last_value < 0 || last_value > max_value) {
        last_value = std::clamp(observed_value, int64_t{0}, max_value);
        g_last_prenotify_stamina_entry.store(entry, std::memory_order_release);
        g_last_prenotify_stamina_value.store(last_value, std::memory_order_release);
        return false;
    }

    const int64_t delta = observed_value - last_value;
    if (delta == 0) {
        return false;
    }

    const DWORD now = GetTickCount();
    const DWORD ab00_activity_tick = g_stamina_ab00_activity_tick.load(std::memory_order_acquire);
    const bool recent_ab00_activity = ab00_activity_tick != 0 && now - ab00_activity_tick <= 1800;

    if (delta > 0 &&
        observed_value == max_value &&
        !WasRecentPlayerStaminaConsumption(entry, 1500) &&
        !recent_ab00_activity) {
        g_last_prenotify_stamina_value.store(observed_value, std::memory_order_release);

        const auto current = g_stamina_prenotify_full_refresh_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 64) {
            Log("hooks: stamina pre-notify accepted idle full refresh entry=0x%p old_cached=%lld observed=%lld max=%lld delta=%lld note=d40-full-refresh-reset",
                reinterpret_cast<void*>(entry),
                static_cast<long long>(last_value),
                static_cast<long long>(observed_value),
                static_cast<long long>(max_value),
                static_cast<long long>(delta));
        }
        return false;
    }

    if (delta > 0 && observed_value == max_value && WasRecentPlayerStaminaConsumption(entry, 900)) {
        const int64_t final_value = std::clamp(last_value, int64_t{0}, max_value);
        g_last_prenotify_stamina_value.store(final_value, std::memory_order_release);

        if (final_value != observed_value) {
            *reinterpret_cast<int64_t*>(entry + 0x08) = final_value;
            RecordPlayerStaminaPreNotifyWrite(entry, final_value);
            *adjusted_value = final_value;

            const auto current = g_stamina_prenotify_active_recovery_block_logs.fetch_add(1, std::memory_order_acq_rel);
            if (current < 128) {
                Log("hooks: stamina pre-notify held active full refresh entry=0x%p old_cached=%lld observed=%lld final=%lld max=%lld delta=%lld consumption=%.3f heal=%.3f note=d49-active-full-refresh-gate",
                    reinterpret_cast<void*>(entry),
                    static_cast<long long>(last_value),
                    static_cast<long long>(observed_value),
                    static_cast<long long>(final_value),
                    static_cast<long long>(max_value),
                    static_cast<long long>(delta),
                    config.stamina.consumption_multiplier,
                    config.stamina.heal_multiplier);
            }
            return true;
        }
        return false;
    }

    const double multiplier = delta < 0 ? config.stamina.consumption_multiplier : config.stamina.heal_multiplier;
    const int64_t scaled_delta = ScaleHookDelta(delta, multiplier);
    const int64_t final_value = std::clamp(last_value + scaled_delta, int64_t{0}, max_value);
    g_last_prenotify_stamina_value.store(final_value, std::memory_order_release);

    if (final_value == observed_value) {
        return false;
    }

    *reinterpret_cast<int64_t*>(entry + 0x08) = final_value;
    RecordPlayerStaminaPreNotifyWrite(entry, final_value);
    *adjusted_value = final_value;

    const auto current = g_stamina_prenotify_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 256) {
        Log("hooks: stamina pre-notify adjusted entry=0x%p old_cached=%lld observed=%lld final=%lld max=%lld delta=%lld scaled_delta=%lld consumption=%.3f heal=%.3f",
            reinterpret_cast<void*>(entry),
            static_cast<long long>(last_value),
            static_cast<long long>(observed_value),
            static_cast<long long>(final_value),
            static_cast<long long>(max_value),
            static_cast<long long>(delta),
            static_cast<long long>(scaled_delta),
            config.stamina.consumption_multiplier,
            config.stamina.heal_multiplier);
    }

    return true;
}

bool TryApplyTrackedStaminaAb00VisualCost(const uintptr_t entry,
                                          const int64_t original_delta,
                                          const char* const note) {
    if (entry < kMinimumPointerAddress || original_delta >= 0) {
        return false;
    }

    const auto& config = GetConfig();
    if (!config.general.enabled || !config.stamina.enabled) {
        return false;
    }

    int32_t stat_type = 0;
    int64_t current_value = 0;
    int64_t max_value = 0;
    if (!TryReadHookStatEntry(entry, &stat_type, &current_value, &max_value) || stat_type != kStaminaId || max_value <= 0) {
        return false;
    }

    if (WasRecentPlayerStaminaPreNotifyWrite(entry, 175)) {
        const auto log_index = g_stamina_ab00_real_write_skip_logs.fetch_add(1, std::memory_order_acq_rel);
        if (log_index < 96) {
            Log("hooks: stamina-ab00 visual skipped after real stamina write entry=0x%p current=%lld max=%lld original_delta=%lld note=d41-real-write-guard",
                reinterpret_cast<void*>(entry),
                static_cast<long long>(current_value),
                static_cast<long long>(max_value),
                static_cast<long long>(original_delta));
        }
        return false;
    }

    const int64_t cost = std::min<int64_t>(-original_delta, max_value);
    // Large ab00 deltas also fire for weapon draw/sheathe style state refreshes.
    // Let the existing stat-write/watcher path handle those; this visual publish
    // is only for sustained movement drains that otherwise keep the bar hidden.
    const int64_t movement_cost_ceiling = std::max<int64_t>(50000, max_value / 8);
    if (cost > movement_cost_ceiling) {
        const auto log_index = g_stamina_ab00_visual_write_logs.fetch_add(1, std::memory_order_acq_rel);
        if (log_index < 384) {
            Log("hooks: stamina-ab00 visual current skipped large cost entry=0x%p current=%lld max=%lld original_delta=%lld ceiling=%lld note=d38-large-cost-callertrace",
                reinterpret_cast<void*>(entry),
                static_cast<long long>(current_value),
                static_cast<long long>(max_value),
                static_cast<long long>(original_delta),
                static_cast<long long>(movement_cost_ceiling));
        }
        return false;
    }

    int64_t base_value = current_value;
    TryGetPlayerStaminaAb00VirtualBase(entry, current_value, max_value, config, &base_value);

    const int64_t scaled_cost = std::max<int64_t>(1, ScaleHookDelta(cost, config.stamina.consumption_multiplier));
    const int64_t final_value = std::clamp(base_value - scaled_cost, int64_t{0}, max_value);
    if (final_value == current_value) {
        return false;
    }

    *reinterpret_cast<int64_t*>(entry + 0x08) = final_value;
    RecordPlayerStaminaAb00VirtualCost(entry, final_value, max_value);
    g_last_prenotify_stamina_entry.store(entry, std::memory_order_release);
    g_last_prenotify_stamina_value.store(final_value, std::memory_order_release);
    RecordPlayerStaminaPreNotifyWrite(entry, final_value);

    const auto log_index = g_stamina_ab00_visual_write_logs.fetch_add(1, std::memory_order_acq_rel);
    if (log_index < 384) {
        Log("hooks: stamina-ab00 visual current write entry=0x%p old=%lld base=%lld final=%lld max=%lld original_delta=%lld scaled_cost=%lld consumption=%.3f note=%s",
            reinterpret_cast<void*>(entry),
            static_cast<long long>(current_value),
            static_cast<long long>(base_value),
            static_cast<long long>(final_value),
            static_cast<long long>(max_value),
            static_cast<long long>(original_delta),
            static_cast<long long>(scaled_cost),
            config.stamina.consumption_multiplier,
            note != nullptr ? note : "d43-ab00-commit");
    }

    return true;
}

bool TryCommitPendingTrackedStaminaAb00VisualCost(const DWORD now, const char* const note) {
    static_cast<void>(now);
    const uintptr_t pending_entry = g_stamina_ab00_pending_entry.exchange(0, std::memory_order_acq_rel);
    const int64_t pending_delta = g_stamina_ab00_pending_delta.exchange(0, std::memory_order_acq_rel);
    g_stamina_ab00_pending_tick.store(0, std::memory_order_release);
    if (pending_entry < kMinimumPointerAddress || pending_delta >= 0) {
        return false;
    }
    return TryApplyTrackedStaminaAb00VisualCost(pending_entry, pending_delta, note);
}

bool TryPublishTrackedStaminaAb00VisualCost(const uintptr_t entry,
                                                 const int64_t original_delta,
                                                 bool* const suppress_original_delta) {
    if (suppress_original_delta != nullptr) {
        *suppress_original_delta = false;
    }
    if (entry < kMinimumPointerAddress || original_delta == 0) {
        return false;
    }

    constexpr DWORD kPairCancelWindowMs = 180;
    constexpr DWORD kPendingCommitWindowMs = 260;
    const DWORD now = GetTickCount();
    g_stamina_ab00_activity_tick.store(now, std::memory_order_release);
    const uintptr_t pending_entry = g_stamina_ab00_pending_entry.load(std::memory_order_acquire);
    const int64_t pending_delta = g_stamina_ab00_pending_delta.load(std::memory_order_acquire);
    const DWORD pending_tick = g_stamina_ab00_pending_tick.load(std::memory_order_acquire);
    const DWORD pending_age = pending_tick != 0 ? now - pending_tick : 0;

    if (original_delta > 0) {
        if (pending_entry == entry && pending_delta < 0 &&
            pending_age <= kPairCancelWindowMs &&
            std::min<int64_t>(original_delta, -pending_delta) >= (std::max<int64_t>(original_delta, -pending_delta) * 3) / 4) {
            g_stamina_ab00_pending_entry.store(0, std::memory_order_release);
            g_stamina_ab00_pending_delta.store(0, std::memory_order_release);
            g_stamina_ab00_pending_tick.store(0, std::memory_order_release);

            const auto log_index = g_stamina_ab00_pair_cancel_logs.fetch_add(1, std::memory_order_acq_rel);
            if (log_index < 128) {
                Log("hooks: stamina-ab00 transient pair canceled entry=0x%p pending_delta=%lld refund_delta=%lld age=%lu note=d43-terrain-turn-pair-cancel",
                    reinterpret_cast<void*>(entry),
                    static_cast<long long>(pending_delta),
                    static_cast<long long>(original_delta),
                    static_cast<unsigned long>(pending_age));
            }
            if (suppress_original_delta != nullptr) {
                *suppress_original_delta = true;
            }
            return false;
        }

        if (pending_entry >= kMinimumPointerAddress && pending_delta < 0 && pending_age > kPendingCommitWindowMs) {
            const bool committed = TryCommitPendingTrackedStaminaAb00VisualCost(now, "d63-expired-before-positive-suppress-raw");
            if (suppress_original_delta != nullptr) {
                *suppress_original_delta = true;
            }
            return committed;
        }
        if (pending_entry == entry && pending_delta < 0) {
            if (suppress_original_delta != nullptr) {
                *suppress_original_delta = true;
            }
        }
        return false;
    }

    if (pending_entry >= kMinimumPointerAddress && pending_delta < 0) {
        if (pending_entry != entry || pending_age > kPairCancelWindowMs) {
            TryCommitPendingTrackedStaminaAb00VisualCost(now, "d43-sustained-movement-commit");
        }
    }

    g_stamina_ab00_pending_entry.store(entry, std::memory_order_release);
    g_stamina_ab00_pending_delta.store(original_delta, std::memory_order_release);
    g_stamina_ab00_pending_tick.store(now, std::memory_order_release);
    if (suppress_original_delta != nullptr) {
        *suppress_original_delta = true;
    }

    const auto log_index = g_stamina_ab00_pending_logs.fetch_add(1, std::memory_order_acq_rel);
    if (log_index < 128) {
        Log("hooks: stamina-ab00 visual cost pending entry=0x%p original_delta=%lld note=d43-await-refund-or-sustain",
            reinterpret_cast<void*>(entry),
            static_cast<long long>(original_delta));
    }
    return false;
}
uintptr_t ReadStackQwordSafe(const uintptr_t rsp, const size_t index) {
    if (rsp < kMinimumPointerAddress) {
        return 0;
    }
    __try {
        return *reinterpret_cast<const uintptr_t*>(rsp + index * sizeof(uintptr_t));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

#ifndef VK_GAMEPAD_LEFT_THUMBSTICK_BUTTON
#define VK_GAMEPAD_LEFT_THUMBSTICK_BUTTON 0xC8
#endif

#ifndef VK_GAMEPAD_RIGHT_THUMBSTICK_BUTTON
#define VK_GAMEPAD_RIGHT_THUMBSTICK_BUTTON 0xC9
#endif

using GetAsyncKeyStateFn = SHORT(WINAPI*)(int);

bool TryReadHookStatEntry(uintptr_t entry, int32_t* stat_type, int64_t* current_value, int64_t* max_value);

GetAsyncKeyStateFn ResolveGetAsyncKeyState() {
    static HMODULE user32 = LoadLibraryW(L"user32.dll");
    static GetAsyncKeyStateFn fn = user32
        ? reinterpret_cast<GetAsyncKeyStateFn>(GetProcAddress(user32, "GetAsyncKeyState"))
        : nullptr;
    return fn;
}

bool IsKeyDown(const int vk) {
    const auto get_async_key_state = ResolveGetAsyncKeyState();
    return vk != 0 && get_async_key_state != nullptr && (get_async_key_state(vk) & 0x8000) != 0;
}

bool IsFocusComboWindowActive() {
    constexpr DWORD kFocusComboWindowMs = 750;
    const DWORD now = GetTickCount();

    if (IsKeyDown('X') ||
        (IsKeyDown(VK_GAMEPAD_LEFT_THUMBSTICK_BUTTON) && IsKeyDown(VK_GAMEPAD_RIGHT_THUMBSTICK_BUTTON))) {
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

    __try {
        const uintptr_t tracked_player_health = GetTrackedPlayerHealthEntry();
        const uintptr_t tracked_player_stamina = GetTrackedPlayerStaminaEntry();
        const uintptr_t tracked_player_spirit = GetTrackedPlayerSpiritEntry();
        const bool exact_tracked_player_entry =
            (tracked_player_health >= kMinimumPointerAddress && ctx.rdi == tracked_player_health) ||
            (tracked_player_stamina >= kMinimumPointerAddress && ctx.rdi == tracked_player_stamina) ||
            (tracked_player_spirit >= kMinimumPointerAddress && ctx.rdi == tracked_player_spirit);
        const bool value_in_r8_rsi = ctx.r8 == ctx.rsi && ctx.r8 != 0;
        int64_t tracked_entry_max = 0;
        bool r8_rsi_counter_value = false;
        bool r8_rsi_plausible_stat_value = false;
        if (exact_tracked_player_entry) {
            tracked_entry_max = *reinterpret_cast<const int64_t*>(ctx.rdi + 0x18);
            if (value_in_r8_rsi) {
                const int64_t r8_value = static_cast<int64_t>(ctx.r8);
                r8_rsi_counter_value = (ctx.r8 == ctx.r13) || (ctx.r8 == ctx.rbp);
                r8_rsi_plausible_stat_value =
                    tracked_entry_max > 0 &&
                    r8_value >= 0 &&
                    r8_value <= tracked_entry_max &&
                    !r8_rsi_counter_value;
            }
        }

        // In 1.11, exact tracked entries often carry a matching r8/rsi frame
        // counter (also visible in r13/rbp), while rbx carries the stat value.
        // Only protect r8/rsi when it is both range-valid and not that counter.
        const bool use_r8_rsi_value =
            exact_tracked_player_entry &&
            ctx.rdi != tracked_player_stamina &&
            value_in_r8_rsi &&
            r8_rsi_plausible_stat_value;
        const int64_t observed_value = static_cast<int64_t>(use_r8_rsi_value ? ctx.r8 : ctx.rbx);
        const char* value_source = use_r8_rsi_value
            ? "r8/rsi"
            : (exact_tracked_player_entry && value_in_r8_rsi ? "rbx-counter-r8/rsi" : "rbx");
        const bool log_sample = exact_tracked_player_entry && ShouldLogSample(g_stat_write_samples, 48);
        if (log_sample) {
            Log("hooks: stat-write callback entry=0x%p value=%lld source=%s rbx=%lld rsi=%lld r8=%lld max=%lld r13=%lld rbp=%lld r8_counter=%d r8_plausible=%d tracked_player_health=0x%p tracked_player_stamina=0x%p tracked_player_spirit=0x%p matched=%d rip=0x%p",
                reinterpret_cast<void*>(ctx.rdi),
                static_cast<long long>(observed_value),
                value_source,
                static_cast<long long>(ctx.rbx),
                static_cast<long long>(ctx.rsi),
                static_cast<long long>(ctx.r8),
                static_cast<long long>(tracked_entry_max),
                static_cast<long long>(ctx.r13),
                static_cast<long long>(ctx.rbp),
                r8_rsi_counter_value ? 1 : 0,
                r8_rsi_plausible_stat_value ? 1 : 0,
                reinterpret_cast<void*>(tracked_player_health),
                reinterpret_cast<void*>(tracked_player_stamina),
                reinterpret_cast<void*>(tracked_player_spirit),
                exact_tracked_player_entry ? 1 : 0,
                reinterpret_cast<void*>(ctx.rip));
        }

        if (!exact_tracked_player_entry) {
            LogResourceWriteCandidate(ctx, tracked_player_health, tracked_player_stamina, tracked_player_spirit, false);
            LogSpecialMountHealthWriteCandidate(ctx);
        }

        if (use_r8_rsi_value) {
            return;
        }

        int64_t adjusted_value = observed_value;
        // d71: do not override player-stamina stat-write refills from the virtual stamina cache.
        // On 1.12.01 save/load this path can see a valid full refill while the cached virtual
        // value is stale or zero, which pins Kliff stamina empty. The authoritative cost scaling
        // remains in the stamina delta hook below.

        adjusted_value = observed_value;
        if (ctx.rdi == tracked_player_stamina && !use_r8_rsi_value && TryAdjustTrackedStaminaPreNotify(ctx.rdi, observed_value, &adjusted_value)) {
            ctx.rbx = static_cast<uintptr_t>(adjusted_value);
            if (log_sample) {
                Log("hooks: stat-write pre-notify adjusted tracked stamina entry=0x%p final=%lld source=%s",
                    reinterpret_cast<void*>(ctx.rdi),
                    static_cast<long long>(adjusted_value),
                    value_source);
            }
            return;
        }

        adjusted_value = observed_value;
        if (TryAdjustStatWrite(ctx.rdi, &adjusted_value, ctx.r14)) {
            if (use_r8_rsi_value) {
                ctx.r8 = static_cast<uintptr_t>(adjusted_value);
                ctx.rsi = static_cast<uintptr_t>(adjusted_value);
            } else {
                ctx.rbx = static_cast<uintptr_t>(adjusted_value);
            }
            if (log_sample) {
                Log("hooks: stat-write adjusted entry=0x%p final=%lld source=%s",
                    reinterpret_cast<void*>(ctx.rdi),
                    static_cast<long long>(adjusted_value),
                    value_source);
            } else if (!exact_tracked_player_entry) {
                const auto current = g_resource_write_candidate_logs.fetch_add(1, std::memory_order_acq_rel);
                if (current < 160) {
                    Log("hooks: stat-write adjusted candidate rbx entry=0x%p old=%lld final=%lld r8=%lld rsi=%lld rip=0x%p",
                        reinterpret_cast<void*>(ctx.rdi),
                        static_cast<long long>(observed_value),
                        static_cast<long long>(adjusted_value),
                        static_cast<long long>(ctx.r8),
                        static_cast<long long>(ctx.rsi),
                        reinterpret_cast<void*>(ctx.rip));
                }
            }
        }
        if (!exact_tracked_player_entry && !value_in_r8_rsi && ctx.r8 == ctx.rsi && ctx.r8 != ctx.rbx) {
            const int64_t original_arg_value = static_cast<int64_t>(ctx.r8);
            int64_t adjusted_arg_value = static_cast<int64_t>(ctx.r8);
            if (adjusted_arg_value >= 0 && TryAdjustStatWrite(ctx.rdi, &adjusted_arg_value, ctx.r14)) {
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
    if (raw_count < 192) {
        const uintptr_t ret0 = ReadStackQwordSafe(ctx.rsp, 0);
        const uintptr_t ret1 = ReadStackQwordSafe(ctx.rsp, 1);
        const uintptr_t ret2 = ReadStackQwordSafe(ctx.rsp, 2);
        const uintptr_t ret3 = ReadStackQwordSafe(ctx.rsp, 3);
        const uintptr_t ret4 = ReadStackQwordSafe(ctx.rsp, 4);
        const uintptr_t ret5 = ReadStackQwordSafe(ctx.rsp, 5);
        Log("hooks: stamina-ab00 roottrace #%d rax=0x%p rbx=%lld rcx=0x%p rdx=0x%p r8=%lld r9=%lld r10=0x%p r11=0x%p r12=0x%p r13=0x%p r14=0x%p r15=0x%p rsi=0x%p rbp=%lld rdi=0x%p rsp=0x%p rip=0x%p ret0=0x%p ret1=0x%p ret2=0x%p ret3=0x%p ret4=0x%p ret5=0x%p note=d38-callertrace",
            raw_count,
            reinterpret_cast<void*>(ctx.rax),
            static_cast<long long>(ctx.rbx),
            reinterpret_cast<void*>(ctx.rcx),
            reinterpret_cast<void*>(ctx.rdx),
            static_cast<long long>(ctx.r8),
            static_cast<long long>(ctx.r9),
            reinterpret_cast<void*>(ctx.r10),
            reinterpret_cast<void*>(ctx.r11),
            reinterpret_cast<void*>(ctx.r12),
            reinterpret_cast<void*>(ctx.r13),
            reinterpret_cast<void*>(ctx.r14),
            reinterpret_cast<void*>(ctx.r15),
            reinterpret_cast<void*>(ctx.rsi),
            static_cast<long long>(ctx.rbp),
            reinterpret_cast<void*>(ctx.rdi),
            reinterpret_cast<void*>(ctx.rsp),
            reinterpret_cast<void*>(ctx.rip),
            reinterpret_cast<void*>(ret0),
            reinterpret_cast<void*>(ret1),
            reinterpret_cast<void*>(ret2),
            reinterpret_cast<void*>(ret3),
            reinterpret_cast<void*>(ret4),
            reinterpret_cast<void*>(ret5));
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
            // d49: AB00 is a movement/visual callback and can fire on detached Kliff stamina mirrors.
            // Let the normal status resolver/watcher choose the player resource block first.
            if (!HasReadableTrackedPlayerCombatPairForHooks()) {
                const auto promote_skip = g_stamina_ab00_pending_logs.fetch_add(1, std::memory_order_acq_rel);
                if (promote_skip < 64) {
                    Log("hooks: stamina-ab00 skipped bootstrap from untracked entry=0x%p note=d49-no-ab00-promotion", reinterpret_cast<void*>(entry));
                }
            }
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

        const ActorResolveSnapshot tracked_mount_snapshot = g_mount_resolve;
        const bool entry_is_trusted_mount_stamina =
            tracked_mount_snapshot.valid() &&
            g_mount_track_confidence.load(std::memory_order_acquire) >= 3 &&
            entry == tracked_mount_snapshot.stamina_entry;
        if (entry_is_trusted_mount_stamina) {
            UpdateTrackedMountFromStaminaContext(entry, ctx.r14);
            const auto& config = GetConfig();
            if (config.mount.enabled &&
                (config.mount.lock_stamina ||
                 (config.mount.special_enabled && config.mount.lock_special_stamina)) &&
                static_cast<int64_t>(ctx.rbx) < 0) {
                ctx.rbx = 0;
            }
            if (log_sample) {
                Log("hooks: stamina-ab00 routed trusted mount stamina entry=0x%p delta=%lld player_stamina=0x%p mount_stamina=0x%p",
                    reinterpret_cast<void*>(entry),
                    static_cast<long long>(ctx.rbx),
                    reinterpret_cast<void*>(tracked_player_stamina),
                    reinterpret_cast<void*>(tracked_mount_snapshot.stamina_entry));
            }
            return;
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
            const int64_t player_stamina_delta = static_cast<int64_t>(ctx.rbx);
            int64_t adjusted_delta = player_stamina_delta;
            const bool adjusted = TryAdjustStaminaDelta(entry, &adjusted_delta);
            if (adjusted) {
                ctx.rbx = static_cast<uintptr_t>(adjusted_delta);
                const auto adjust_log = g_stamina_ab00_real_write_skip_logs.fetch_add(1, std::memory_order_acq_rel);
                if (adjust_log < 128) {
                    Log("hooks: stamina-ab00 scaled tracked player delta entry=0x%p old=%lld final=%lld note=d68-authoritative-no-pair-helper",
                        reinterpret_cast<void*>(entry),
                        static_cast<long long>(player_stamina_delta),
                        static_cast<long long>(adjusted_delta));
                }
            }
            UpdateTrackedPlayerResourceOwner(ctx.r14, "stamina-ab00 tracked player owner");
            const bool trace_sample = ShouldLogSample(g_stamina_ab00_pair_trace_samples, 128);
            if (log_sample || trace_sample) {
                int32_t trace_type = 0;
                int64_t trace_current = 0;
                int64_t trace_max = 0;
                const bool trace_read_ok = TryReadHookStatEntry(entry, &trace_type, &trace_current, &trace_max);
                Log("hooks: stamina-ab00 player stamina passthrough entry=0x%p delta=%lld owner=0x%p status=%u rdi=0x%p r14=0x%p current=%lld max=%lld read_ok=%d note=d68-movement-callertrace",
                    reinterpret_cast<void*>(entry),
                    static_cast<long long>(player_stamina_delta),
                    reinterpret_cast<void*>(ctx.r14),
                    static_cast<unsigned>(ctx.rdi & 0xFFFFu),
                    reinterpret_cast<void*>(ctx.rdi),
                    reinterpret_cast<void*>(ctx.r14),
                    static_cast<long long>(trace_current),
                    static_cast<long long>(trace_max),
                    trace_read_ok ? 1 : 0);
            }
            return;
        }

        if (static_cast<int64_t>(ctx.rbx) < 0) {
            const auto skip_log = g_stamina_ab00_pending_logs.fetch_add(1, std::memory_order_acq_rel);
            if (skip_log < 64) {
                Log("hooks: stamina-ab00 skipped untracked player-shaped cost entry=0x%p delta=%lld tracked_player_stamina=0x%p note=d49-no-ab00-promotion",
                    reinterpret_cast<void*>(entry),
                    static_cast<long long>(ctx.rbx),
                    reinterpret_cast<void*>(tracked_player_stamina));
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

















