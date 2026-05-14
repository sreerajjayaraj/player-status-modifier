# Nexus changelog - fix48

- Fixed mount lock regression introduced by the split-config transition.
- Mount health lock now installs the required stat-write hook again.
- Restored backward compatibility for older INI files that use stat multipliers without explicit
  `Enabled=1` flags.
- Older health, stamina, and spirit settings now behave as expected after the May 11, 2026 update.
- Keeps fix47 compatibility for `CrimsonDesert.exe` version `1.0.0.1235`.
