# Security Scan Notes

This is an unsigned hobby ASI plugin. Unsigned status alone is expected for local mod builds.

## Expected Scan Signals

- No digital signature.
- Runtime process/module inspection.
- Memory reads/writes inside `CrimsonDesert.exe`.
- Possible TLS callback warning depending on compiler output.

## Safety Measures

- The ASI is intended to run only inside `CrimsonDesert.exe`.
- Runtime lookup is anchored to the Crimson Desert module.
- The user-facing Nexus zip contains only the ASI and INI.
- No file overlay was observed in the DMM scan for the current ASI line.

