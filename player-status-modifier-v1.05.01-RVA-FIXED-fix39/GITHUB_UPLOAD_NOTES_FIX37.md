# GitHub upload notes - fix37

Build id:

`safe-rva-fixed-fix37-affinity-donate-cap-gift-pet-open`

Purpose:

- Stable continuation of fix36.
- Corrects the donation target after gameplay confirmed the visible donation
  affinity cap is `+100`.
- Companion DMM JSON now patches:
  - NPC greeting friendly gain: `+5 -> +15`.
  - Beggar coin / donation gain: `+50 -> +100`.
- Documents that NPC gift and petting base gain remain open and are not changed
  by this build.

Expected test result:

- NPC greeting friendly gain should remain `+15`.
- Give coin / donation gain should remain `+100`.
- NPC gift gain should remain `+5` until a gift-specific path is found.
- Petting with the tested abyss gear remains `+15` total because the gear bonus
  is separate from the unmodified `+5` petting base.

Testing note:

Disable overlapping trust/friendly patches while testing this companion JSON.
The INI `Affinity` multiplier and the ASI runtime hooks are not currently the
source of the visible greeting, donation, gift, or petting base UI increments.
