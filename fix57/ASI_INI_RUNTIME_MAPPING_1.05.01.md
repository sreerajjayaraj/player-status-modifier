# ASI / INI Runtime Mapping for Crimson Desert 1.05.01

This mod is loaded by Ultimate ASI Loader through `version.dll`.

Expected deployment layout:

```text
CrimsonDesert\bin64\
  CrimsonDesert.exe
  version.dll
  player-status-modifier.asi
  player-status-modifier.ini
```

The loader only injects/loads the `.asi`. The `.ini` is opened by the ASI itself.

## Path resolution

`src/dllmain.cpp` stores the ASI module handle from `DllMain`, then calls:

```cpp
GetModuleFileNameW(g_module, ...)
```

and replaces the ASI filename with `player-status-modifier.ini`.

That means the INI must be beside the loaded ASI in `bin64`. The game working directory is not used for config discovery.

The log is resolved the same way:

```text
player-status-modifier.log
```

beside the ASI.

## Load chain

1. `version.dll` / Ultimate ASI Loader loads `player-status-modifier.asi`.
2. `DllMain` starts the mod init thread.
3. `InitializeMod()` verifies the host executable name is `CrimsonDesert.exe`.
4. `LoadConfig(config_path)` reads `player-status-modifier.ini`.
5. `InitializeLogger()` opens `player-status-modifier.log`.
6. `InstallHooks()` uses the loaded config to choose which 1.05.01 hook signatures to scan/install.
7. `StartConfigWatcher()` watches the same INI and hot-reloads changed values.

Most changed multiplier values apply at runtime on the next matching game write. Changes that alter which hooks need to exist are logged as requiring a game restart.

## 1.05.01 binary hook verification

Verified against the uploaded `CrimsonDesert.exe`.

- SHA-256: `4a858f3269fcea526f995b33d75a4c723b5df5fbb6a1a44d4a2691aa81629365`
- Player pointer hook: RVA `0x34FB1A`
- Player status marker source: `RDX`
- All currently selected signatures resolve uniquely in executable sections.
- The executable has no conventional `.text` section, so the scanner falls back to executable sections.

See `CRIMSON_DESERT_1.05.01_SCAN_REPORT.md` for the full pattern table.

## INI key to source/runtime mapping

