#include "config.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <filesystem>
#include <memory>
#include <mutex>
#include <vector>

namespace {

std::atomic<std::shared_ptr<const ModConfig>> g_config{std::make_shared<const ModConfig>(ModConfig{})};
std::mutex g_config_path_mutex;
std::wstring g_config_path;

bool ReadBool(const wchar_t* section, const wchar_t* key, const bool default_value, const std::wstring& path) {
    return GetPrivateProfileIntW(section, key, default_value ? 1 : 0, path.c_str()) != 0;
}

bool HasIniKey(const wchar_t* section, const wchar_t* key, const std::wstring& path) {
    wchar_t buffer[2]{};
    return GetPrivateProfileStringW(
               section,
               key,
               L"",
               buffer,
               static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0])),
               path.c_str()) > 0;
}

bool ReadBoolAlias(const wchar_t* section,
                   const wchar_t* canonical_key,
                   const wchar_t* alias_key,
                   const bool default_value,
                   const std::wstring& path) {
    if (HasIniKey(section, canonical_key, path)) {
        return ReadBool(section, canonical_key, default_value, path);
    }

    if (alias_key != nullptr && HasIniKey(section, alias_key, path)) {
        return ReadBool(section, alias_key, default_value, path);
    }

    return default_value;
}

DWORD ReadDword(const wchar_t* section, const wchar_t* key, const DWORD default_value, const std::wstring& path) {
    return static_cast<DWORD>(GetPrivateProfileIntW(section, key, static_cast<int>(default_value), path.c_str()));
}

double ReadDoubleRaw(const wchar_t* section, const wchar_t* key, const double default_value, const std::wstring& path) {
    wchar_t buffer[64]{};
    const auto default_text = std::to_wstring(default_value);
    GetPrivateProfileStringW(section,
                             key,
                             default_text.c_str(),
                             buffer,
                             static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0])),
                             path.c_str());

    wchar_t* end = nullptr;
    const double parsed = std::wcstod(buffer, &end);
    if (end == buffer || !std::isfinite(parsed)) {
        return default_value;
    }

    return parsed;
}

double ReadDouble(const wchar_t* section, const wchar_t* key, const double default_value, const std::wstring& path) {
    const double parsed = ReadDoubleRaw(section, key, default_value, path);
    if (parsed < 0.0) {
        return default_value;
    }

    return parsed;
}

void ReadBoolIfPresent(const wchar_t* section,
                       const wchar_t* key,
                       bool* const target,
                       const std::wstring& path) {
    if (target != nullptr && HasIniKey(section, key, path)) {
        *target = ReadBool(section, key, *target, path);
    }
}

void ReadBoolAliasIfPresent(const wchar_t* section,
                            const wchar_t* canonical_key,
                            const wchar_t* alias_key,
                            bool* const target,
                            const std::wstring& path) {
    if (target != nullptr && (HasIniKey(section, canonical_key, path) ||
                             (alias_key != nullptr && HasIniKey(section, alias_key, path)))) {
        *target = ReadBoolAlias(section, canonical_key, alias_key, *target, path);
    }
}

void ReadDwordIfPresent(const wchar_t* section,
                        const wchar_t* key,
                        DWORD* const target,
                        const std::wstring& path) {
    if (target != nullptr && HasIniKey(section, key, path)) {
        *target = ReadDword(section, key, *target, path);
    }
}

void ReadDoubleIfPresent(const wchar_t* section,
                         const wchar_t* key,
                         double* const target,
                         const std::wstring& path) {
    if (target != nullptr && HasIniKey(section, key, path)) {
        *target = ReadDouble(section, key, *target, path);
    }
}

void ReadDoubleRawIfPresent(const wchar_t* section,
                            const wchar_t* key,
                            double* const target,
                            const std::wstring& path) {
    if (target != nullptr && HasIniKey(section, key, path)) {
        *target = ReadDoubleRaw(section, key, *target, path);
    }
}

