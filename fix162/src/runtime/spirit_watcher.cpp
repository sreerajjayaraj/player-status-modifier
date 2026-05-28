#include "runtime/spirit_watcher.h"

#include "config.h"
#include "logger.h"
#include "mod_logic.h"
#include "runtime/actor_resolve.h"
#include "runtime/runtime_state.h"
#include "runtime/stat_logic.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <cstdint>

namespace {

constexpr DWORD kSpiritWatcherPollMs = 100;
constexpr DWORD kSpiritWatcherSnapshotMs = 5000;
constexpr DWORD kSpiritRecoveryLockoutMaxMs = 1000;
constexpr DWORD kVisibleSpiritCostWindowMs = 1000;
constexpr uint32_t kObservedConsumptionAcceptWindowMs = 1500;
constexpr int64_t kPlayerResourceMaxCeiling = 1000000;
constexpr int64_t kPassiveRegenMaxPermille = 30;
constexpr intptr_t kPostRematchAltStaminaOffsetFromHealth = -0x260;
constexpr uintptr_t kPostRematchAltSpiritOffsetFromHealth = 0x9B0;
constexpr int64_t kPostRematchHealthMax = 300000;
constexpr int64_t kPostRematchResourceMax = 100000;
std::atomic<bool> g_spirit_watcher_running{false};
HANDLE g_spirit_watcher_thread = nullptr;
std::atomic<std::uint32_t> g_spirit_watcher_logs{0};
std::atomic<std::uint32_t> g_spirit_watcher_snapshot_logs{0};
std::atomic<std::uint32_t> g_stamina_watcher_logs{0};
std::atomic<std::uint32_t> g_stamina_watcher_snapshot_logs{0};
std::atomic<std::uint32_t> g_spirit_post_drain_scan_logs{0};
std::atomic<std::uint32_t> g_spirit_ui_candidate_sample_logs{0};
std::atomic<std::uint32_t> g_player_resource_change_logs{0};
std::atomic<std::uint32_t> g_focus_recovery_tick_logs{0};

struct ResourceTraceSlot {
    uintptr_t entry = 0;
    int32_t type = 0;
    int64_t current = 0;
    int64_t max = 0;
    bool seen = false;
};

ResourceTraceSlot g_resource_trace_slots[128]{};

struct FloatCandidateOffset {
    const char* base_name;
    uintptr_t offset;
};

constexpr FloatCandidateOffset kSpiritUiCandidateOffsets[] = {
    {"player-root", 0xA24},
    {"player-root", 0x15A0},
    {"player-root", 0x2014},
    {"player-root", 0x1A28},
    {"player-root", 0xA10},
    {"player-marker", 0x2B64},
    {"player-marker", 0x2E78},
    {"player-marker", 0x2DC4},
};

bool FloatNear(const float value, const float expected, const float epsilon) {
    const float delta = value > expected ? value - expected : expected - value;
    return delta <= epsilon;
}

uintptr_t ResolveSpiritUiCandidateBase(const char* const base_name) {
    if (base_name == nullptr) {
        return 0;
    }
    if (strcmp(base_name, "player-root") == 0) {
        return GetTrackedPlayerStatRoot();
    }
    if (strcmp(base_name, "player-marker") == 0) {
        return GetTrackedPlayerStatusMarker();
    }
    return 0;
}

bool TryReadFloat(const uintptr_t address, float* const value) {
    if (address < kMinimumPointerAddress || value == nullptr) {
        return false;
    }

    __try {
        *value = *reinterpret_cast<const float*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool IsPassiveRecoveryTick(const int64_t delta, const int64_t max_value) {
    if (delta <= 0 || max_value <= 0) {
        return false;
    }

    const int64_t passive_ceiling = std::max<int64_t>(1000, (max_value * kPassiveRegenMaxPermille) / 1000);
    return delta <= passive_ceiling;
}

bool ShouldBoostObservedSpiritRecovery(const int64_t delta,
                                       const int64_t max_value,
                                       const DWORD last_consumption_tick,
                                       const DWORD lockout_ms,
                                       const DWORD now) {
    static_cast<void>(last_consumption_tick);
    static_cast<void>(lockout_ms);
    static_cast<void>(now);
    if (IsFocusRecoveryWindowActive() || IsPassiveRecoveryTick(delta, max_value)) {
        return true;
    }

    return false;
}

bool IsPrimaryTrackedSpiritEntry(const uintptr_t entry, const int32_t stat_type, const int64_t max_value) {
    const uintptr_t health_entry = GetTrackedPlayerHealthEntry();
    return stat_type == kSpiritId &&
           health_entry >= kMinimumPointerAddress &&
           entry == health_entry + kSpiritEntryOffsetFromHealth &&
           max_value >= 100000 &&
           max_value <= 220000;
}

bool ShouldAcceptObservedSpiritConsumptionWithoutRecentHook(const uintptr_t entry,
                                                            const int32_t stat_type,
                                                            const int64_t previous_value,
                                                            const int64_t observed_value,
                                                            const int64_t max_value) {
    if (previous_value <= observed_value ||
        observed_value < 0 ||
        previous_value > max_value ||
        !IsPrimaryTrackedSpiritEntry(entry, stat_type, max_value)) {
        return false;
    }

    const int64_t delta = previous_value - observed_value;
    return delta >= 1000;
}

bool ShouldBoostObservedStaminaRecovery(const int64_t delta, const int64_t max_value) {
    return delta > 0 && max_value > 0;
}

int64_t ScaleObservedRecoveryDelta(const int64_t delta, const double multiplier) {
    if (multiplier <= 0.0) {
        return delta;
    }
    return ScaleDelta(delta, multiplier);
}

void LogSpiritUiCandidateSamples(const uintptr_t spirit_entry,
                                 const int64_t current_value,
                                 const int64_t max_value,
                                 DWORD* const last_sample_tick) {
    if (last_sample_tick == nullptr ||
        spirit_entry < kMinimumPointerAddress ||
        max_value <= 0 ||
        current_value < 0 ||
        current_value > max_value) {
        return;
    }

    const auto log_index = g_spirit_ui_candidate_sample_logs.load(std::memory_order_acquire);
    if (log_index >= 160) {
        return;
    }

    const DWORD now = GetTickCount();
    if (current_value >= max_value && now - *last_sample_tick < 5000) {
        return;
    }
    if (now - *last_sample_tick < 500) {
        return;
    }
    *last_sample_tick = now;

    const double normalized = static_cast<double>(current_value) / static_cast<double>(max_value);
    const auto header_index = g_spirit_ui_candidate_sample_logs.fetch_add(1, std::memory_order_acq_rel);
    if (header_index >= 160) {
        return;
    }

    Log("runtime: spirit ui candidate sample begin entry=0x%p current=%lld max=%lld normalized=%.6f root=0x%p marker=0x%p",
        reinterpret_cast<void*>(spirit_entry),
        static_cast<long long>(current_value),
        static_cast<long long>(max_value),
        normalized,
        reinterpret_cast<void*>(GetTrackedPlayerStatRoot()),
        reinterpret_cast<void*>(GetTrackedPlayerStatusMarker()));

    for (const auto& candidate : kSpiritUiCandidateOffsets) {
        const uintptr_t base = ResolveSpiritUiCandidateBase(candidate.base_name);
        if (base < kMinimumPointerAddress) {
            continue;
        }

        const uintptr_t address = base + candidate.offset;
        float value = 0.0f;
        if (!TryReadFloat(address, &value)) {
            continue;
        }

        const double value_as_double = static_cast<double>(value);
        const double diff = value_as_double > normalized ? value_as_double - normalized : normalized - value_as_double;
        const auto line_index = g_spirit_ui_candidate_sample_logs.fetch_add(1, std::memory_order_acq_rel);
        if (line_index >= 160) {
            return;
        }

        Log("runtime: spirit ui candidate sample base=%s offset=0x%llX addr=0x%p float=%.6f normalized=%.6f diff=%.6f current=%lld max=%lld",
            candidate.base_name,
            static_cast<unsigned long long>(candidate.offset),
            reinterpret_cast<void*>(address),
            value_as_double,
            normalized,
            diff,
            static_cast<long long>(current_value),
            static_cast<long long>(max_value));
    }
}

void TraceNearbyPlayerResourceChanges(const uintptr_t health_entry, const char* const reason) {
    if (health_entry < kMinimumPointerAddress) {
        return;
    }

    for (intptr_t signed_offset = -0x400; signed_offset <= 0xA00; signed_offset += 0x10) {
        const uintptr_t entry = static_cast<uintptr_t>(static_cast<intptr_t>(health_entry) + signed_offset);
        if (entry < kMinimumPointerAddress) {
            continue;
        }

        __try {
            const int32_t stat_type = *reinterpret_cast<const int32_t*>(entry);
            if (stat_type != 17 && stat_type != 18 && stat_type != 19 && stat_type != 20) {
                continue;
            }

            const int64_t current_value = *reinterpret_cast<const int64_t*>(entry + 0x08);
            const int64_t max_value = *reinterpret_cast<const int64_t*>(entry + 0x18);
            if (max_value <= 0 ||
                current_value < 0 ||
                current_value > max_value ||
                max_value > kPlayerResourceMaxCeiling) {
                continue;
            }

            ResourceTraceSlot* slot = nullptr;
            ResourceTraceSlot* empty_slot = nullptr;
            for (auto& candidate : g_resource_trace_slots) {
                if (candidate.seen && candidate.entry == entry) {
                    slot = &candidate;
                    break;
                }
                if (!candidate.seen && empty_slot == nullptr) {
                    empty_slot = &candidate;
                }
            }

            if (slot == nullptr) {
                slot = empty_slot;
            }
            if (slot == nullptr) {
                continue;
            }

            const bool changed = !slot->seen ||
                slot->type != stat_type ||
                slot->current != current_value ||
                slot->max != max_value;
            if (!changed) {
                continue;
            }

            const int64_t old_current = slot->seen ? slot->current : -1;
            const int64_t old_max = slot->seen ? slot->max : -1;
            slot->seen = true;
            slot->entry = entry;
            slot->type = stat_type;
            slot->current = current_value;
            slot->max = max_value;

            const auto log_index = g_player_resource_change_logs.fetch_add(1, std::memory_order_acq_rel);
            if (log_index >= 512) {
                continue;
            }

            Log("runtime: player resource trace reason=%s offset=%+lld entry=0x%p type=%d old=%lld/%lld new=%lld/%lld tracked=%d",
                reason != nullptr ? reason : "unknown",
                static_cast<long long>(signed_offset),
                reinterpret_cast<void*>(entry),
                stat_type,
                static_cast<long long>(old_current),
                static_cast<long long>(old_max),
                static_cast<long long>(current_value),
                static_cast<long long>(max_value),
                static_cast<int>(ClassifyTrackedStatEntry(entry)));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
}

void ScanSpiritPostDrainCacheNearPointer(const char* const name,
                                         const uintptr_t base,
                                         const uintptr_t spirit_entry,
                                         const int64_t current_value,
                                         const int64_t max_value,
                                         std::uint32_t* const emitted) {
    if (name == nullptr || emitted == nullptr || *emitted >= 48 || base < kMinimumPointerAddress || max_value <= 0) {
        return;
    }

    const float normalized = static_cast<float>(static_cast<double>(current_value) / static_cast<double>(max_value));
    const float display_max_float = static_cast<float>(static_cast<double>(max_value) / 1000.0);

    for (uintptr_t offset = 0; offset <= 0x3000 && *emitted < 48; offset += 4) {
        const uintptr_t address = base + offset;
        __try {
            const int32_t dword_value = *reinterpret_cast<const int32_t*>(address);
            const int64_t qword_value = *reinterpret_cast<const int64_t*>(address);
            const float float_value = *reinterpret_cast<const float*>(address);
            uintptr_t ptr_value = 0;
            if ((offset % sizeof(uintptr_t)) == 0) {
                ptr_value = *reinterpret_cast<const uintptr_t*>(address);
            }

            const bool int_match =
                dword_value == static_cast<int32_t>(current_value) ||
                dword_value == static_cast<int32_t>(max_value) ||
                qword_value == current_value ||
                qword_value == max_value;
            const bool float_match =
                FloatNear(float_value, normalized, 0.00075f) ||
                FloatNear(float_value, display_max_float, 0.01f);
            const bool ptr_match = ptr_value == spirit_entry;

            if (!int_match && !float_match && !ptr_match) {
                continue;
            }

            Log("runtime: spirit post-drain cache scan base=%s ptr=0x%p offset=0x%llX addr=0x%p dword=%d qword=%lld float=%.6f ptr_value=0x%p current=%lld max=%lld normalized=%.6f match=%s%s%s",
                name,
                reinterpret_cast<void*>(base),
                static_cast<unsigned long long>(offset),
                reinterpret_cast<void*>(address),
                dword_value,
                static_cast<long long>(qword_value),
                static_cast<double>(float_value),
                reinterpret_cast<void*>(ptr_value),
                static_cast<long long>(current_value),
                static_cast<long long>(max_value),
                static_cast<double>(normalized),
                int_match ? "int" : "",
                float_match ? "|float" : "",
                ptr_match ? "|ptr" : "");
            ++(*emitted);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
}

void LogSpiritPostDrainCacheScan(const uintptr_t spirit_entry,
                                 const int64_t current_value,
                                 const int64_t max_value) {
    if (spirit_entry < kMinimumPointerAddress ||
        max_value <= 0 ||
        current_value <= 0 ||
        current_value >= max_value) {
        return;
    }

    const auto scan_index = g_spirit_post_drain_scan_logs.fetch_add(1, std::memory_order_acq_rel);
    if (scan_index >= 4) {
        return;
    }

    std::uint32_t emitted = 0;
    Log("runtime: spirit post-drain cache scan begin entry=0x%p current=%lld max=%lld actor=0x%p root=0x%p marker=0x%p health=0x%p",
        reinterpret_cast<void*>(spirit_entry),
        static_cast<long long>(current_value),
        static_cast<long long>(max_value),
        reinterpret_cast<void*>(GetTrackedPlayerActor()),
        reinterpret_cast<void*>(GetTrackedPlayerStatRoot()),
        reinterpret_cast<void*>(GetTrackedPlayerStatusMarker()),
        reinterpret_cast<void*>(GetTrackedPlayerHealthEntry()));
    ScanSpiritPostDrainCacheNearPointer("player-root", GetTrackedPlayerStatRoot(), spirit_entry, current_value, max_value, &emitted);
    ScanSpiritPostDrainCacheNearPointer("player-marker", GetTrackedPlayerStatusMarker(), spirit_entry, current_value, max_value, &emitted);
    ScanSpiritPostDrainCacheNearPointer("player-health", GetTrackedPlayerHealthEntry(), spirit_entry, current_value, max_value, &emitted);
    ScanSpiritPostDrainCacheNearPointer("player-actor", GetTrackedPlayerActor(), spirit_entry, current_value, max_value, &emitted);
    Log("runtime: spirit post-drain cache scan end entry=0x%p emitted=%u",
        reinterpret_cast<void*>(spirit_entry),
        emitted);
}

bool TryReadSpiritEntry(const uintptr_t entry,
                        int32_t* const stat_type,
                        int64_t* const current_value,
                        int64_t* const max_value) {
    if (entry < kMinimumPointerAddress ||
        stat_type == nullptr ||
        current_value == nullptr ||
        max_value == nullptr) {
        return false;
    }

    __try {
        const int32_t type = *reinterpret_cast<const int32_t*>(entry);
        if (!IsSpiritStatId(type)) {
            return false;
        }

        const int64_t current = *reinterpret_cast<const int64_t*>(entry + 0x08);
        const int64_t maximum = *reinterpret_cast<const int64_t*>(entry + 0x18);
        if (maximum <= 0 || maximum > kPlayerResourceMaxCeiling || current < 0 || current > maximum) {
            return false;
        }

        *stat_type = type;
        *current_value = current;
        *max_value = maximum;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool TryWriteSpiritCurrent(const uintptr_t entry, const int64_t value) {
    if (entry < kMinimumPointerAddress || value < 0) {
        return false;
    }

    __try {
        *reinterpret_cast<int64_t*>(entry + 0x08) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool TryReadStaminaEntry(const uintptr_t entry,
                         int32_t* const stat_type,
                         int64_t* const current_value,
                         int64_t* const max_value) {
    if (entry < kMinimumPointerAddress ||
        stat_type == nullptr ||
        current_value == nullptr ||
        max_value == nullptr) {
        return false;
    }

    __try {
        const int32_t type = *reinterpret_cast<const int32_t*>(entry);
        if (!IsStaminaStatId(type)) {
            return false;
        }

        const int64_t current = *reinterpret_cast<const int64_t*>(entry + 0x08);
        const int64_t maximum = *reinterpret_cast<const int64_t*>(entry + 0x18);
        if (maximum <= 0 || maximum > kPlayerResourceMaxCeiling || current < 0 || current > maximum) {
            return false;
        }

        *stat_type = type;
        *current_value = current;
        *max_value = maximum;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool TryWriteStatCurrent(const uintptr_t entry, const int64_t value) {
    if (entry < kMinimumPointerAddress || value < 0) {
        return false;
    }

    __try {
        *reinterpret_cast<int64_t*>(entry + 0x08) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool TryResolvePostRematchAltResourceEntries(uintptr_t* const alt_stamina_entry,
                                             uintptr_t* const alt_spirit_entry) {
    if (alt_stamina_entry == nullptr || alt_spirit_entry == nullptr) {
        return false;
    }

    *alt_stamina_entry = 0;
    *alt_spirit_entry = 0;

    const uintptr_t health_entry = GetTrackedPlayerHealthEntry();
    if (health_entry < kMinimumPointerAddress) {
        return false;
    }

    __try {
        const int32_t health_type = *reinterpret_cast<const int32_t*>(health_entry);
        const int64_t health_max = *reinterpret_cast<const int64_t*>(health_entry + 0x18);
        if (health_type != kHealthId || health_max != kPostRematchHealthMax) {
            return false;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }

    const uintptr_t stamina_candidate =
        static_cast<uintptr_t>(static_cast<intptr_t>(health_entry) + kPostRematchAltStaminaOffsetFromHealth);
    int32_t stamina_type = 0;
    int64_t stamina_current = 0;
    int64_t stamina_max = 0;
    if (TryReadStaminaEntry(stamina_candidate, &stamina_type, &stamina_current, &stamina_max) &&
        stamina_max == kPostRematchResourceMax) {
        *alt_stamina_entry = stamina_candidate;
    }

    const uintptr_t spirit_candidate = health_entry + kPostRematchAltSpiritOffsetFromHealth;
    int32_t spirit_type = 0;
    int64_t spirit_current = 0;
    int64_t spirit_max = 0;
    if (TryReadSpiritEntry(spirit_candidate, &spirit_type, &spirit_current, &spirit_max) &&
        spirit_max == kPostRematchResourceMax) {
        *alt_spirit_entry = spirit_candidate;
    }

    return *alt_stamina_entry >= kMinimumPointerAddress || *alt_spirit_entry >= kMinimumPointerAddress;
}

int64_t AdjustObservedDelta(const int64_t previous_value,
                            const int64_t observed_value,
                            const int64_t max_value,
                            const StatConfig& stat_config) {
    const int64_t delta = observed_value - previous_value;
    int64_t adjusted_delta = delta;
    if (delta < 0) {
        adjusted_delta = -ScaleDelta(-delta, stat_config.consumption_multiplier);
    } else if (ShouldBoostObservedSpiritRecovery(delta, max_value, 0, 0, GetTickCount())) {
        adjusted_delta = ScaleObservedRecoveryDelta(delta, stat_config.heal_multiplier);
    } else {
        return observed_value;
    }

    return ClampToRange(previous_value + adjusted_delta, 0, max_value);
}

bool ApplyObservedPostRematchAltSpiritChange(const ModConfig& config,
                                             const uintptr_t entry,
                                             const int32_t stat_type,
                                             const int64_t previous_value,
                                             const int64_t observed_value,
                                             const int64_t max_value,
                                             DWORD* const last_consumption_tick,
                                             int64_t* const final_value) {
    if (final_value == nullptr ||
        !ShouldInstallSpiritHook(config) ||
        previous_value < 0 ||
        previous_value > max_value ||
        observed_value < 0 ||
        observed_value > max_value ||
        previous_value == observed_value) {
        return false;
    }

    const int64_t delta = observed_value - previous_value;
    int64_t adjusted_value = observed_value;
    const DWORD now = GetTickCount();

    if (delta < 0) {
        if (IsFocusRecoveryWindowActive()) {
            *final_value = previous_value;
            const auto log_index = g_spirit_watcher_logs.fetch_add(1, std::memory_order_acq_rel);
            if (log_index < 128) {
                Log("runtime: post-rematch alt spirit blocked focus-mode consumption entry=0x%p type=%d old=%lld observed=%lld max=%lld",
                    reinterpret_cast<void*>(entry),
                    stat_type,
                    static_cast<long long>(previous_value),
                    static_cast<long long>(observed_value),
                    static_cast<long long>(max_value));
            }
            return TryWriteSpiritCurrent(entry, previous_value);
        }

        const int64_t consumed = previous_value - observed_value;
        adjusted_value =
            ClampToRange(previous_value - ScaleDelta(consumed, config.spirit.consumption_multiplier), 0, max_value);
        if (last_consumption_tick != nullptr) {
            *last_consumption_tick = now;
        }
    } else if (delta > 0 && ShouldBoostObservedSpiritRecovery(delta, max_value, 0, 0, now)) {
        adjusted_value =
            ClampToRange(previous_value + ScaleObservedRecoveryDelta(delta, config.spirit.heal_multiplier), 0, max_value);
    } else {
        return false;
    }

    *final_value = adjusted_value;
    if (adjusted_value != observed_value && !TryWriteSpiritCurrent(entry, adjusted_value)) {
        *final_value = observed_value;
        return false;
    }

    const auto log_index = g_spirit_watcher_logs.fetch_add(1, std::memory_order_acq_rel);
    if (log_index < 128) {
        Log("runtime: post-rematch alt spirit scaled observed change entry=0x%p type=%d old=%lld observed=%lld final=%lld max=%lld delta=%lld consumption=%.3f heal=%.3f",
            reinterpret_cast<void*>(entry),
            stat_type,
            static_cast<long long>(previous_value),
            static_cast<long long>(observed_value),
            static_cast<long long>(*final_value),
            static_cast<long long>(max_value),
            static_cast<long long>(delta),
            config.spirit.consumption_multiplier,
            config.spirit.heal_multiplier);
    }
    return adjusted_value != observed_value;
}

bool ApplyObservedSpiritChange(const ModConfig& config,
                               const uintptr_t entry,
                               const int32_t stat_type,
                               const int64_t previous_value,
                               const int64_t observed_value,
                               const int64_t max_value,
                               DWORD* const last_consumption_tick,
                               int64_t* const final_value) {
    if (final_value == nullptr ||
        !ShouldInstallSpiritHook(config) ||
        previous_value < 0 ||
        previous_value > max_value ||
        observed_value < 0 ||
        observed_value > max_value ||
        previous_value == observed_value) {
        return false;
    }

    const int64_t delta = observed_value - previous_value;
    const DWORD now = GetTickCount();
    const DWORD configured_lockout_ms = config.spirit.recovery_lockout_ms;
    DWORD lockout_ms = configured_lockout_ms > kSpiritRecoveryLockoutMaxMs
        ? kSpiritRecoveryLockoutMaxMs
        : configured_lockout_ms;

    if (delta > 0 &&
        previous_value == 0 &&
        last_consumption_tick != nullptr &&
        *last_consumption_tick == 0) {
        *final_value = observed_value;
        const auto current = g_spirit_watcher_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 64) {
            Log("runtime: spirit watcher accepted bootstrap fill entry=0x%p type=%d old=%lld observed=%lld max=%lld heal=%.3f",
                reinterpret_cast<void*>(entry),
                stat_type,
                static_cast<long long>(previous_value),
                static_cast<long long>(observed_value),
                static_cast<long long>(max_value),
                config.spirit.heal_multiplier);
        }
        return false;
    }
    if (delta < 0) {
        if (IsFocusRecoveryWindowActive()) {
            *final_value = previous_value;
            const auto current = g_spirit_watcher_logs.fetch_add(1, std::memory_order_acq_rel);
            if (current < 64) {
                Log("runtime: spirit watcher blocked focus-mode consumption entry=0x%p type=%d old=%lld observed=%lld max=%lld",
                    reinterpret_cast<void*>(entry),
                    stat_type,
                    static_cast<long long>(previous_value),
                    static_cast<long long>(observed_value),
                    static_cast<long long>(max_value));
            }
            return TryWriteSpiritCurrent(entry, previous_value);
        }

        if (!WasRecentPlayerSpiritConsumption(entry, kObservedConsumptionAcceptWindowMs) &&
            !ShouldAcceptObservedSpiritConsumptionWithoutRecentHook(entry,
                                                                    stat_type,
                                                                    previous_value,
                                                                    observed_value,
                                                                    max_value)) {
            *final_value = previous_value;
            const auto current = g_spirit_watcher_logs.fetch_add(1, std::memory_order_acq_rel);
            if (current < 96) {
                Log("runtime: spirit watcher rejected idle drain entry=0x%p type=%d old=%lld observed=%lld max=%lld accept_window_ms=%u",
                    reinterpret_cast<void*>(entry),
                    stat_type,
                    static_cast<long long>(previous_value),
                    static_cast<long long>(observed_value),
                    static_cast<long long>(max_value),
                    static_cast<unsigned>(kObservedConsumptionAcceptWindowMs));
            }
            return TryWriteSpiritCurrent(entry, previous_value);
        }

        const int64_t consumed = previous_value - observed_value;
        const int64_t adjusted_value =
            ClampToRange(previous_value - ScaleDelta(consumed, config.spirit.consumption_multiplier), 0, max_value);
        *final_value = adjusted_value;
        LogSpiritPostDrainCacheScan(entry, observed_value, max_value);
        if (last_consumption_tick != nullptr) {
            *last_consumption_tick = now;
        }

        if (adjusted_value != observed_value && !TryWriteSpiritCurrent(entry, adjusted_value)) {
            *final_value = observed_value;
            return false;
        }

        const auto current = g_spirit_watcher_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 64) {
            Log("runtime: spirit watcher scaled observed consumption entry=0x%p type=%d old=%lld observed=%lld final=%lld max=%lld consumption=%.3f primary_accept=%d",
                reinterpret_cast<void*>(entry),
                stat_type,
                static_cast<long long>(previous_value),
                static_cast<long long>(observed_value),
                static_cast<long long>(adjusted_value),
                static_cast<long long>(max_value),
                config.spirit.consumption_multiplier,
                ShouldAcceptObservedSpiritConsumptionWithoutRecentHook(entry,
                                                                        stat_type,
                                                                        previous_value,
                                                                        observed_value,
                                                                        max_value) ? 1 : 0);
        }
        return false;
    }

    if (delta > 0 &&
        last_consumption_tick != nullptr &&
        *last_consumption_tick != 0 &&
        lockout_ms > 0 &&
        now - *last_consumption_tick <= lockout_ms) {
        *final_value = previous_value;
        const auto current = g_spirit_watcher_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 64) {
            Log("runtime: spirit watcher blocked post-consumption recovery entry=0x%p type=%d old=%lld observed=%lld final=%lld max=%lld elapsed_ms=%lu lockout_ms=%lu configured_lockout_ms=%lu",
                reinterpret_cast<void*>(entry),
                stat_type,
                static_cast<long long>(previous_value),
                static_cast<long long>(observed_value),
                static_cast<long long>(previous_value),
                static_cast<long long>(max_value),
                static_cast<unsigned long>(now - *last_consumption_tick),
                static_cast<unsigned long>(lockout_ms),
                static_cast<unsigned long>(configured_lockout_ms));
        }
        return TryWriteSpiritCurrent(entry, previous_value);
    }

    const bool boost_observed_recovery = ShouldBoostObservedSpiritRecovery(
        delta,
        max_value,
        last_consumption_tick != nullptr ? *last_consumption_tick : 0,
        lockout_ms,
        now);
    int64_t adjusted_value = boost_observed_recovery
        ? AdjustObservedDelta(previous_value, observed_value, max_value, config.spirit)
        : observed_value;
    *final_value = adjusted_value;

    if (adjusted_value == observed_value) {
        if (delta < 0 &&
            adjusted_value < previous_value &&
            last_consumption_tick != nullptr) {
            *last_consumption_tick = now;
            const auto current = g_spirit_watcher_logs.fetch_add(1, std::memory_order_acq_rel);
            if (current < 64) {
                Log("runtime: spirit watcher tracked consumed no-op entry=0x%p type=%d old=%lld observed=%lld final=%lld max=%lld lockout_ms=%lu",
                    reinterpret_cast<void*>(entry),
                    stat_type,
                    static_cast<long long>(previous_value),
                    static_cast<long long>(observed_value),
                    static_cast<long long>(adjusted_value),
                    static_cast<long long>(max_value),
                    static_cast<unsigned long>(lockout_ms));
            }
        }
        const auto current = g_spirit_watcher_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 32) {
            Log("runtime: spirit watcher observed no-op entry=0x%p type=%d old=%lld observed=%lld max=%lld consumption=%.3f heal=%.3f",
                reinterpret_cast<void*>(entry),
                stat_type,
                static_cast<long long>(previous_value),
                static_cast<long long>(observed_value),
                static_cast<long long>(max_value),
                config.spirit.consumption_multiplier,
                config.spirit.heal_multiplier);
        }
        return false;
    }

    if (!TryWriteSpiritCurrent(entry, adjusted_value)) {
        return false;
    }

    if (delta < 0 &&
        adjusted_value < previous_value &&
        last_consumption_tick != nullptr) {
        *last_consumption_tick = now;
    }

    return true;
}

bool FillSpiritOnInitialTrack(const ModConfig& config,
                              const uintptr_t entry,
                              const int32_t stat_type,
                              const int64_t current_value,
                              const int64_t max_value,
                              int64_t* const final_value) {
    if (final_value == nullptr ||
        !ShouldInstallSpiritHook(config) ||
        entry < kMinimumPointerAddress ||
        max_value <= 0 ||
        max_value > kPlayerResourceMaxCeiling ||
        current_value < 0 ||
        current_value >= max_value) {
        return false;
    }

    if (!TryWriteSpiritCurrent(entry, max_value)) {
        return false;
    }

    *final_value = max_value;
    const auto log_index = g_focus_recovery_tick_logs.fetch_add(1, std::memory_order_acq_rel);
    if (log_index < 96) {
        Log("runtime: spirit watcher load-fill entry=0x%p type=%d old=%lld final=%lld max=%lld",
            reinterpret_cast<void*>(entry),
            stat_type,
            static_cast<long long>(current_value),
            static_cast<long long>(max_value),
            static_cast<long long>(max_value));
    }
    return true;
}

bool ApplyFocusSpiritRecoveryTick(const ModConfig& config,
                                  const uintptr_t entry,
                                  const int32_t stat_type,
                                  const int64_t current_value,
                                  const int64_t max_value,
                                  int64_t* const final_value) {
    if (final_value == nullptr ||
        !ShouldInstallSpiritHook(config) ||
        !IsFocusRecoveryWindowActive() ||
        entry < kMinimumPointerAddress ||
        max_value <= 0 ||
        max_value > kPlayerResourceMaxCeiling ||
        current_value < 0 ||
        current_value >= max_value) {
        return false;
    }

    constexpr int64_t kFocusBaseRecoveryTick = 400;
    const int64_t adjusted_delta = std::max<int64_t>(
        1,
        ScaleObservedRecoveryDelta(kFocusBaseRecoveryTick, config.spirit.heal_multiplier));
    const int64_t adjusted_value = ClampToRange(current_value + adjusted_delta, 0, max_value);
    if (adjusted_value <= current_value || !TryWriteSpiritCurrent(entry, adjusted_value)) {
        return false;
    }

    *final_value = adjusted_value;
    const auto log_index = g_spirit_watcher_logs.fetch_add(1, std::memory_order_acq_rel);
    if (log_index < 128) {
        Log("runtime: spirit watcher focus-recovery tick entry=0x%p type=%d old=%lld final=%lld max=%lld base=%lld heal=%.3f",
            reinterpret_cast<void*>(entry),
            stat_type,
            static_cast<long long>(current_value),
            static_cast<long long>(adjusted_value),
            static_cast<long long>(max_value),
            static_cast<long long>(kFocusBaseRecoveryTick),
            config.spirit.heal_multiplier);
    }
    return true;
}

bool ApplyObservedStaminaChange(const ModConfig& config,
                                const uintptr_t entry,
                                const int32_t stat_type,
                                const int64_t previous_value,
                                const int64_t observed_value,
                                const int64_t max_value,
                                int64_t* const final_value) {
    if (final_value == nullptr ||
        !config.general.enabled ||
        !config.stamina.enabled ||
        previous_value < 0 ||
        previous_value > max_value ||
        observed_value < 0 ||
        observed_value > max_value ||
        previous_value == observed_value) {
        return false;
    }

    const int64_t delta = observed_value - previous_value;
    int64_t adjusted_value = observed_value;
    if (delta < 0) {
        adjusted_value =
            ClampToRange(previous_value - ScaleDelta(-delta, config.stamina.consumption_multiplier), 0, max_value);
    } else if (delta > 0 && ShouldBoostObservedStaminaRecovery(delta, max_value)) {
        adjusted_value =
            ClampToRange(previous_value + ScaleObservedRecoveryDelta(delta, config.stamina.heal_multiplier), 0, max_value);
    } else {
        return false;
    }

    *final_value = adjusted_value;
    if (adjusted_value != observed_value && !TryWriteStatCurrent(entry, adjusted_value)) {
        *final_value = observed_value;
        return false;
    }

    const auto current = g_stamina_watcher_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 48) {
        Log("runtime: stamina watcher scaled observed change entry=0x%p type=%d old=%lld observed=%lld final=%lld max=%lld delta=%lld consumption=%.3f heal=%.3f",
            reinterpret_cast<void*>(entry),
            stat_type,
            static_cast<long long>(previous_value),
            static_cast<long long>(observed_value),
            static_cast<long long>(*final_value),
            static_cast<long long>(max_value),
            static_cast<long long>(delta),
            config.stamina.consumption_multiplier,
            config.stamina.heal_multiplier);
    }
    return adjusted_value != observed_value;
}

DWORD WINAPI SpiritWatcherThreadProc(LPVOID) {
    uintptr_t last_entry = 0;
    int64_t last_value = 0;
    DWORD last_spirit_consumption_tick = 0;
    uintptr_t last_stamina_entry = 0;
    int64_t last_stamina_value = 0;
    uintptr_t last_post_rematch_alt_stamina_entry = 0;
    int64_t last_post_rematch_alt_stamina_value = 0;
    uintptr_t last_post_rematch_alt_spirit_entry = 0;
    int64_t last_post_rematch_alt_spirit_value = 0;
    DWORD last_post_rematch_alt_spirit_consumption_tick = 0;
    DWORD last_snapshot_tick = GetTickCount();
    DWORD last_ui_candidate_sample_tick = 0;

    Log("runtime: spirit watcher started");
    while (g_spirit_watcher_running.load(std::memory_order_acquire)) {
        const ModConfig config = GetConfig();
        const uintptr_t entry = GetTrackedPlayerSpiritEntry();
        const uintptr_t stamina_entry = GetTrackedPlayerStaminaEntry();

        int32_t stamina_type = 0;
        int64_t stamina_value = 0;
        int64_t stamina_max = 0;
        bool stale_player_stamina = false;
        if (g_runtime_enabled.load(std::memory_order_acquire) &&
            config.general.enabled &&
            stamina_entry >= kMinimumPointerAddress &&
            ClassifyTrackedStatEntry(stamina_entry) == TrackedStatEntryKind::PlayerStamina) {
            stale_player_stamina = !TryReadStaminaEntry(stamina_entry, &stamina_type, &stamina_value, &stamina_max);
        }

        if (stale_player_stamina) {
            g_state_mutex.lock();
            ResetTrackedEntriesLocked();
            g_state_mutex.unlock();

            last_stamina_entry = 0;
            last_stamina_value = 0;

            const auto log_index = g_stamina_watcher_logs.fetch_add(1, std::memory_order_acq_rel);
            if (log_index < 48) {
                Log("runtime: stamina watcher reset stale player stat entry=0x%p",
                    reinterpret_cast<void*>(stamina_entry));
            }
        } else if (g_runtime_enabled.load(std::memory_order_acquire) &&
            config.general.enabled &&
            stamina_entry >= kMinimumPointerAddress &&
            ClassifyTrackedStatEntry(stamina_entry) == TrackedStatEntryKind::PlayerStamina) {
            if (stamina_entry != last_stamina_entry) {
                last_stamina_entry = stamina_entry;
                last_stamina_value = stamina_value;

                const auto log_index = g_stamina_watcher_logs.fetch_add(1, std::memory_order_acq_rel);
                if (log_index < 32) {
                    Log("runtime: stamina watcher tracking entry=0x%p type=%d current=%lld max=%lld",
                        reinterpret_cast<void*>(stamina_entry),
                        stamina_type,
                        static_cast<long long>(stamina_value),
                        static_cast<long long>(stamina_max));
                }
            } else {
                int64_t final_stamina_value = stamina_value;
                if (ApplyObservedStaminaChange(config,
                                               stamina_entry,
                                               stamina_type,
                                               last_stamina_value,
                                               stamina_value,
                                               stamina_max,
                                               &final_stamina_value)) {
                    const auto log_index = g_stamina_watcher_logs.fetch_add(1, std::memory_order_acq_rel);
                    if (log_index < 48) {
                        Log("runtime: stamina watcher adjusted entry=0x%p type=%d old=%lld observed=%lld final=%lld max=%lld consumption=%.3f heal=%.3f",
                            reinterpret_cast<void*>(stamina_entry),
                            stamina_type,
                            static_cast<long long>(last_stamina_value),
                            static_cast<long long>(stamina_value),
                            static_cast<long long>(final_stamina_value),
                            static_cast<long long>(stamina_max),
                            config.stamina.consumption_multiplier,
                            config.stamina.heal_multiplier);
                    }
                }
                last_stamina_value = final_stamina_value;
            }
        } else {
            last_stamina_entry = 0;
            last_stamina_value = 0;
        }

        uintptr_t post_rematch_alt_stamina_entry = 0;
        uintptr_t post_rematch_alt_spirit_entry = 0;
        TryResolvePostRematchAltResourceEntries(&post_rematch_alt_stamina_entry, &post_rematch_alt_spirit_entry);

        if (g_runtime_enabled.load(std::memory_order_acquire) &&
            config.general.enabled &&
            post_rematch_alt_stamina_entry >= kMinimumPointerAddress &&
            post_rematch_alt_stamina_entry != stamina_entry) {
            int32_t alt_stamina_type = 0;
            int64_t alt_stamina_value = 0;
            int64_t alt_stamina_max = 0;
            if (TryReadStaminaEntry(post_rematch_alt_stamina_entry,
                                    &alt_stamina_type,
                                    &alt_stamina_value,
                                    &alt_stamina_max)) {
                if (post_rematch_alt_stamina_entry != last_post_rematch_alt_stamina_entry) {
                    last_post_rematch_alt_stamina_entry = post_rematch_alt_stamina_entry;
                    last_post_rematch_alt_stamina_value = alt_stamina_value;

                    const auto log_index = g_stamina_watcher_logs.fetch_add(1, std::memory_order_acq_rel);
                    if (log_index < 64) {
                        Log("runtime: post-rematch alt stamina watcher tracking entry=0x%p offset=-0x260 type=%d current=%lld max=%lld",
                            reinterpret_cast<void*>(post_rematch_alt_stamina_entry),
                            alt_stamina_type,
                            static_cast<long long>(alt_stamina_value),
                            static_cast<long long>(alt_stamina_max));
                    }
                } else {
                    int64_t final_alt_stamina_value = alt_stamina_value;
                    if (ApplyObservedStaminaChange(config,
                                                   post_rematch_alt_stamina_entry,
                                                   alt_stamina_type,
                                                   last_post_rematch_alt_stamina_value,
                                                   alt_stamina_value,
                                                   alt_stamina_max,
                                                   &final_alt_stamina_value)) {
                        const auto log_index = g_stamina_watcher_logs.fetch_add(1, std::memory_order_acq_rel);
                        if (log_index < 96) {
                            Log("runtime: post-rematch alt stamina watcher adjusted entry=0x%p type=%d old=%lld observed=%lld final=%lld max=%lld consumption=%.3f heal=%.3f",
                                reinterpret_cast<void*>(post_rematch_alt_stamina_entry),
                                alt_stamina_type,
                                static_cast<long long>(last_post_rematch_alt_stamina_value),
                                static_cast<long long>(alt_stamina_value),
                                static_cast<long long>(final_alt_stamina_value),
                                static_cast<long long>(alt_stamina_max),
                                config.stamina.consumption_multiplier,
                                config.stamina.heal_multiplier);
                        }
                    }
                    last_post_rematch_alt_stamina_value = final_alt_stamina_value;
                }
            }
        } else {
            last_post_rematch_alt_stamina_entry = 0;
            last_post_rematch_alt_stamina_value = 0;
        }

        if (g_runtime_enabled.load(std::memory_order_acquire) &&
            config.general.enabled &&
            post_rematch_alt_spirit_entry >= kMinimumPointerAddress &&
            post_rematch_alt_spirit_entry != entry) {
            int32_t alt_spirit_type = 0;
            int64_t alt_spirit_value = 0;
            int64_t alt_spirit_max = 0;
            if (TryReadSpiritEntry(post_rematch_alt_spirit_entry,
                                   &alt_spirit_type,
                                   &alt_spirit_value,
                                   &alt_spirit_max)) {
                if (post_rematch_alt_spirit_entry != last_post_rematch_alt_spirit_entry) {
                    last_post_rematch_alt_spirit_entry = post_rematch_alt_spirit_entry;
                    last_post_rematch_alt_spirit_value = alt_spirit_value;
                    last_post_rematch_alt_spirit_consumption_tick = 0;

                    const auto log_index = g_spirit_watcher_logs.fetch_add(1, std::memory_order_acq_rel);
                    if (log_index < 64) {
                        Log("runtime: post-rematch alt spirit watcher tracking entry=0x%p offset=+0x9B0 type=%d current=%lld max=%lld",
                            reinterpret_cast<void*>(post_rematch_alt_spirit_entry),
                            alt_spirit_type,
                            static_cast<long long>(alt_spirit_value),
                            static_cast<long long>(alt_spirit_max));
                    }
                } else {
                    int64_t final_alt_spirit_value = alt_spirit_value;
                    ApplyObservedPostRematchAltSpiritChange(config,
                                                            post_rematch_alt_spirit_entry,
                                                            alt_spirit_type,
                                                            last_post_rematch_alt_spirit_value,
                                                            alt_spirit_value,
                                                            alt_spirit_max,
                                                            &last_post_rematch_alt_spirit_consumption_tick,
                                                            &final_alt_spirit_value);
                    if (ApplyFocusSpiritRecoveryTick(config,
                                                     post_rematch_alt_spirit_entry,
                                                     alt_spirit_type,
                                                     final_alt_spirit_value,
                                                     alt_spirit_max,
                                                     &final_alt_spirit_value)) {
                        last_post_rematch_alt_spirit_consumption_tick = 0;
                    }
                    last_post_rematch_alt_spirit_value = final_alt_spirit_value;
                }
            }
        } else {
            last_post_rematch_alt_spirit_entry = 0;
            last_post_rematch_alt_spirit_value = 0;
            last_post_rematch_alt_spirit_consumption_tick = 0;
        }

        int32_t stat_type = 0;
        int64_t current_value = 0;
        int64_t max_value = 0;
        if (!g_runtime_enabled.load(std::memory_order_acquire) ||
            !config.general.enabled ||
            entry < kMinimumPointerAddress ||
            ClassifyTrackedStatEntry(entry) != TrackedStatEntryKind::PlayerSpirit ||
            !TryReadSpiritEntry(entry, &stat_type, &current_value, &max_value)) {
            last_entry = 0;
            last_value = 0;
            Sleep(kSpiritWatcherPollMs);
            continue;
        }

        if (entry != last_entry) {
            last_entry = entry;
            last_value = current_value;
            last_spirit_consumption_tick = 0;

            int64_t tracked_value = current_value;
            if (FillSpiritOnInitialTrack(config, entry, stat_type, current_value, max_value, &tracked_value)) {
                SyncPlayerSpiritVisualMirror(entry, tracked_value, max_value, "spirit-watcher-load-fill");
            }
            last_value = tracked_value;

            const auto log_index = g_spirit_watcher_logs.fetch_add(1, std::memory_order_acq_rel);
            if (log_index < 32) {
                Log("runtime: spirit watcher tracking entry=0x%p type=%d current=%lld max=%lld",
                    reinterpret_cast<void*>(entry),
                    stat_type,
                    static_cast<long long>(tracked_value),
                    static_cast<long long>(max_value));
            }

            Sleep(kSpiritWatcherPollMs);
            continue;
        }

        int64_t final_value = current_value;
        if (ApplyObservedSpiritChange(config,
                                      entry,
                                      stat_type,
                                      last_value,
                                      current_value,
                                      max_value,
                                      &last_spirit_consumption_tick,
                                      &final_value)) {
            if (final_value > last_value && config.spirit.recovery_lockout_ms > 0) {
                last_spirit_consumption_tick = 0;
            }
            const auto log_index = g_spirit_watcher_logs.fetch_add(1, std::memory_order_acq_rel);
            if (log_index < 48) {
                Log("runtime: spirit watcher adjusted entry=0x%p type=%d old=%lld observed=%lld final=%lld max=%lld consumption=%.3f heal=%.3f lockout_ms=%lu",
                    reinterpret_cast<void*>(entry),
                    stat_type,
                    static_cast<long long>(last_value),
                    static_cast<long long>(current_value),
                    static_cast<long long>(final_value),
                    static_cast<long long>(max_value),
                    config.spirit.consumption_multiplier,
                    config.spirit.heal_multiplier,
                    static_cast<unsigned long>(config.spirit.recovery_lockout_ms));
            }
        }

        if (ApplyFocusSpiritRecoveryTick(config,
                                         entry,
                                         stat_type,
                                         final_value,
                                         max_value,
                                         &final_value)) {
            last_spirit_consumption_tick = 0;
        }

        SyncPlayerSpiritVisualMirror(entry, final_value, max_value, "spirit-watcher");
        last_value = final_value;
        TraceNearbyPlayerResourceChanges(GetTrackedPlayerHealthEntry(), "watcher");
        LogSpiritUiCandidateSamples(entry, final_value, max_value, &last_ui_candidate_sample_tick);
        const DWORD now = GetTickCount();
        if (now - last_snapshot_tick >= kSpiritWatcherSnapshotMs) {
            last_snapshot_tick = now;
            const auto snapshot_index = g_spirit_watcher_snapshot_logs.fetch_add(1, std::memory_order_acq_rel);
            if (snapshot_index < 16) {
                Log("runtime: spirit watcher snapshot entry=0x%p type=%d current=%lld max=%lld consumption=%.3f heal=%.3f",
                    reinterpret_cast<void*>(entry),
                    stat_type,
                    static_cast<long long>(current_value),
                    static_cast<long long>(max_value),
                    config.spirit.consumption_multiplier,
                    config.spirit.heal_multiplier);
            }
            const auto stamina_snapshot_index = g_stamina_watcher_snapshot_logs.fetch_add(1, std::memory_order_acq_rel);
            if (stamina_snapshot_index < 16 && last_stamina_entry >= kMinimumPointerAddress) {
                Log("runtime: stamina watcher snapshot entry=0x%p current=%lld consumption=%.3f heal=%.3f",
                    reinterpret_cast<void*>(last_stamina_entry),
                    static_cast<long long>(last_stamina_value),
                    config.stamina.consumption_multiplier,
                    config.stamina.heal_multiplier);
            }
        }
        Sleep(kSpiritWatcherPollMs);
    }

    Log("runtime: spirit watcher stopped");
    return 0;
}

}  // namespace

bool StartSpiritWatcher() {
    bool expected = false;
    if (!g_spirit_watcher_running.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return true;
    }

    g_spirit_watcher_thread = CreateThread(nullptr, 0, SpiritWatcherThreadProc, nullptr, 0, nullptr);
    if (g_spirit_watcher_thread == nullptr) {
        g_spirit_watcher_running.store(false, std::memory_order_release);
        Log("runtime: spirit watcher failed to start error=%lu", static_cast<unsigned long>(GetLastError()));
        return false;
    }

    return true;
}

void StopSpiritWatcher() {
    if (!g_spirit_watcher_running.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    HANDLE thread = g_spirit_watcher_thread;
    g_spirit_watcher_thread = nullptr;
    if (thread != nullptr) {
        WaitForSingleObject(thread, 2000);
        CloseHandle(thread);
    }
}
