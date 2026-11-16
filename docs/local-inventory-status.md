# Local main-menu inventory integration — research, NOT implemented

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
one match in that binary. This does NOT establish ABI compatibility. No runtime
function or inventory mutation has been enabled by these research changes.

## Integration still required

- Disassemble pointer operands/targets and confirm allocation size, field offsets,
  SOID-by-value convention, attribute setter and virtual slots in this exact DLL.
- Resolve ItemSystem/schema and validate paint/seed/wear/name/StatTrak attributes.
- Process copied menu selections on the game thread even outside a match; do not
  call the cache from the D3D rendering thread or require a local player.
- Keep an owned-item ledger keyed by account/team/definition and selection revision.
  Repeated unchanged choices must not add duplicates. Update/recreate only our
  synthetic items; never remove or edit genuine items. Avoid ID collisions and
  retry cleanly if inventory is not ready or the account/cache changes.
- Implement removal and ownership-safe shutdown before enabling the UI checkbox.
- Publish weapon AND equipment selections/defaults, including changes made before
  enabling Share to inventory. Changing the checkbox must reconcile immediately.
- Verify Panorama image invalidation, main-menu visibility, map transitions,
  checkbox off, Default, reconnect and unload in game.

No working `Share to inventory` checkbox is claimed. Current game fallback paints
remain independent and continue to operate as before.