bool IsRegularFile(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

bool IsPrimaryConfigName(const std::filesystem::path& path) {
    return _wcsicmp(path.filename().c_str(), L"player-status-modifier.ini") == 0;
}

bool IsDefaultConfigName(const std::filesystem::path& path) {
    return _wcsicmp(path.filename().c_str(), L"player-status-modifier.default.ini") == 0;
}

bool IsLayerConfigName(const std::filesystem::path& path) {
    const auto file_name = path.filename().wstring();
    constexpr wchar_t kPrefix[] = L"player-status-modifier.";
    constexpr wchar_t kSuffix[] = L".ini";
    constexpr std::size_t kPrefixLength = (sizeof(kPrefix) / sizeof(kPrefix[0])) - 1;
    constexpr std::size_t kSuffixLength = (sizeof(kSuffix) / sizeof(kSuffix[0])) - 1;

    if (file_name.size() <= kPrefixLength + kSuffixLength) {
        return false;
    }

    if (_wcsnicmp(file_name.c_str(), kPrefix, kPrefixLength) != 0) {
        return false;
    }

    return _wcsicmp(file_name.c_str() + file_name.size() - kSuffixLength, kSuffix) == 0;
}

double ClampDouble(const double value, const double minimum, const double maximum, const double fallback) {
    if (!std::isfinite(value)) {
        return fallback;
    }

    if (value < minimum) {
        return minimum;
    }

    if (value > maximum) {
        return maximum;
    }

    return value;
}

void ApplyConfigLayer(const std::wstring& path, ModConfig* const next) {
    if (next == nullptr) {
        return;
    }

    ReadBoolIfPresent(L"General", L"Enabled", &next->general.enabled, path);
    ReadBoolIfPresent(L"General", L"LogEnabled", &next->general.log_enabled, path);
    ReadBoolIfPresent(L"General", L"Verbose", &next->general.verbose, path);
    ReadDwordIfPresent(L"General", L"MaxLogLines", &next->general.max_log_lines, path);
    ReadDwordIfPresent(L"General", L"InitDelayMs", &next->general.init_delay_ms, path);
    ReadDwordIfPresent(L"General", L"StaleComponentMs", &next->general.stale_component_ms, path);
    ReadDwordIfPresent(L"General", L"RelockIdleMs", &next->general.relock_idle_ms, path);

    if (HasIniKey(L"Damage", L"Multiplier", path)) {
        next->damage.outgoing.enabled = true;
        next->damage.outgoing.multiplier =
            ReadDouble(L"Damage", L"Multiplier", next->damage.outgoing.multiplier, path);
    }

    ReadBoolAliasIfPresent(L"OutgoingDamage", L"Enabled", L"Enable", &next->damage.outgoing.enabled, path);
    ReadBoolIfPresent(L"OutgoingDamage", L"UseStatWriteFallback", &next->damage.outgoing.stat_write_fallback, path);
    ReadDoubleIfPresent(L"OutgoingDamage", L"Multiplier", &next->damage.outgoing.multiplier, path);

    ReadBoolAliasIfPresent(L"IncomingDamage", L"Enabled", L"Enable", &next->damage.incoming.enabled, path);
    ReadBoolIfPresent(L"IncomingDamage", L"UseStatWriteFallback", &next->damage.incoming.stat_write_fallback, path);
    ReadDoubleIfPresent(L"IncomingDamage", L"Multiplier", &next->damage.incoming.multiplier, path);

    ReadDoubleIfPresent(L"Items", L"GainMultiplier", &next->items.gain_multiplier, path);

    ReadDoubleIfPresent(L"Affinity", L"Multiplier", &next->affinity.multiplier, path);
    ReadBoolIfPresent(L"Affinity", L"GiftDiagnostics", &next->affinity.gift_diagnostics, path);
    ReadBoolIfPresent(L"Affinity", L"PetDiagnostics", &next->affinity.pet_diagnostics, path);

    ReadDoubleRawIfPresent(L"Durability", L"ConsumptionChance", &next->durability.consumption_chance, path);

    ReadBoolAliasIfPresent(L"Mount", L"Enabled", L"Enable", &next->mount.enabled, path);
    ReadBoolIfPresent(L"Mount", L"LockHealth", &next->mount.lock_health, path);
    ReadBoolIfPresent(L"Mount", L"LockStamina", &next->mount.lock_stamina, path);
    if (HasIniKey(L"Mount", L"LockValue", path)) {
        DWORD lock_value = static_cast<DWORD>(next->mount.lock_value);
        ReadDwordIfPresent(L"Mount", L"LockValue", &lock_value, path);
        next->mount.lock_value = static_cast<int64_t>(lock_value);
    }

    ReadBoolIfPresent(L"DragonLimit",
                      L"roof_summon_experimental",
                      &next->dragon_limit.roof_summon_experimental,
                      path);
    ReadBoolIfPresent(L"DragonLimit", L"village_summon", &next->dragon_limit.village_summon, path);
    ReadBoolIfPresent(L"DragonLimit",
                      L"cancel_restrict_flying",
                      &next->dragon_limit.cancel_restrict_flying,
                      path);

    ReadBoolAliasIfPresent(L"Position Control(Height)",
                           L"Enable",
                           L"Enabled",
                           &next->position_control.enabled,
                           path);
    if (HasIniKey(L"Position Control(Height)", L"Key", path)) {
        DWORD key = static_cast<DWORD>(next->position_control.key);
        ReadDwordIfPresent(L"Position Control(Height)", L"Key", &key, path);
        next->position_control.key = static_cast<int>(key);
    }
    if (HasIniKey(L"Position Control(Height)", L"Amplitude", path)) {
        double amplitude = static_cast<double>(next->position_control.amplitude);
        ReadDoubleIfPresent(L"Position Control(Height)", L"Amplitude", &amplitude, path);
        next->position_control.amplitude = static_cast<float>(amplitude);
    }

    ReadBoolAliasIfPresent(L"Position Control(Horizontal)",
                           L"Enable",
                           L"Enabled",
                           &next->position_control.horizontal_enabled,
                           path);
    if (HasIniKey(L"Position Control(Horizontal)", L"Key", path)) {
        DWORD key = static_cast<DWORD>(next->position_control.horizontal_key);
        ReadDwordIfPresent(L"Position Control(Horizontal)", L"Key", &key, path);
        next->position_control.horizontal_key = static_cast<int>(key);
    }
    if (HasIniKey(L"Position Control(Horizontal)", L"Multiplier", path)) {
        double multiplier = static_cast<double>(next->position_control.horizontal_multiplier);
        ReadDoubleIfPresent(L"Position Control(Horizontal)", L"Multiplier", &multiplier, path);
        next->position_control.horizontal_multiplier = static_cast<float>(multiplier);
    }

    ReadBoolAliasIfPresent(L"Resistance", L"Enabled", L"Enable", &next->resistance.enabled, path);
    ReadDoubleIfPresent(L"Resistance", L"FireResistance", &next->resistance.fire_resistance, path);
    ReadDoubleIfPresent(L"Resistance", L"IceResistance", &next->resistance.ice_resistance, path);
    if (HasIniKey(L"Resistance", L"ElectricityResistance", path)) {
        ReadDoubleIfPresent(L"Resistance",
                            L"ElectricityResistance",
                            &next->resistance.electricity_resistance,
                            path);
    } else {
        ReadDoubleIfPresent(L"Resistance",
                            L"LightningResistance",
                            &next->resistance.electricity_resistance,
                            path);
    }

    const auto read_stat_section = [&](const wchar_t* const section, StatConfig* const stat) {
        const bool has_explicit_enable =
            HasIniKey(section, L"Enabled", path) || HasIniKey(section, L"Enable", path);
        const bool has_multiplier =
            HasIniKey(section, L"ConsumptionMultiplier", path) || HasIniKey(section, L"HealMultiplier", path);

        ReadBoolAliasIfPresent(section, L"Enabled", L"Enable", &stat->enabled, path);
        ReadDoubleIfPresent(section, L"ConsumptionMultiplier", &stat->consumption_multiplier, path);
        ReadDoubleIfPresent(section, L"HealMultiplier", &stat->heal_multiplier, path);

        // Backward compatibility: pre-split configs used the presence of non-neutral
        // multipliers as the enable signal and did not include Enabled=1.
        if (!has_explicit_enable &&
            has_multiplier &&
            (stat->consumption_multiplier != 1.0 || stat->heal_multiplier != 1.0)) {
            stat->enabled = true;
        }
    };

    read_stat_section(L"Health", &next->health);
    read_stat_section(L"Stamina", &next->stamina);
    read_stat_section(L"Spirit", &next->spirit);
}

void SanitizeConfig(ModConfig* const next) {
    if (next == nullptr) {
        return;
    }

    if (next->general.max_log_lines == 0) {
        next->general.max_log_lines = 2000;
    } else if (next->general.max_log_lines > 1000000) {
        next->general.max_log_lines = 1000000;
    }

    if (next->general.stale_component_ms == 0) {
        next->general.stale_component_ms = 60000;
    }

    if (next->general.relock_idle_ms == 0) {
        next->general.relock_idle_ms = 10000;
    }

    if (next->position_control.key <= 0) {
        next->position_control.key = VK_F6;
    }

    if (!std::isfinite(next->position_control.amplitude) || next->position_control.amplitude < 0.0f) {
        next->position_control.amplitude = 0.1f;
    }

    if (next->position_control.horizontal_key <= 0) {
        next->position_control.horizontal_key = VK_F7;
    }

    if (!std::isfinite(next->position_control.horizontal_multiplier) ||
        next->position_control.horizontal_multiplier < 0.0f) {
        next->position_control.horizontal_multiplier = 1.5f;
    }

    next->resistance.fire_resistance =
        ClampDouble(next->resistance.fire_resistance, 0.0, 0.99, 0.0);
    next->resistance.ice_resistance =
        ClampDouble(next->resistance.ice_resistance, 0.0, 0.99, 0.0);
    next->resistance.electricity_resistance =
        ClampDouble(next->resistance.electricity_resistance, 0.0, 0.99, 0.0);

    next->durability.consumption_chance =
        ClampDouble(next->durability.consumption_chance, 0.0, 100.0, 100.0);

    if (next->mount.lock_value <= 0) {
        next->mount.lock_value = 9999999;
    }

    if (!next->health.enabled) {
        next->health.consumption_multiplier = 1.0;
        next->health.heal_multiplier = 1.0;
    }

    if (!next->stamina.enabled) {
        next->stamina.consumption_multiplier = 1.0;
        next->stamina.heal_multiplier = 1.0;
    }

    if (!next->spirit.enabled) {
        next->spirit.consumption_multiplier = 1.0;
        next->spirit.heal_multiplier = 1.0;
    }
}

}  // namespace

