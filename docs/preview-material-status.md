# Weapon preview material investigation

The input DLLs are kept outside Git in `received-binaries/`. They were inspected
as data, not loaded or executed. `preview-abi-inspection.json` records hashes and
RTTI candidates so future work can identify the exact builds. Run:

```
python -m venv /tmp/preview-analysis
/tmp/preview-analysis/bin/pip install pefile
/tmp/preview-analysis/bin/python tools/inspect_preview_abi.py \
  received-binaries/client.dll received-binaries/materialsystem.dll
```

## Established from these binaries

- Client timestamp: `0x65272025`; material system timestamp: `0x63dc3de4`.
- `CCustomMaterialOwner` RTTI locates a vtable at client RVA `0xb95300`.
  Its destructor at `0x1de6b0` uses a delete size of **0x18**, not the 0x14
  suggested by the reference Fatality header comment. Its vector starts at +4;
  count is at +0x10 and cached element pointer at +0x14.
- Owner slot 1 (`0x1de6f0`) accesses the vector and takes material/index arguments;
  slot 3 (`0x1ac360`) iterates materials and calls destination slot 1.
- `CCSWeaponVisualsDataProcessor` RTTI locates vtable `0xc0969c`.
  Its deleting destructor at `0x4086d0` uses allocation size **0xc18**.
- A processor method at `0x408e10` calls material-system virtual byte offset
  **0x260** (slot 152 on x86). This is evidence of a material-system dependency,
  not sufficient proof of a callable manager getter signature.
- `CCustomMaterialManager` RTTI locates a vtable at materialsystem RVA `0xa0d64`.

## Still required before calling into the engine

Resolve and validate the item-view constructor/destructor, attribute setters,
paint schema access, generated-material entry point, asynchronous readiness and
custom-material lifetime. Confirm the processor constructor argument structures
and matching model material indices. Sticker materials require their own path;
they cannot be represented by paint-kit parameters alone.

Do not create a fake item view by zeroing a guessed structure, copy vectors with
memcpy, use borrowed materials without refcount ownership, or call these RVAs in
other builds. RTTI is identification evidence, not ABI compatibility proof.

## Current runtime behavior

The inline viewport renders the base model and supports rotation/panning. It does
**not** render selected paint, seed, wear or stickers. No new unverified engine
calls were introduced by this investigation. Runtime verification in CS:GO is
still necessary after implementing the missing interfaces.

## Further call-chain findings

The inspector now uniquely locates these **candidates** in executable sections:

| Candidate | Client RVA |
| --- | --- |
| Generated-material update | `0x719630` |
| Weapon-material generation | `0x719770` |
| Weapon visuals processor constructor | `0x408640` |

The update candidate clears the custom-material owner at `this+0x10`, tests its
count at `this+0x20`, calls static-data lookup (`0x715880`), compares item classes
`tool` / `wearable_item`, and dispatches the weapon branch to `0x719770`. These
operations match the source-level generation flow, but this binary's signature
is **not** the two-argument declaration in the reference source: it returns with
`ret 0x0c` and reads `[ebp+0x10]`. Its weapon branch passes four stack arguments,
including a model handle consumed by the callee. Do not call through the old
reference typedef: that would corrupt the stack.

The processor constructor initializes its base/reference count, writes the RTTI
identified derived vtable, stores a paintable-material pointer at `+0x58`, calls
the compare-data copy routine and initializes the `CustomWeapon` shader. It also
returns with `ret 0x0c`; the apparent third argument must be accounted for even
though the shader string is constant in this build.

The exact unique signatures are included in the JSON report and inspection tool.
They are diagnostic candidates only, not registered as executable runtime hooks.
The next missing link is constructing a valid isolated item view with attributes,
then establishing the actual stack-argument semantics and asynchronous lifetime.

## Experimental live-owner path

An experimental alternative now reuses generated materials of a **matching local
weapon** instead of constructing an item view. Requirements: matching weapon
definition, fallback paint kit, seed and wear; nonempty material vector; inherited
owner vtable functions; exact supplied-client timestamp and the verified owner
copy instruction sequence. The preview checks the actual `SetMDL(const char*)`
slot against the disassembled implementation before passing an owner.

In this binary CMergedMDL's string overload is RVA `0x9aec30`; it delegates to the
handle overload at `0x9aeb60`. At `0x9aebc2` the latter calls owner virtual slot 3
with its own root CMDL (`this+4`) as destination. The previously inspected owner
implementation copies through SetCustomMaterial, retaining references. The
source entity pointer is used synchronously and is not cached. Before rebinding,
only the preview owner's references are cleared via the build-specific owner
clear routine (`0x1acc80`). This is guarded by the same build/slot checks.

The UI reports whether live materials were copied, are unavailable on this build,
or require the matching weapon in game. "Copied" is not a promise that asynchronous
material composition is complete. This path does not generate arbitrary catalog
finishes and does not implement separate sticker projection. It requires an
in-game test and remains protected by the preview exception boundary, which does
not guarantee safety after an engine fault.

## Agent's held weapon

The agent viewport now consumes a separate, copied CT/T finish state. On the
supplied client build it refreshes custom materials in merged child 1 (the held
weapon), not in the root animation model or child 0 (the agent). Default/missing
matching live materials clears preview-owned references. Refresh is throttled to
500 ms except when the selected finish revision changes. No extra merged models
are appended, no entity pointer is cached, and no game entity is changed here.

Additional binary evidence: RVA `0x9af4f0` searches merged model handles using data
at +0x6d4, count +0x6e0 and stride 0x6d0. RVA `0x9af400` creates a child CMDL and
invokes source-owner slot 3 with the child as destination. The updater checks the
supplied-build fingerprint, a vector-access instruction sequence, child count
(two), and destination owner methods before copying. Runtime validation is still
required. A raw pointer guard/SEH cannot make an incompatible ABI safe.

This does not implement arbitrary skin generation, glove mesh replacement, or
separate sticker projection. Gloves need verified bodygroup handling so the
original agent's hand/glove geometry is not rendered on top of the replacement.

## Superseded: unarmed static agent viewport

The latest requested agent preview deliberately has **no held weapon** and no
idle animation playback. Its cache is keyed only by team/agent; hovering weapon,
knife or finish icons cannot append a weapon or switch its pose. The held-weapon
material updater described above was removed from that viewport accordingly.
The runtime looks for a standing unarmed/fists label in the animation MDL and
logs whether it matched; absent such a label it freezes the legacy pose at time
20 rather than playing an intro or choosing sequence zero. Exact hand posture
still requires an in-game check. Game equipment application is unchanged.
