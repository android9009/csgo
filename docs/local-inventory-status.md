# Experimental local main-menu inventory integration

Requested behavior: `Share to inventory` mirrors the selected weapon/model and
finish into the client inventory immediately, including while in the main menu.
It must not send GC/Steam requests, grant real items, or alter real owned items.

## Located implementation

Osiris legacy CS:GO snapshot, MIT, immutable reference:
https://github.com/danielkrupinski/Osiris/tree/a20c93a2a913f90464d8cfb49716e3a5e6d8c7ab

Relevant files at that revision:
- `Source/InventoryChanger/GameIntegration/Inventory.cpp`, `Inventory::createSOCItem`:
  obtains local inventory/type cache, creates EconItem, fills IDs/account/definition,
  sets dynamic attributes, calls cache `addObject` and inventory `soCreated` (event 4).
- `Source/CSGO/CSPlayerInventory.h`: SOID, cache/owner access, notifications/removal.
- `Source/CSGO/PODs/EconItem.h`: legacy PE32 EconItem field layout.
- `Source/CSGO/PODs/SharedObjectTypeCache.h`: enumerate items and find highest IDs.
- `Source/CSGO/ItemSchema.h`: attribute setters, cache and InventoryManager methods.
- `Source/BytePatterns/WindowsClientPatternFinder.h`: discovery signatures.

The Fatality reference's `inventorychanger::add()` only appends an internal
configuration entry; it is not an implementation of main-menu SOC insertion.

## Offline verification

`tools/inspect_inventory_abi.py` inspects executable sections of a PE without
loading it. Run with pefile installed:

```
python tools/inspect_inventory_abi.py received-binaries/client.dll
```

`docs/inventory-abi-inspection.json` records the supplied client's SHA-256,
timestamp and signature matches. Inventory manager, item factory, type-cache
creation, dynamic attribute setter and custom-name setter signatures each have
one match in that binary. This does NOT establish complete ABI compatibility. See the opt-in backend below.

## Implemented opt-in backend

`features/skins/local_inventory.cpp` and the Weapons gear's **Share to inventory**
checkbox mirror copied SkinState CT/T selections plus selected knives/gloves,
agents, medals and music kits. Agents/medals resolve item definitions by schema
name or inventory image path; music resolves its kit ID from music_definitions
and creates item definition 1314 with integer attribute 166. Default removes the
corresponding synthetic item. No UI index is used as an economy definition.
Enabling it also shares existing selections. FRAME_START processes the state on
the game thread, including outside matches; no D3D-thread SOC calls or local
player requirement. Choice changes are picked up on the next frame. No Apply button.

- Item creation, paint/seed/wear, custom name and static StatTrak attributes.
- Defaults and checkbox-off remove only entries tracked in this module's ledger.
- Unchanged choices do not create duplicates. Changes replace the old synthetic
  item with a new identity to avoid stale inventory images/attributes.
- Monotonic IDs above observed economic IDs and inventory positions; ignore default
  high-bit IDs and reject exhaustion. Tested in `tests/local_inventory_ids_test.cpp`.
- Cache/account replacement drops stale pointers; every removal verifies pointer
  AND item ID membership in the current cache before callbacks/destruction.
- Shutdown requests game-thread cleanup, waiting up to 500 ms; it never invokes SOC
  callbacks on the unload worker. If unavailable, it logs and leaves the engine's
  cache intact rather than touching it from the wrong thread.
- No explicit GC/Steam requests, account ownership grants or real-item edits.
- Reference attribution/license retained in `third_party/licenses/Osiris-inventory.txt`.

The supplied binary's factory operand resolves to RVA `0x6ea210`: allocation is
`0x38` bytes and construction tail-calls `0x6d8b20`. Offline disassembly confirms
item IDs at +8/+16, attribute storage +24, account/position +28/+32, flags +48;
the deleting destructor returns with 4-byte argument cleanup. Cache creation
resolves to RVA `0x82ea10`, reads class ID at +32, and returns with 4-byte cleanup.
The backend rejects clients with a different PE timestamp (`0x65272025` required)
and requires all discovery signatures. Timestamp checks and SEH are not proof of
runtime safety; this remains experimental and defaults OFF.

