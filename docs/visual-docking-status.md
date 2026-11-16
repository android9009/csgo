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
  a flat underlay for additive rim/legacy passes and restored tint parameters.

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
