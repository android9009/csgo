# ESP docking / reference materials

## Implementation

- A shared player ESP renderer consumes independent Enemy/Team/Local layouts.
  Seventeen dock slots include eleven individually draggable horizontal flags.
  Bars occupy inner lanes and change orientation/length with their dock side;
  labels stack outside them. Right-click restores an individual slot.
- Health, armor and ammo use explicit gradient toggles and secondary colors.
- Preview skeleton segments come from the synchronous StudioRender draw rather
  than a schematic pose. If capture fails there is no fabricated skeleton.
- Preview glow is a GUI alpha-silhouette approximation, not the engine glow
  manager. It must be visually checked on the target game build.
- The six reference VMT definitions are used for live and preview chams, with
  a flat underlay for Rim Glow only. Legacy borrows and retains the engine
  material, as in the reference; tint parameters are written as in Fatality.

## Provenance

VMT definitions and embedded weapon icon font originate in the user-supplied
https://github.com/reversed2603/fatality_source at commit
`dee7327e3b55c03ee93e9eaa0c4dcada64e49110`, files
`internal_hvh/misc/init.cpp` and `internal_hvh/weaponicons.h`.
No license file was found in that checkout; this attribution does not grant
redistribution rights. The font is not a newly drawn vector icon set. Glyph
mapping and appearance still require a visual check.
The KeyValues FromString calling convention is adapted from MIT-licensed
Osiris, covered by `third_party/licenses/Osiris-inventory.txt`.

## Evidence and limitations

Logs11 show a successful startup followed by exit code 100, without an exception
address sufficient to identify the Weapon toggle crash. The entity-class scanner
now decodes and validates the pinned client's constant class getters instead of
invoking them. This is defensive hardening, not proof of the crash's cause or
resolution. Scan activation/count diagnostics are logged for the next game test.

Portable state/docking tests do not validate Windows ABI, GPU shaders, materials,
preview bone capture, glyph correspondence, drag interaction or runtime stability.
All of those require an in-game pass on the supplied binary version.

## Follow-up to image-48

- Dragging now moves only the grabbed element under the cursor. A release commits
  its nearest side/grid slot; clicking without moving does not reorder it. The
  hover rectangles and drag-side highlight are removed.
- Visuals preview now fits the agent's studio bounds to the actual viewport
  aspect, rather than using a fixed 390-unit camera. The projected box uses the
  same transformed bounds as the model, instead of a fixed 72-unit reference hull.
- Live player bounds clip the hull's edges at the camera plane and intersect
  with the viewport. Previously one off-screen corner rejected the entire
  player; this explains the close-range disappearance in that code path.
- Chams now retain the borrowed Legacy material like the reference, remove the
  non-reference Legacy flat underlay and extra IMaterialVar vector getter, reset
  render-view modulation before drawing, and explicitly draw filled material
  passes (restoring the prior wireframe flag afterwards). Missing shaders leave
  the original model intact instead of silently becoming a debug material.
- Exception code/instruction diagnostics were added for model-pass faults.

The close-range regression has portable clipping tests. Runtime Legacy stability,
material appearance, camera framing and interactive dragging still need an
in-game check; no supplied crash dump establishes the Legacy crash cause.

## Follow-up to image-49 / supplied chams.cpp and chams.h

- Both agent previews now use a yaw-independent enclosing-cylinder camera fit.
  Rotation is about the fixed model root (local X/Y zero), not the asymmetric
  studio-box midpoint. Only orientation changes with horizontal drag; the camera
  distance and translation no longer change with yaw. Natural perspective and
  the silhouette can still change as a 3D model turns.
- Visuals framing leaves a footer for the docked bars/weapon label/icon. The
  skinchanger retains its separate larger framing, without yaw-dependent zoom.
- The attached chams.cpp SHA-256 is
  `7ba4bbdc8f99c76a111ea074ca2e09b0f5aa51d289d174e4329cfe5edeb15473`,
  byte-identical to the previously used Fatality reference file. It applies
  Normal, Flat, Rim Glow, Contour, Flow, Acrylic and Legacy; the sprite entry is
  auxiliary, not an agent chams choice. Raw VMT definitions are in misc/init.cpp,
  not in either uploaded chams file. All six copied VMT strings were compared
  with that init.cpp (ignoring whitespace) and match. Legacy is the engine's
  dev/glow_armsrace. The >0.95 opacity snap from apply_material is now matched too.

Portable framing tests sweep all yaw angles with the same camera distance and
check corner containment. Windows compilation and an in-game visual check are
separate validations; no in-game pass was available in this environment.
