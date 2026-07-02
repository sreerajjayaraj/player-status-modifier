# Player Status Modifier d101

Build ID: `fix208-d101-1.0.0.1855-player-priority-mount-source`

Purpose: fix the d100 overlap where playable-character resource blocks were first accepted as `rideable-proxy` mounts, then later promoted as player resources. That caused Damiane/Oongka stamina regeneration and Damiane registered-mount stamina handling to follow the wrong owner path.

Changes:

- Classify player tracked entries before trusted mount entries when an entry overlaps both snapshots.
- Clear a stale trusted mount overlap during stats rebind instead of permanently rejecting the new player resource snapshot.
- Reject new `rideable-proxy` mount candidates from generic stat-component scans unless a real mount context has already identified the same mount.

Deployment:

- Built with Visual Studio 18 2026 x64 in `out/build/x64-Release-d101`.
- TLS directory stripped with `tools/strip-empty-tls-directory.js`.
- DMM ASI deployed to `E:\Downloads\Compressed\CD_Mods\DMM\mods\player-status-modifier.asi`.
- No ASI was copied directly to `bin64`.

Hashes:

- d101 final ASI SHA256: `C8C73CC4D68DFAA31C183C4A082921AF17AF36199B8D287C6950371C7F99EC06`
- d100 rollback ASI SHA256: `A5048F610802A04D0DA983FFDD6B1374F2BCB2AE185AABF7C76E0BC74ED46CF3`
- d100 rollback copy: `E:\CD_Mods_Master\backups\player-status-modifier\d100-before-d101-20260702\player-status-modifier.d100.before-d101.asi`

Expected log markers:

- `runtime: cleared stale trusted mount overlap during stats rebind`
- `runtime: mount stat-component rejected reason=rideable-proxy-needs-mount-context`
- Fewer or no persistent `trusted-mount-overlap` rebind rejects for active player stamina.
