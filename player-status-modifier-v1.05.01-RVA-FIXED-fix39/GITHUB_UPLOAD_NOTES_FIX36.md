# GitHub upload notes - fix36

Superseded by fix37: gameplay showed donation affinity caps at `+100`, so the
fix37 companion JSON patches donation directly to `100`.

Build id:

`safe-rva-fixed-fix36-affinity-donate-pet-base-open`

Purpose:

- Stable continuation of fix35.
- Corrects the petting expectation: petting base is still vanilla `+5`; the
  observed `+15` pet result is `+5` base plus the separate `+10` abyss gear
  bonus.
- Extends the DMM companion patch to donation affinity:
  `release-artifacts/Player_Status_Modifier_Affinity_3x_DMM.json`.

Expected test result:

- NPC greeting friendly gain should remain `+15`.
- Give coin / donation gain should become `+150`.
- Petting without a newly discovered pet-specific path remains `+5`.
- Petting with the tested abyss gear remains `+15` total because the gear bonus
  is separate from the unmodified petting base.

Testing note:

Disable overlapping trust/friendly patches while testing this companion JSON.
The INI `Affinity` multiplier and the ASI runtime hooks are not currently the
source of the visible greeting, donation, or petting base UI increments.
