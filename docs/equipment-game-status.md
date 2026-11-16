# Experimental game equipment application

`features/skins/equipment_state.hpp` holds copied CT/T selections. Menu agent
clicks, knife/glove selection, finish changes and Default now publish to it.
`equipment_changer.cpp` runs after network post-data-update through the existing
skin hook, independently of whether ordinary weapon paints are enabled.

Implemented paths:

- Local agent model-index override, preserving the original for Default/unload.
- Knife definition and fallback attributes, first-person viewmodel and worldmodel
  replacement; saved entity attributes/models are restored on Default/drop.
- Existing wearable override, or creation via the game's CEconWearable factory;
  local wearable handle assignment, body setting, fallback attributes, model
  switch and network pre/post-data update. Only self-created wearables may be
  destroyed. Destruction checks the serial handle and class identity.
- Missing netvars/model indices skip the operation. No guessed item field
  offsets; virtual interface slots follow the legacy Source/Fatality interfaces.

Limits requiring Windows/game verification:

- This is client-side cosmetic state, not Steam inventory mutation.
- A requested model must already be available in the model index. Missing
  precached models are logged and skipped; there is no string-table injection.
- Wearable attachment/material updates are experimental. A CEconWearable factory
  plus a successful compile do not prove visible gloves on every client build.
  Fatality's build-specific equip-glove function/leaf-system calls have not been
  copied without verification.
- Knife animation-sequence remapping, killfeed icon replacement, ragdoll model
  overrides and agent arm-model replacement are not implemented here.
- Engine exceptions are caught by the existing hook, but this does not guarantee
  memory safety with an incompatible engine ABI. No in-game test was performed.
- Preview glove replacement and arbitrary catalog material generation remain
  separate unfinished tasks, not implied by this game-equipment implementation.

Manual checks: CT and T separately, Default restoration, weapon switching,
dropping/picking up knives, death/respawn, team/map change, reconnect and unload.
Check `%TEMP%/csgo_base.log` for `equipment:` messages and hook exceptions.

## Full-update scheduling

Full updates are now scheduled centrally by `SkinChanger` only when the copied
weapon/equipment selection revision actually changes. Unchanged Set/Select,
reopening a catalog, repeated Default and repeated agent selection are idempotent.
The revision gate survives entity/network/map resets. Entity respawn, dropped-item
restoration or the server resending a knife definition does not schedule a new
full update. Local netvar enforcement and necessary per-entity notifications are
separate from full updates. Rapid edits are coalesced by the existing throttle;
there is no periodic full-update timer. Explicit unload may still request one
restoration update.

`tests/skin_refresh_test.cpp` simulates 10,000 unchanged commits per category and
asserts that only real edits/default transitions generate a revision event.
