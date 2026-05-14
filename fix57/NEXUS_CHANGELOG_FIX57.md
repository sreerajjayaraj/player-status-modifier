# Player Status Modifier Fix57 Changelog

- Confirmed working mount stamina lock for normal horses and special rideable mounts.
- Fixed mounted stamina drain that used type-18 spirit-backed stamina entries through the `stamina-ab00` hook.
- Locks both the main rider-spirit mounted stamina drain and auxiliary rideable stamina pools while `[Mount] LockStamina=1`.
- Keeps normal unmounted player spirit behavior on the regular spirit multiplier path.
- Preserves mounted health/stamina relock logic from previous builds.
- Keeps split INI layering support so users can combine modules such as damage-only, mount-only, stamina/spirit, durability, affinity, items, health, and resistance.
- Includes default all-options INI plus separate optional module INIs for users who want smaller feature sets.
- Build id: `safe-rva-fixed-fix57-mounted-type18-stamina-hardlock`.

