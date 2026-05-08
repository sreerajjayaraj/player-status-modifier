# GitHub Upload Notes - Fix 22

Build id:

```text
safe-rva-fixed-fix22-ground-mount-sanity-guard
```

Purpose:
- Fixes unsafe/stale ground-mount relock behavior seen in the Silverfang-only wolf mount log.
- Keeps source, docs, tools, and vendored `deps/safetyhook` in a clean uploadable tree.
- Excludes local CMake output (`out/`) and nested dependency git metadata.

Release artifacts:
- `release-artifacts/player-status-modifier.asi`
- `release-artifacts/player-status-modifier.default.ini`
- `release-artifacts/player-status-modifier.safe-tested.ini`

Artifact SHA256:

```text
1D4F102E27816B2928D6CB14835E1BE34B911C71C2F6A60CA763DD993DB7D373  player-status-modifier.asi
5B7A767627F2D920DE151D3AD1B29FA7583A5DE7708789766A11260329D8582B  player-status-modifier.default.ini
63C29E629BB67DABD6BD828EB1585DD6FDFFD374954D7D1803DC1971A3178D1E  player-status-modifier.safe-tested.ini
```

Verification already run:
- `tools/verify-release-asi.ps1`
- `tools/audit-source-tree.ps1`
