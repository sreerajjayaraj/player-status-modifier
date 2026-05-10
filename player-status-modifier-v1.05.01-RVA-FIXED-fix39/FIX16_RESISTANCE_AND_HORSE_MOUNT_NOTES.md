# FIX16 Resistance + Horse Mount Resolver (Crimson Desert 1.05.01)

Base: uploaded `player-status-modifier-FIX14-WITH-RESISTANCE-v1.05.01-BUILDFIX` source, with the stable fix-14 tracking guard preserved and fix-15 horse-mount resolver changes merged.

Executable verified:
- `CrimsonDesert.exe` SHA-256: `4a858f3269fcea526f995b33d75a4c723b5df5fbb6a1a44d4a2691aa81629365`

## Resistance audit

The resistance implementation in the uploaded FIX14-WITH-RESISTANCE build was not valid enough to keep as-is.

Problems removed:
- It treated the damage hook `status_id` as if it were an elemental damage type. In the current hook path, that value is the target status/stat id; health is `0`.
- It used placeholder comments and applied the highest configured resistance to every negative damage-hook delta, regardless of element.
- It used `LightningResistance`, but the executable contains `ElectricityResistance` naming instead.

Executable string evidence:
- `FireResistance`: file offsets/RVAs [('0x4c4b3e6', '0x4c4bfe6'), ('0x4df4628', '0x4df5228')]
- `IceResistance`: file offsets/RVAs [('0x4c4b3f5', '0x4c4bff5'), ('0x4df4618', '0x4df5218')]
- `ElectricityResistance`: file offsets/RVAs [('0x4afb562', '0x4afc162'), ('0x4c4b403', '0x4c4c003')]
- `LightningResistance`: no exact string occurrence found

FIX16 replacement behavior:
- `[Resistance]` is now a real, guarded incoming-health resistance multiplier on the tracked player stat-write path.
- It does not pretend to know fire/ice/electricity damage type IDs.
- It applies only to tracked player health decreases, after `[Health] ConsumptionMultiplier` and `[IncomingDamage] Multiplier`.
- Effective additional multiplier is `1.0 - max(FireResistance, IceResistance, ElectricityResistance)`.
- `LightningResistance` is still accepted as a legacy INI alias, but `ElectricityResistance` is the canonical key.

Expected log:
```text
config: Resistance Enabled=1 Fire=0.750 Ice=0.000 Electricity=0.000 EffectiveIncomingMultiplier=0.250
runtime: applied incoming resistance old=... requested=... resistance_multiplier=0.250 final_consumption_multiplier=...
```

## Horse/mount resolver changes

Merged fix-15 horse mount work into the uploaded resistance branch:
- Adds horse-sized mount profile thresholds.
- Keeps dragon-sized mount support.
- Does not blindly lock generic 70000/85000 health entries unless there is mount context.
- Adds raw stamina diagnostics for the existing stamina hook.
- Logs horse-sized non-player health candidates instead of locking them blindly.

Expected useful mount logs:
```text
hooks: stamina-ab00 raw callback ...
runtime: tracked mount profile=horse ...
runtime: locked mount health write ...
runtime: locked mount-like stamina delta ...
runtime: observed non-player health candidate ... note=not-locked-without-mount-context
```

## Unchanged from stable baseline

- Affinity remains guarded/off; no partial affinity-current hook.
- Player health tracking hard guard remains.
- Incoming damage, outgoing damage, item gain, durability and spirit fixes remain.
