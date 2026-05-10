# JSON Status

This safe release intentionally keeps the existing INI parser.

Reason: the current INI path is the tested runtime path. Switching to JSON at the same time as runtime hook changes would add avoidable risk.

Recommended next step, after all runtime functions are stable:

1. Add JSON config support.
2. Load `player-status-modifier.json` first.
3. Fall back to `player-status-modifier.ini`.
4. Keep the same field names and value ranges.

Do not remove INI support until the JSON loader has been tested across several sessions.