std::vector<std::wstring> GetConfigMergePaths(const std::wstring& config_path) {
    std::vector<std::wstring> paths;
    const std::filesystem::path primary_path(config_path);

    const auto directory = primary_path.parent_path();
    std::error_code ec;
    if (!directory.empty() && std::filesystem::is_directory(directory, ec)) {
        const auto default_path = directory / L"player-status-modifier.default.ini";
        if (!IsDefaultConfigName(primary_path) && IsRegularFile(default_path)) {
            paths.push_back(default_path.wstring());
        }

        if (IsRegularFile(primary_path)) {
            paths.push_back(primary_path.wstring());
        }

        std::vector<std::filesystem::path> layers;
        for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
            if (ec) {
                break;
            }

            const auto path = entry.path();
            if (!IsRegularFile(path) ||
                IsPrimaryConfigName(path) ||
                IsDefaultConfigName(path) ||
                !IsLayerConfigName(path)) {
                continue;
            }

            layers.push_back(path);
        }

        std::sort(layers.begin(), layers.end(), [](const auto& left, const auto& right) {
            return _wcsicmp(left.filename().c_str(), right.filename().c_str()) < 0;
        });

        for (const auto& layer : layers) {
            paths.push_back(layer.wstring());
        }
    } else if (IsRegularFile(primary_path)) {
        paths.push_back(primary_path.wstring());
    }

    return paths;
}

bool ReadConfigSnapshot(const std::wstring& config_path, ModConfig* const config) {
    if (config == nullptr) {
        return false;
    }

    ModConfig next{};
    const auto paths = GetConfigMergePaths(config_path);
    for (const auto& path : paths) {
        ApplyConfigLayer(path, &next);
    }

    SanitizeConfig(&next);
    *config = next;
    return true;
}

void SetConfigSnapshot(const std::wstring& config_path, const ModConfig& config) {
    g_config.store(std::make_shared<const ModConfig>(config), std::memory_order_release);

    std::lock_guard lock(g_config_path_mutex);
    g_config_path = config_path;
}

bool LoadConfig(const std::wstring& config_path) {
    ModConfig next{};
    if (!ReadConfigSnapshot(config_path, &next)) {
        return false;
    }

    SetConfigSnapshot(config_path, next);
    return true;
}

ModConfig GetConfig() {
    const auto snapshot = g_config.load(std::memory_order_acquire);
    if (!snapshot) {
        return {};
    }

    return *snapshot;
}

std::wstring GetLoadedConfigPath() {
    std::lock_guard lock(g_config_path_mutex);
    return g_config_path;
}
