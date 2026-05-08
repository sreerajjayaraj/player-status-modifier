#include "config.h"
#include "config_watcher.h"
#include "hooks.h"
#include "logger.h"
#include "mod_logic.h"
#include "position_control.h"
#include "runtime/runtime_state.h"

#include <Windows.h>

#include <algorithm>
#include <cwchar>
#include <filesystem>
#include <string>

namespace {

HMODULE g_module = nullptr;
constexpr wchar_t kTargetProcessName[] = L"CrimsonDesert.exe";
constexpr char kModSourceBuildId[] = "safe-rva-fixed-fix22-ground-mount-sanity-guard";

std::wstring GetSiblingPath(const wchar_t* file_name) {
    std::wstring module_path(MAX_PATH, L'\0');
    const DWORD length = GetModuleFileNameW(g_module, module_path.data(), static_cast<DWORD>(module_path.size()));
    module_path.resize(length);

    auto path = std::filesystem::path(module_path);
    path.replace_filename(file_name);
    return path.wstring();
}

std::wstring GetHostProcessPath() {
    std::wstring process_path(MAX_PATH, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, process_path.data(), static_cast<DWORD>(process_path.size()));
    process_path.resize(length);
    return process_path;
}

bool IsTargetHostProcess(const std::wstring& process_path) {
    if (process_path.empty()) {
        return false;
    }

    const auto file_name = std::filesystem::path(process_path).filename().wstring();
    return _wcsicmp(file_name.c_str(), kTargetProcessName) == 0;
}

void LogEffectiveConfig(const ModConfig& config) {
    Log("config: General Enabled=%d LogEnabled=%d Verbose=%d MaxLogLines=%lu InitDelayMs=%lu",
        config.general.enabled ? 1 : 0,
        config.general.log_enabled ? 1 : 0,
        config.general.verbose ? 1 : 0,
        static_cast<unsigned long>(config.general.max_log_lines),
        static_cast<unsigned long>(config.general.init_delay_ms));
    Log("config: Health ConsumptionMultiplier=%.3f HealMultiplier=%.3f",
        config.health.consumption_multiplier,
        config.health.heal_multiplier);
    Log("config: Stamina ConsumptionMultiplier=%.3f HealMultiplier=%.3f",
        config.stamina.consumption_multiplier,
        config.stamina.heal_multiplier);
    Log("config: Spirit ConsumptionMultiplier=%.3f HealMultiplier=%.3f",
        config.spirit.consumption_multiplier,
        config.spirit.heal_multiplier);
    Log("config: OutgoingDamage Enabled=%d Multiplier=%.3f IncomingDamage Enabled=%d Multiplier=%.3f",
        config.damage.outgoing.enabled ? 1 : 0,
        config.damage.outgoing.multiplier,
        config.damage.incoming.enabled ? 1 : 0,
        config.damage.incoming.multiplier);
    Log("config: Items GainMultiplier=%.3f Affinity Multiplier=%.3f Durability ConsumptionChance=%.3f",
        config.items.gain_multiplier,
        config.affinity.multiplier,
        config.durability.consumption_chance);
    Log("config: Resistance Enabled=%d Fire=%.3f Ice=%.3f Electricity=%.3f EffectiveIncomingMultiplier=%.3f",
        config.resistance.enabled ? 1 : 0,
        config.resistance.fire_resistance,
        config.resistance.ice_resistance,
        config.resistance.electricity_resistance,
        config.resistance.enabled
            ? (1.0 - std::max(std::max(config.resistance.fire_resistance, config.resistance.ice_resistance),
                              config.resistance.electricity_resistance))
            : 1.0);
    Log("config: Mount Enabled=%d LockHealth=%d LockStamina=%d LockValue=%lld",
        config.mount.enabled ? 1 : 0,
        config.mount.lock_health ? 1 : 0,
        config.mount.lock_stamina ? 1 : 0,
        static_cast<long long>(config.mount.lock_value));
    Log("config: DragonLimit village_summon=%d cancel_restrict_flying=%d roof_summon_experimental=%d",
        config.dragon_limit.village_summon ? 1 : 0,
        config.dragon_limit.cancel_restrict_flying ? 1 : 0,
        config.dragon_limit.roof_summon_experimental ? 1 : 0);
    Log("config: Position Height Enable=%d Key=%d Amplitude=%.3f Horizontal Enable=%d Key=%d Multiplier=%.3f",
        config.position_control.enabled ? 1 : 0,
        config.position_control.key,
        static_cast<double>(config.position_control.amplitude),
        config.position_control.horizontal_enabled ? 1 : 0,
        config.position_control.horizontal_key,
        static_cast<double>(config.position_control.horizontal_multiplier));
}

DWORD WINAPI InitializeMod(LPVOID) {
    const auto host_process_path = GetHostProcessPath();
    if (!IsTargetHostProcess(host_process_path)) {
        return 0;
    }

        const auto config_path = GetSiblingPath(L"player-status-modifier.ini");
        const auto log_path = GetSiblingPath(L"player-status-modifier.log");

        LoadConfig(config_path);
        const ModConfig initial_config = GetConfig();
        InitializeLogger(log_path,
                         initial_config.general.log_enabled,
                         initial_config.general.verbose,
                         initial_config.general.max_log_lines);

        Log("dllmain: initialization started");
        Log("dllmain: source build = %s compiled=%s %s stamina-id=%d legacy-stamina-id=%d stamina-offset=0x%llX legacy-offset=0x%llX spirit-offset=0x%llX",
            kModSourceBuildId,
            __DATE__,
            __TIME__,
            kStaminaId,
            kLegacyStaminaId,
            static_cast<unsigned long long>(kStaminaEntryOffsetFromHealth),
            static_cast<unsigned long long>(kLegacyStaminaEntryOffsetFromHealth),
            static_cast<unsigned long long>(kSpiritEntryOffsetFromHealth));
        Log("dllmain: host process = %ls", host_process_path.c_str());
        Log("dllmain: config path = %ls", config_path.c_str());
        LogEffectiveConfig(initial_config);

        const auto init_delay = initial_config.general.init_delay_ms;
    if (init_delay > 0) {
        Sleep(init_delay);
    }

    ResetRuntimeState();

        if (!InstallHooks()) {
            Log("dllmain: hook installation failed");
            return 0;
        }

        if (!StartMountResolver()) {
            Log("dllmain: mount resolver failed to start");
            RemoveHooks();
            return 0;
        }

    ModConfig config = GetConfig();
    if ((config.position_control.enabled || config.position_control.horizontal_enabled) && !IsPositionHeightHookInstalled()) {
        config.position_control.enabled = false;
        config.position_control.horizontal_enabled = false;
        SetConfigSnapshot(config_path, config);
        Log("dllmain: position control requested but position hook is unavailable; disabling position control");
    }

    if (!InitializePositionControl()) {
        Log("dllmain: position control initialization failed");
        RemoveHooks();
        return 0;
    }

    if (!StartConfigWatcher()) {
        Log("dllmain: config watcher failed to start");
    }

    Log("dllmain: initialization finished");
    return 0;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, const DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);

        HANDLE thread = CreateThread(nullptr, 0, InitializeMod, nullptr, 0, nullptr);
        if (thread != nullptr) {
            CloseHandle(thread);
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        StopMountResolver();
        StopConfigWatcher();
        ShutdownPositionControl();
        RemoveHooks();
        ShutdownLogger();
    }

    return TRUE;
}
