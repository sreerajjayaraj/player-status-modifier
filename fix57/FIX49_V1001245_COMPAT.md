# Fix49 v1.0.0.1245 compatibility

- Adds compatibility for `CrimsonDesert.exe` version `1.0.0.1245`.
- Static PE scan confirms the core player, mount, stats, damage, item, durability, dragon, and
  position hook signatures still resolve uniquely after the game update.
- Updates the moved affinity-friendly callsite to RVA `0x0FAB2A84`.
- Updates the moved affinity pet resource diagnostic callsite to RVA `0x0C98C977`.
- Keeps the fix48 mount stat-write and legacy INI behavior.
- Requires a fresh runtime log to confirm live hook installation inside the updated game process.
