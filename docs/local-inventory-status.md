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
checkbox mirror copied SkinState CT/T selections plus selected knives/gloves.
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
- Agents, medals, music and stickers are not mirrored yet; current coverage is
  weapon finishes, knives and gloves. Rarity metadata is not mirrored.
- The checkbox and selections use the existing session-only state, not persistence
  of synthetic Steam items. They do not survive a game restart automatically.
- Sharing does not automatically equip the synthetic item into the game's loadout.
  Existing game skin application remains a separate fallback system.
- A changed/discarded inventory cache is engine-owned; the module deliberately
  does not dereference the old cache to attempt cleanup after replacement.
- Check CT/T independently, main menu before joining a server, repeated selection,
  changes to wear/seed/name/StatTrak, Default, checkbox off/on, reconnect and unload.
- Cross-weapon paint kits can be selected but the engine may render them incorrectly
  or not at all on a different model. This is not fixed by SOC insertion.
