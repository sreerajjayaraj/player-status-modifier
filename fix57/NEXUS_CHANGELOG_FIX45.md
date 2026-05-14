# Nexus changelog - fix45

- Added true layered INI support: the ASI now reads `player-status-modifier.ini` and every sibling `player-status-modifier.*.ini`.
- Split configs now work in combinations such as affinity + damage, damage + mount, or damage + mount + stamina/spirit + affinity.
- Missing sections now stay neutral, so a damage layer does not activate affinity, items, durability, mount, dragon, health, stamina, or spirit by accident.
- Retained full all-options configs for users who prefer one INI.
- Startup log now lists every loaded config layer for easier support/debugging.
- Petting affinity is still not solved by this build; the affinity diagnostics layer remains available for route research.