| INI section/key | Source config field | Hook/install gate | Runtime effect |
|---|---|---|---|
| `[General] Enabled` | `ModConfig::general.enabled` | Global gate for optional hooks | Callbacks return without changing values when disabled |
| `[General] LogEnabled` | `general.log_enabled` | Logger setup/reload | Enables/disables `player-status-modifier.log` |
| `[General] verbose` / `Verbose` | `general.verbose` | Logger setup/reload | Full logging when enabled |
| `[General] MaxLogLines` | `general.max_log_lines` | Logger setup/reload | Caps log size when verbose is off |
| `[General] InitDelayMs` | `general.init_delay_ms` | Init thread | Delay before scanning/installing hooks |
| `[General] StaleComponentMs` | `general.stale_component_ms` | Runtime state | Expires stale captured player/mount components |
| `[General] RelockIdleMs` | `general.relock_idle_ms` | Mount resolver/runtime state | Controls relock timing after idle |
| `[OutgoingDamage] Enabled` or `Enable` | `damage.outgoing.enabled` | `ShouldInstallDamageHook()` | Scales outgoing negative health deltas |
| `[OutgoingDamage] Multiplier` | `damage.outgoing.multiplier` | `ShouldInstallDamageHook()` if not `1.0` | Multiplies outgoing damage |
| `[IncomingDamage] Enabled` or `Enable` | `damage.incoming.enabled` | `ShouldInstallDamageHook()` | Scales incoming negative health deltas |
| `[IncomingDamage] Multiplier` | `damage.incoming.multiplier` | `ShouldInstallDamageHook()` if enabled and not `1.0` | Multiplies incoming player-health damage in addition to `[Health] ConsumptionMultiplier` |
| `[Items] GainMultiplier` | `items.gain_multiplier` | `ShouldInstallItemGainHook()` if not `1.0` | Multiplies item gain amount |
| `[Affinity] Multiplier` | `affinity.multiplier` | `ShouldInstallAffinityHook()` if not `1.0` | Multiplies positive affinity deltas |
| `[Durability] ConsumptionChance` | `durability.consumption_chance` | `ShouldInstallDurabilityHooks()` if below `100.0` | Chance that durability/maintenance consumption proceeds |
| `[Mount] Enabled` or `Enable` | `mount.enabled` | Mount health/stamina gates | Enables mount/dragon stat lock logic |
| `[Mount] LockHealth` | `mount.lock_health` | Damage hook gate | Converts mount/dragon negative health delta into recovery |
| `[Mount] LockStamina` | `mount.lock_stamina` | Shared stat/stamina hook gate | Locks resolved mount/dragon stamina |
| `[Mount] LockValue` | `mount.lock_value` | Runtime mount lock | Desired mount health/stamina lock target, clamped to each stat max |
| `[DragonLimit] village_summon` | `dragon_limit.village_summon` | Dragon village hook gate | Bypasses village summon restriction after player context is captured |
| `[DragonLimit] cancel_restrict_flying` | `dragon_limit.cancel_restrict_flying` | Dragon flying hook gate | Prevents validated forced-dismount result |
| `[DragonLimit] roof_summon_experimental` | `dragon_limit.roof_summon_experimental` | Dragon roof hook gate | Experimental roof/high-place summon bypass |
| `[Position Control(Height)] Enable` or `Enabled` | `position_control.enabled` | Position hook gate | Starts height key listener and applies vertical delta |
| `[Position Control(Height)] Key` | `position_control.key` | Key listener | Windows virtual-key code |
| `[Position Control(Height)] Amplitude` | `position_control.amplitude` | Runtime position callback | Height added per key-listener tick |
| `[Position Control(Horizontal)] Enable` or `Enabled` | `position_control.horizontal_enabled` | Position hook gate | Starts horizontal key listener |
| `[Position Control(Horizontal)] Key` | `position_control.horizontal_key` | Key listener | Windows virtual-key code |
| `[Position Control(Horizontal)] Multiplier` | `position_control.horizontal_multiplier` | Runtime position callback | Scales X/Z movement delta while key is held |
| `[Health] ConsumptionMultiplier` | `health.consumption_multiplier` | Damage hook gate | Base multiplier for incoming negative player-health deltas; combines with `[IncomingDamage]` when enabled |
| `[Health] HealMultiplier` | `health.heal_multiplier` | Damage hook gate | Scales positive player-health deltas |
| `[Stamina] ConsumptionMultiplier` | `stamina.consumption_multiplier` | Shared stat/stat-write hook gate | Scales negative player-stamina writes |
| `[Stamina] HealMultiplier` | `stamina.heal_multiplier` | Shared stat/stat-write hook gate | Scales positive player-stamina writes/natural regen |
| `[Spirit] ConsumptionMultiplier` | `spirit.consumption_multiplier` | Spirit-delta hook gate | Scales negative spirit delta |
| `[Spirit] HealMultiplier` | `spirit.heal_multiplier` | Spirit-delta hook gate | Scales positive spirit delta |
| Legacy `[Damage] Multiplier` | outgoing damage fallback | Damage hook gate | Used only when `[OutgoingDamage] Multiplier` is absent |

## Alias compatibility added

The current default INI already works as-is. This update additionally accepts these aliases so small INI naming differences do not silently disable a feature:

```ini
[OutgoingDamage]
Enable=1
; or
Enabled=1

[IncomingDamage]
Enable=1
; or
Enabled=1

[Mount]
Enable=1
; or
Enabled=1

[Position Control(Height)]
Enable=1
; or
Enabled=1

[Position Control(Horizontal)]
Enable=1
; or
Enabled=1
```

When both canonical and alias keys exist, the canonical key in the default INI wins.

## Runtime fixes in this package

- `[IncomingDamage] Enabled/Multiplier` now has a real runtime path. Incoming player-health damage uses:
  `Health.ConsumptionMultiplier * IncomingDamage.Multiplier` when `[IncomingDamage] Enabled=1`.
- `[Mount] LockValue` now controls mount/dragon health and stamina lock targets. The value is clamped to the resolved stat max, so the default `9999999` means “lock to max” for mounts with lower max values.
- The old fallback behavior is still kept if a mount stat entry cannot be read safely: negative mount deltas are inverted instead of being allowed through.

## Runtime validation checklist

After placing the ASI and INI in `bin64`, launch the game and inspect `player-status-modifier.log`.

Expected lines include:

```text
dllmain: host process = ...CrimsonDesert.exe
dllmain: config path = ...\bin64\player-status-modifier.ini
config: ...
hooks: installed player-pointer hook ... marker-source=rdx
hooks: loadout ...
dllmain: initialization finished
```

If an INI value is not taking effect, compare the `config:` lines with the INI. Those lines are emitted from the values actually parsed by the ASI.
