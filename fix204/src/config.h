#pragma once

#include <Windows.h>

#include <cstdint>
#include <string>
#include <vector>

struct StatConfig {
    bool enabled = false;
    double consumption_multiplier = 1.0;
    double heal_multiplier = 1.0;
    DWORD recovery_lockout_ms = 0;

    bool operator==(const StatConfig&) const = default;
};

struct DamageChannelConfig {
    bool enabled = false;
    bool stat_write_fallback = false;
    double multiplier = 1.0;

    bool operator==(const DamageChannelConfig&) const = default;
};

struct DamageConfig {
    DamageChannelConfig outgoing{false, false, 1.0};
    DamageChannelConfig incoming{false, false, 1.0};

    bool operator==(const DamageConfig&) const = default;
};

struct ItemConfig {
    double gain_multiplier = 1.0;

    bool operator==(const ItemConfig&) const = default;
};

struct AffinityConfig {
    double multiplier = 1.0;
    bool gift_diagnostics = false;
    bool pet_diagnostics = false;

    bool operator==(const AffinityConfig&) const = default;
};

struct DurabilityConfig {
    double consumption_chance = 100.0;

    bool operator==(const DurabilityConfig&) const = default;
};

struct PositionControlConfig {
    bool enabled = false;
    int key = VK_F6;
    float amplitude = 0.1f;
    bool horizontal_enabled = false;
    int horizontal_key = VK_F7;
    float horizontal_multiplier = 1.5f;

    bool operator==(const PositionControlConfig&) const = default;
};

struct MountConfig {
    bool enabled = false;
    bool lock_health = true;
    bool lock_stamina = true;
    bool lock_spirit_stamina = false;
    int64_t lock_value = 9999999;
    bool special_enabled = true;
    bool lock_special_health = true;
    bool lock_special_stamina = true;
    bool lock_special_spirit_stamina = false;
    bool lock_special_health_candidates = false;
    int64_t special_lock_value = 9999999;

    bool operator==(const MountConfig&) const = default;
};

struct DragonLimitConfig {
    bool roof_summon_experimental = false;
    bool village_summon = false;
    bool cancel_restrict_flying = false;

    bool operator==(const DragonLimitConfig&) const = default;
};

struct ResistanceConfig {
    bool enabled = false;
    double fire_resistance = 0.0;
    double ice_resistance = 0.0;
    double electricity_resistance = 0.0;

    bool operator==(const ResistanceConfig&) const = default;
};

struct GeneralConfig {
    bool enabled = true;
    bool log_enabled = true;
    bool verbose = false;
    DWORD max_log_lines = 2000;
    DWORD init_delay_ms = 3000;
    DWORD stale_component_ms = 60000;
    DWORD relock_idle_ms = 10000;

    bool operator==(const GeneralConfig&) const = default;
};

struct AdvancedConfig {
    bool swap_player_stamina_spirit = false;
    bool redirect_large_stamina_costs_to_spirit = false;
    bool suppress_redirected_stamina_cost = false;
    int64_t spirit_stamina_redirect_min_cost = 50000;
    double spirit_stamina_redirect_scale = 1.0;
    DWORD focus_recovery_window_ms = 12000;

    bool operator==(const AdvancedConfig&) const = default;
};

struct ModConfig {
    GeneralConfig general;
    DamageConfig damage;
    ItemConfig items;
    AffinityConfig affinity;
    DurabilityConfig durability;
    MountConfig mount;
    DragonLimitConfig dragon_limit;
    PositionControlConfig position_control;
    ResistanceConfig resistance;
    StatConfig health{false, 1.0, 1.0, 0};
    StatConfig stamina{false, 1.0, 1.0, 0};
    StatConfig spirit{false, 1.0, 1.0, 0};
    AdvancedConfig advanced;

    bool operator==(const ModConfig&) const = default;
};

inline bool IsStatConfigNeutral(const StatConfig& config) {
    return !config.enabled ||
           (config.consumption_multiplier == 1.0 &&
            config.heal_multiplier == 1.0 &&
            config.recovery_lockout_ms == 0);
}

inline bool IsAnyPlayerStatMultiplierEnabled(const ModConfig& config) {
    return !IsStatConfigNeutral(config.health) ||
           !IsStatConfigNeutral(config.stamina) ||
           !IsStatConfigNeutral(config.spirit);
}

