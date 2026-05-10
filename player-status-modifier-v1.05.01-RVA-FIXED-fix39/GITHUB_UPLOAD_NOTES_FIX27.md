# GitHub Upload Notes - Fix 27

Build id:

```text
safe-rva-fixed-fix27-affinity-friendly-diagnostics
```

Purpose:
- Keeps fix26 as the stable gameplay baseline.
- Leaves the stale legacy affinity scaler rejected unless both exact legacy hook sites validate.
- Adds guarded read-only diagnostics for player character to NPC and player character to pet/animal relationship paths under the game's friendly/intimacy system.
- The diagnostic hooks are installed only when `[Affinity] Multiplier` is not `1.0`.

Release artifacts:
- `release-artifacts/player-status-modifier.asi`
- `release-artifacts/player-status-modifier.default.ini`
- `release-artifacts/player-status-modifier.safe-tested.ini`

Artifact SHA256:

```text
B185AAE2650400B9F20F3E0A5E79F40F6A366888346414EDEBDC6A52C8EECBA6  player-status-modifier.asi
5B7A767627F2D920DE151D3AD1B29FA7583A5DE7708789766A11260329D8582B  player-status-modifier.default.ini
63C29E629BB67DABD6BD828EB1585DD6FDFFD374954D7D1803DC1971A3178D1E  player-status-modifier.safe-tested.ini
```

Verification already run:
- `tools/verify-release-asi.ps1`
