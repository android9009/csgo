# ESP / Chams preview and world ESP status

## Implemented

- The ESP and Chams viewports use the existing Fatality controls and Source 1 merged-agent renderer. Local follows the live agent model/team; Team/Enemy use the corresponding side. The skinchanger remains separately framed.
- Chams preview selects **Player / Local Type** according to preview Type, respects General > Enabled, and updates the primary material/color/alpha. Normal/Flat use engine debug materials. Other types use installed engine effect materials, with a logged Flat fallback if missing. Rim/Contour use a second effect pass for the secondary color. This is not a port of the original Fatality custom shader definitions.
- Borrowed material modulation and Ignore-Z flags are restored after the preview pass, including the render-fault recovery path. The game DrawModelExecute hook now shares this scoped material implementation.
- ESP preview: projected model-bound box, sample name/HP/armor/ammo/weapon/icon/selected info flags, and a sound-radius guide. Colors and enable switches are connected to the native GUI configuration. Preview values are deterministic samples, not another player's live information. Skeleton/History skeleton currently show a static standing-pose guide, **not** actual animated bones/history.
- LMB drags name, HP, armor, ammo, weapon name/icon and the flags group in the ESP preview. Dragging empty space rotates the model. RMB on an element resets it. Whole elements are clamped to the preview. Positions are normalized to the model box (DPI-independent), separate for Enemy/Team/Local, and used by live player ESP. Box and pose guides stay attached to the model. This does not introduce an ImGui dependency. Enemy/Team/Local are edited using the existing preview Type selector; the PLAYER controls and their colors switch profiles with it.
- World ESP reads dropped weapon/grenade/C4 entities, excludes carried items, honors three independent Enable toggles and icon/color settings, and draws category vector markers plus readable entity labels. These are lightweight markers, not the game's original weapon-specific HUD glyphs.
- Planted C4 supports the existing Timer / Damage / Defusing options. Damage is explicitly an approximation using distance and armor, without tracing cover. No netvar offset is guessed for bomb state: an unresolved offset disables the corresponding read.

## Performance / threading

Source model/material/entity APIs remain on the game render thread; moving those calls to a worker would be unsafe. The GUI consumes copied numeric snapshots, never retained entity or studio-header pointers.

- Frozen preview pixels are redrawn only when model, yaw, skinchanger aspect, appearance, or device/reset state changes. This also caches the projected preview box.
- Model-file availability and material lookups are cached; no per-frame repeated file opens or material creation.
- World scanning is gated behind enabled categories, capped to 4096 entities / 256 labels, and refreshed at most every 100 ms (immediately on category changes). Projection uses one copied view matrix per frame. Moving projectiles can therefore have up to 100 ms position latency.
- No asynchronous jobs call engine interfaces. No new unbounded render queues, downloads or per-frame config writes.

## Explicit limitations / still unverified

- Game model passes now support Player, Ragdoll, Attachments and local Arms material settings, plus dropped Weapon / Grenade / C4 materials with independent material-color pickers. Through-wall/visible colors are separate passes where Ignore Z is configured. Existing engine-forced overrides are respected.
- Engine glow now applies independent player-side and object-category settings to existing in-use glow entries. The old inactive SDK binding had the wrong manager dereference, vector ABI, and entry layout; it was corrected against the non-LEGACY layout in this repository's origin/main source. Writes are bounded and prior values are restored on disable and on the game-thread shutdown path. Entities without an engine-registered glow entry are not artificially registered and may not glow.
- Live player ESP now reads health/armor/ammo/name/weapon, projects hull boxes and cached bone segments, applies enabled real flags (scoped/armor/kit/immunity and active C4/taser), and uses the corresponding Enemy/Team/Local colors and drag layout. It does not synthesize exploit/lagcomp/hit/money flags for which no data is captured. Live Sound/History skeleton remain unconnected to actual sound/history events. Local ESP is not hidden automatically in first person; normal projection can reject a box intersecting the near plane.
- Hidden/Ignore-Z preview cannot demonstrate occlusion without an occluder; the visible pass shows the visible color. Backtrack/On-shot/Fake require historical/shot/fake-animation state absent from this base and are not implemented as game passes. Ragdoll/Arms/Attachments have game material passes but are not simulated by the standing player preview. Skip occlusion does not yet alter engine PVS/bone-setup culling, and Hide sleeves is not connected.
- Drag layout is session-local, matching the rest of the GUI configuration; no new config-file format is silently introduced.
- Material availability, shader appearance, model ABI, input behavior, and real frametimes require verification in the user's CS:GO build. Windows CI confirms compilation, not runtime correctness or a no-stutter guarantee.

## Runtime acceptance checks

1. Open Chams, enable General, choose Enemy/Team/Local, select Normal/Flat and a distinct RGBA color. Verify immediate change and no leaked tint on game models or the skinchanger after switching tabs.
2. In ESP enable Esp, Box, Name, HP/Armor/Ammo, Weapon/Icons and Info. Rotate the model; verify the box tracks the projected bounds. The footer identifies sample ESP values.
3. Drag each label/bar, leave the viewport while dragging, release, and rotate again. RMB resets the element. Close/reopen menu, change DPI and Type; input must not stick.
4. Toggle Weapon/Grenades/C4 independently in a match with dropped items. Carried items must not receive labels; plant/defuse a bomb and compare countdowns. Test spectator/disconnect/map switch.
5. Profile frametimes with all categories enabled; compare idle preview vs continuous rotation. Test D3D9 reset and unload. No measured performance claim is made before these checks.

## Additional safety / tests

The player's IClientNetworkable::GetClientClass now uses the x86 networkable subobject (+8), not the unrelated entity-vtable slot. Thrown projectiles are not mistaken for carried weapons just because they retain an owner handle. Glow is applied before rendering; off-thread unload requests cleanup on the game thread instead of writing from the worker. Material effects have a cheap disabled fast path and copy only the effects snapshot, not all player ESP settings, per model draw. Eleven portable test suites cover team routing, independent snapshots/layouts, world classification/projection, damage approximation, preview framing, and existing skin/inventory state. Native game rendering still needs runtime testing.
