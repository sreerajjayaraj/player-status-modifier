#include "config_watcher.h"

#include "config.h"
#include "hooks.h"
#include "logger.h"
#include "position_control.h"

#include <Windows.h>

#include <atomic>

namespace {

constexpr DWORD kConfigWatchPollMs = 1000;
constexpr DWORD kConfigReloadSettleMs = 200;

std::atomic<bool> g_config_watcher_running{false};
HANDLE g_config_watcher_thread = nullptr;

bool DidHookRequirementsChange(const ModConfig& previous, const ModConfig& next) {
    return ShouldInstallSharedStatHooks(previous) != ShouldInstallSharedStatHooks(next) ||
           ShouldInstallDamageHook(previous) != ShouldInstallDamageHook(next) ||
           ShouldInstallItemGainHook(previous) != ShouldInstallItemGainHook(next) ||
           ShouldInstallAffinityHook(previous) != ShouldInstallAffinityHook(next) ||
           ShouldInstallDurabilityHooks(previous) != ShouldInstallDurabilityHooks(next) ||
           ShouldInstallDragonVillageSummonHook(previous) != ShouldInstallDragonVillageSummonHook(next) ||
           ShouldInstallDragonFlyingRestrictHook(previous) != ShouldInstallDragonFlyingRestrictHook(next) ||
           ShouldInstallDragonRoofRestrictHook(previous) != ShouldInstallDragonRoofRestrictHook(next) ||
           ShouldInstallPositionHeightHook(previous) != ShouldInstallPositionHeightHook(next);
}

bool TryGetLastWriteTimestamp(const std::wstring& path, ULONGLONG* const timestamp) {
    if (timestamp == nullptr) {
        return false;
    }

    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes)) {
        return false;
    }

    ULARGE_INTEGER last_write{};
    last_write.LowPart = attributes.ftLastWriteTime.dwLowDateTime;
    last_write.HighPart = attributes.ftLastWriteTime.dwHighDateTime;
    *timestamp = last_write.QuadPart;
    return true;
}

bool TryGetConfigSetTimestamp(const std::wstring& primary_path, ULONGLONG* const timestamp) {
    if (timestamp == nullptr) {
        return false;
    }

    ULONGLONG newest_timestamp = 0;
    const auto paths = GetConfigMergePaths(primary_path);
    for (const auto& path : paths) {
        ULONGLONG candidate_timestamp = 0;
        if (TryGetLastWriteTimestamp(path, &candidate_timestamp) &&
            candidate_timestamp > newest_timestamp) {
            newest_timestamp = candidate_timestamp;
        }
    }

    *timestamp = newest_timestamp ^ (static_cast<ULONGLONG>(paths.size()) << 1);
    return true;
}

void ApplyLoggerReload(const ModConfig& previous, const ModConfig& current) {
    if (previous.general.log_enabled && !current.general.log_enabled) {
        Log("config-watcher: config reloaded, disabling logger");
        UpdateLoggerConfig(false, current.general.verbose, current.general.max_log_lines);
        return;
    }

    UpdateLoggerConfig(current.general.log_enabled, current.general.verbose, current.general.max_log_lines);
    if (current.general.log_enabled) {
        Log("config-watcher: config reloaded (verbose=%d max-log-lines=%lu stamina=%.3f/%.3f spirit=%.3f/%.3f spirit_lockout_ms=%lu stamina_to_spirit=%d suppress_stamina=%d min_cost=%lld scale=%.3f mount_lock=%d lock_value=%lld special_mount=%d special_health=%d special_stamina=%d special_health_candidates=%d special_lock_value=%lld)",
            current.general.verbose ? 1 : 0,
            static_cast<unsigned long>(current.general.max_log_lines),
            current.stamina.consumption_multiplier,
            current.stamina.heal_multiplier,
            current.spirit.consumption_multiplier,
            current.spirit.heal_multiplier,
            static_cast<unsigned long>(current.spirit.recovery_lockout_ms),
            current.advanced.redirect_large_stamina_costs_to_spirit ? 1 : 0,
            current.advanced.suppress_redirected_stamina_cost ? 1 : 0,
            static_cast<long long>(current.advanced.spirit_stamina_redirect_min_cost),
            current.advanced.spirit_stamina_redirect_scale,
            current.mount.lock_stamina ? 1 : 0,
            static_cast<long long>(current.mount.lock_value),
            current.mount.special_enabled ? 1 : 0,
            current.mount.lock_special_health ? 1 : 0,
            current.mount.lock_special_stamina ? 1 : 0,
            current.mount.lock_special_health_candidates ? 1 : 0,
            static_cast<long long>(current.mount.special_lock_value));
    }
}