inline bool IsMountLockEnabled(const ModConfig& config) {
    return config.mount.enabled &&
           (config.mount.lock_health ||
            config.mount.lock_stamina ||
            config.mount.lock_spirit_stamina ||
            (config.mount.special_enabled && config.mount.lock_special_health) ||
            (config.mount.special_enabled && config.mount.lock_special_stamina) ||
            (config.mount.special_enabled && config.mount.lock_special_spirit_stamina));
}

inline bool IsResistanceEnabled(const ModConfig& config) {
    return config.resistance.enabled &&
           (config.resistance.fire_resistance > 0.0 ||
            config.resistance.ice_resistance > 0.0 ||
            config.resistance.electricity_resistance > 0.0);
}

inline bool ShouldInstallStaminaHook(const ModConfig& config) {
    return config.general.enabled &&
           (!IsStatConfigNeutral(config.stamina) ||
            (config.mount.enabled &&
             (config.mount.lock_stamina ||
              config.mount.lock_spirit_stamina ||
              (config.mount.special_enabled && config.mount.lock_special_stamina) ||
              (config.mount.special_enabled && config.mount.lock_special_spirit_stamina))));
}

inline bool ShouldInstallPlayerStaminaHooks(const ModConfig& config) {
    static_cast<void>(config);
    return false;
}

inline bool ShouldInstallMountStaminaHook(const ModConfig& config) {
    return config.general.enabled &&
           config.mount.enabled &&
           (config.mount.lock_stamina ||
            config.mount.lock_spirit_stamina ||
            (config.mount.special_enabled && config.mount.lock_special_health) ||
            (config.mount.special_enabled && config.mount.lock_special_stamina) ||
            (config.mount.special_enabled && config.mount.lock_special_spirit_stamina));
}

inline bool ShouldInstallSpiritHook(const ModConfig& config) {
    return config.general.enabled && !IsStatConfigNeutral(config.spirit);
}

inline bool ShouldInstallLegacyStatWriteHook(const ModConfig& config) {
    return config.general.enabled &&
           (!IsStatConfigNeutral(config.stamina) ||
            !IsStatConfigNeutral(config.spirit) ||
            !IsStatConfigNeutral(config.health) ||
            (config.mount.enabled && config.mount.lock_health) ||
            (config.mount.enabled && config.mount.special_enabled && config.mount.lock_special_health) ||
            (config.damage.incoming.enabled &&
             config.damage.incoming.stat_write_fallback &&
             config.damage.incoming.multiplier != 1.0) ||
            (config.damage.outgoing.enabled &&
             config.damage.outgoing.stat_write_fallback &&
             config.damage.outgoing.multiplier != 1.0));
}

inline bool ShouldInstallDamageHook(const ModConfig& config) {
    return config.general.enabled &&
           ((config.damage.outgoing.enabled && config.damage.outgoing.multiplier != 1.0) ||
            (config.damage.incoming.enabled && config.damage.incoming.multiplier != 1.0) ||
            (config.mount.enabled && config.mount.lock_health) ||
            (config.mount.enabled && config.mount.special_enabled && config.mount.lock_special_health) ||
            !IsStatConfigNeutral(config.health));
}

inline bool ShouldInstallSharedStatHooks(const ModConfig& config) {
    return config.general.enabled &&
           (ShouldInstallMountStaminaHook(config) ||
            ShouldInstallLegacyStatWriteHook(config) ||
            ShouldInstallDamageHook(config) ||
            ShouldInstallSpiritHook(config));
}

inline bool ShouldInstallItemGainHook(const ModConfig& config) {
    return config.general.enabled && config.items.gain_multiplier != 1.0;
}

inline bool ShouldInstallAffinityHook(const ModConfig& config) {
    (void)config;
    return false;
}

inline bool ShouldInstallDurabilityHooks(const ModConfig& config) {
    (void)config;
    return false;
}

inline bool ShouldInstallDragonVillageSummonHook(const ModConfig& config) {
    (void)config;
    return false;
}

inline bool ShouldInstallDragonFlyingRestrictHook(const ModConfig& config) {
    (void)config;
    return false;
}

inline bool ShouldInstallDragonRoofRestrictHook(const ModConfig& config) {
    (void)config;
    return false;
}

inline bool ShouldInstallPositionHeightHook(const ModConfig& config) {
    (void)config;
    return false;
}

bool LoadConfig(const std::wstring& config_path);
bool ReadConfigSnapshot(const std::wstring& config_path, ModConfig* config);
std::vector<std::wstring> GetConfigMergePaths(const std::wstring& config_path);
void SetConfigSnapshot(const std::wstring& config_path, const ModConfig& config);
ModConfig GetConfig();
std::wstring GetLoadedConfigPath();
