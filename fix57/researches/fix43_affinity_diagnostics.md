# Fix43 affinity diagnostics

Fix43 adds gated logging to answer two remaining questions:

- Which live value controls petting / animal affinity?
- Which live path controls NPC gift affinity?

The pet diagnostic hooks intentionally do not patch game state. The failed
fix41 experiment showed that treating `r9+0x20` as a pet delta can read pointer-
like garbage. Fix43 logs the low `r9` dword fields, `r9+0x20`, nearby stack
candidates, and `r13+0x38C..0x3A0` so the next decision can be based on live
evidence.

Use `player-status-modifier.affinity-diagnostics.ini` for a clean trace. During
the test, perform one NPC gift action and one petting action. The useful log
lines begin with:

- `hooks: affinity-item-event give`
- `hooks: affinity-item-event take`
- `hooks: affinity-petdiag-reloc`
- `hooks: affinity-petdiag-rsrc`
