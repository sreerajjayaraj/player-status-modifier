# Fix46 layered config precedence

Fix46 keeps the fix45 layered INI loader but corrects precedence.

Load order is now:

1. `player-status-modifier.default.ini`
2. `player-status-modifier.ini`
3. Other sibling `player-status-modifier.*.ini` module files, alphabetically

This makes the default/full config a base layer. A user-edited primary `player-status-modifier.ini` can override it, and selected module files can override either one.

The result is safer for DMM/manual installs where both the default full config and the editable primary config may be present.