void ConfigWatcherLoop() {
    const std::wstring config_path = GetLoadedConfigPath();
    ULONGLONG last_write_timestamp = 0;
    TryGetConfigSetTimestamp(config_path, &last_write_timestamp);

    while (g_config_watcher_running.load(std::memory_order_acquire)) {
        Sleep(kConfigWatchPollMs);
        if (!g_config_watcher_running.load(std::memory_order_acquire)) {
            break;
        }

        ULONGLONG observed_timestamp = 0;
        if (!TryGetConfigSetTimestamp(config_path, &observed_timestamp) ||
            observed_timestamp == last_write_timestamp) {
            continue;
        }

        Sleep(kConfigReloadSettleMs);

        ULONGLONG settled_timestamp = 0;
        if (!TryGetConfigSetTimestamp(config_path, &settled_timestamp)) {
            continue;
        }

        last_write_timestamp = settled_timestamp;

        const ModConfig previous = GetConfig();
        ModConfig next{};
        if (!ReadConfigSnapshot(config_path, &next)) {
            if (previous.general.log_enabled) {
                Log("config-watcher: failed to read updated config");
            }
            continue;
        }

        bool disabled_position_control = false;
        if ((next.position_control.enabled || next.position_control.horizontal_enabled) && !IsPositionHeightHookInstalled()) {
            next.position_control.enabled = false;
            next.position_control.horizontal_enabled = false;
            disabled_position_control = true;
        }

        if (next == previous) {
            if (disabled_position_control && previous.general.log_enabled) {
                Log("config-watcher: position control requested but position hook is unavailable");
            }
            continue;
        }

        if (!ApplyPositionControlConfig(previous.position_control, next.position_control)) {
            if (previous.general.log_enabled) {
                Log("config-watcher: failed to apply position control changes");
            }
            continue;
        }

            SetConfigSnapshot(config_path, next);
                ApplyLoggerReload(previous, next);

            if (DidHookRequirementsChange(previous, next) && next.general.log_enabled) {
                Log("config-watcher: hook loadout changed; restart game to apply hook enable/disable changes");
            }

            if (disabled_position_control && next.general.log_enabled) {
                Log("config-watcher: position control requested but position hook is unavailable");
            }
    }
}

DWORD WINAPI ConfigWatcherThreadProc(LPVOID) {
    ConfigWatcherLoop();
    return 0;
}

}  // namespace

bool StartConfigWatcher() {
    if (g_config_watcher_running.exchange(true, std::memory_order_acq_rel)) {
        return true;
    }

    const std::wstring config_path = GetLoadedConfigPath();
    if (config_path.empty()) {
        g_config_watcher_running.store(false, std::memory_order_release);
        return false;
    }

    g_config_watcher_thread = CreateThread(nullptr, 0, ConfigWatcherThreadProc, nullptr, 0, nullptr);
    if (g_config_watcher_thread == nullptr) {
        g_config_watcher_running.store(false, std::memory_order_release);
        return false;
    }
    return true;
}

void StopConfigWatcher() {
    if (!g_config_watcher_running.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    if (g_config_watcher_thread != nullptr) {
        WaitForSingleObject(g_config_watcher_thread, INFINITE);
        CloseHandle(g_config_watcher_thread);
        g_config_watcher_thread = nullptr;
    }
}
