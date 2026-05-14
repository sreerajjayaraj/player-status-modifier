# Nexus changelog - fix46

- Kept true layered INI support from fix45.
- Corrected config precedence so the default all-options config loads before the editable primary config.
- Load order is now default config, primary config, then selected module configs.
- This prevents `player-status-modifier.default.ini` from overwriting user changes in `player-status-modifier.ini`.
- Modular configs still combine normally, such as damage + affinity, damage + mount, or damage + mount + stamina/spirit + affinity.
- Missing INI sections remain neutral, so modules do not enable unrelated systems by accident.