## Outstanding / runtime validation required

- No game is runnable here. SOC callbacks, Panorama refresh and main-menu images
  still require game verification. Inspect `%TEMP%/csgo_base.log` for `local inventory:`.
- Stickers and rarity metadata are not mirrored. Agent/medal/music schema entries
  missing from this installation are logged and skipped, never guessed.
- The checkbox and selections use the existing session-only state, not persistence
  of synthetic Steam items. They do not survive a game restart automatically.
- Sharing now updates the local loadout slot immediately after the item-created
  notification, using the item definition's team slot (+0x28c array) or global
  slot (+0x268). The prior local slot ID is recorded/restored on removal, but
  only if it still refers to our item. Latest weapon selection revision wins
  when multiple configured alternatives share a slot. Existing in-match fallback
  skin application remains independent. This is local loadout-cache application,
  not a server-confirmed equip operation; Panorama badges/agent animation/audio
  still require runtime verification.
- A changed/discarded inventory cache is engine-owned; the module deliberately
  does not dereference the old cache to attempt cleanup after replacement.
- Check CT/T independently, main menu before joining a server, repeated selection,
  changes to wear/seed/name/StatTrak, Default, checkbox off/on, reconnect and unload.
- Cross-weapon paint kits can be selected but the engine may render them incorrectly
  or not at all on a different model. This is not fixed by SOC insertion.

## Live weapon-loadout filter

The Weapons gear now has separate Show all skins, Show all weapons and Share to
inventory checkboxes. Show all weapons defaults OFF. A read-only snapshot is
refreshed on the game thread every 250 ms independently of Share to inventory.
GetItemInLoadout is slot 8: the supplied client's CCSPlayerInventory primary
vtable is RVA 0xcace34, with method RVA 0x6cc240. Disassembly confirms team <=3
and slot <=56 checks. Its item static-data helper RVA 0x715880 reads the definition
from view+0x1ea (not old builds' +0x1ba). Runtime checks the exact getter target
before reading. Snapshots are copied under a mutex; no game pointers cross to GUI.
Render and hit testing use the same filtered column builder, with independent CT/T
selections. No snapshot means a waiting message, not an invented default loadout.
Show all weapons restores the complete existing team catalogue and does not change
which picked-up weapons receive configured fallback skins.

Show all skins changes available paint kit IDs, not the selected item definition.
Foreign paints no longer display a donor weapon's image. When this installation
has no generated PNG for that weapon/paint pair, the selected model's base icon
is used. This is NOT an implementation of arbitrary skinned icon generation;
correctly rendering an AWP finish on a USP remains subject to engine compatibility.

## Local-only application and picked-up weapon policy

Disassembly of manager vtable slot 29 (RVA 0x6e66b0, ret 0x18) shows protobuf
construction and queueing; despite its UpdateInventoryEquippedState name it is
NOT a purely local operation. Neither it nor slot 20 is called. Instead this
module updates only the validated local array at inventory+0xd8 (4 teams x 57
64-bit IDs), which GetItemInLoadout reads at RVA 0x6cc240. Original cache entries
are backed up before substitution. No GC equip message is sent by this module.

Weapons gear also has Allow weapon skins, default OFF. Ordinary weapon fallback
paints compare m_OriginalOwnerXuidLow/High against the local SteamID64 from player
info; account ID rewritten by the cosmetic override is never used to classify
ownership. OFF means original local owner only (unknown/zero owner is skipped),
ON also allows configured finishes on foreign/picked-up weapons. Turning it OFF
restores previously overridden foreign weapons through the existing original
state ledger. The policy change increments the selection revision exactly once;
unchanged commits do not cause periodic full updates. This setting does not
change which weapon models are visible in the menu.
