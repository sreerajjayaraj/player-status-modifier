# GitHub upload notes - fix48

- Source build id: `safe-rva-fixed-fix48-mount-statwrite-legacy-ini`
- Base game target: `CrimsonDesert.exe` version `1.0.0.1235`
- Primary runtime fix:
  mount lock now installs the stat-write hook again, and older INI files without explicit stat
  `Enabled=1` flags continue to work.
- Recommended packaging:
  include the rebuilt `player-status-modifier.asi`, refreshed `SHA256SUMS.txt`, default/full INI,
  split module INIs, and the DMM module zip.
