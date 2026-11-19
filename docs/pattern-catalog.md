# Signature catalogue

`utils/patterns/catalog.hpp` holds every signature as plain data, so the portable
tests can validate it without Windows headers. `utils/patterns/patterns.cpp` only
scans a live process and calls `Patterns::Resolve()` for each entry.

Two groups share one `Definition` type:

| Group | Entries | Numeric fallback | Origin |
|---|---|---|---|
| `kLegacySnapshotCatalog` | 58 | yes, `utils/offsets/offsets.hpp` | Hazedumper snapshot `2023-09-05` |
| `kPortedCatalog` | 99 | none | this repository's `main` branch |

`Patterns::Init()` reports `patterns: N definitions, M resolved, K missing` and lists
unresolved names through `Patterns::Missing()`. `offsets::Get()` publishes the
signature value when it resolved and the pinned constant otherwise; the 99 ported
names have no constant, so they simply publish `0` until a signature matches.

## Where the ported entries come from

`main` in this repository is a different project (`opaihook`) with its own
`offsets.cpp`. That file has three regions:

```
shared block            #ifdef LEGACY            #else
(used by every build)   (legacy configuration)  (live configuration)
```

Only the **shared block** was ported. Two reasons, both about correctness rather
than tidiness:

- the `#ifdef LEGACY` block describes a different, older build with different
  signatures - folding it in would put two incompatible addresses for the same
  symbol into one lookup table;
- the `#else` block is the live configuration and does not describe the build this
  base targets.

So 109 shared names were reviewed, 99 of them are new here. The remaining ten were
already present:

| Name in `main` | Already present as |
|---|---|
| `clantag` | `dwSetClanTag` (identical signature) |
| `d3d_device`, `direct_device` | `dwppDirect3DDevice9` (identical signature) |
| `input` | `dwInput` (identical signature) |
| `screen_matrix`, `world_to_screen_matrix` | `dwViewMatrix` (identical signature) |
| `studio_hdr` | `m_pStudioHdr` (identical signature) |
| `find_hud_element`, `set_abs_angles`, `set_abs_origin` | same name, same signature |

One shared name was dropped for a different reason: `view_render` matched
`FF 50 14 E8 ? ? ? ? 5F` minus seven bytes and was only ever dereferenced twice in
`main`, so it does not describe a stable address. `Interfaces::RenderView` already
provides that interface.

## How each entry was translated

`main` stored raw pattern matches and dereferenced them at each call site. Here the
resolution happens once, in `Patterns::Resolve()`, and the result is published as an
RVA (module-relative), matching the existing Hazedumper entries.

| `Definition` shape | Meaning | Example |
|---|---|---|
| no offsets, `extra = 0` | address of the matched code | `setup_bones`, `update_animstate` |
| no offsets, `extra = N` | address inside the function | `return_addr_extrapolation` (`+0x29`) |
| one offset, `relative = true` | read the 32-bit operand, publish it as an RVA | `local`, `client_state`, `game_rules` |
| one offset, `relative = false` | read the 32-bit operand, publish it raw | `studio_hdr_ptr`, `get_bone_merge` (field offsets) |
| `call_target = true` | resolve the rel32 of the matched `E8` | `model_renderable_animating`, `update_merge_cache` |

Operand offsets were taken from the definition in `main` and, where `main` added the
constant at the call site instead, from those call sites - `allow_extrapolation`
(`+1`), `studio_hdr_ptr` (`+2`), `trace_filter` (`+0x3D`),
`trace_filter_skip_entities` (`+3`), `send_move_addr` (`+1`),
`list_leaves_in_box` (`+3`).

`Resolve()` also treats a zero operand, or one that is not inside the module, as
unresolved. Before this change a null global produced a bogus "RVA" by subtracting a
module base from zero; now the entry is reported as missing and the pinned constant
is used where one exists.

## What is NOT guaranteed

- No ported signature has been matched against a real `client.dll`. They are strings
  copied from another project; only running `Patterns::Init()` in the target process
  says whether they hold.
- Duplicate semantics were not invented: two names may point at the same engine
  object (`glow_object_manager` vs `dwGlowObjectManager`), because the signatures
  differ and either may match on a given build.
- Scanning cost grows with the catalogue: 158 patterns now run at startup instead of
  58, each over the executable sections of its module.

## Reference signatures (not from `main`)

`kReferenceCatalog` holds signatures that belong to a single feature and come from
public CS:GO sources instead of this repository's `main` branch. Keeping them apart
makes the provenance obvious and keeps the "ported from `main`" count honest.

| Name | Module | Used by |
|---|---|---|
| `server_rank_reveal_all` | `client.dll` | Misc → Scoreboard → "Reveal ranks" |
| `set_local_player_ready` | `client.dll` | Misc → Other → "Auto-accept" |
| `send_datagram`, `send_datagram_alt` | `engine.dll` | Misc → General → "Ping spike" |
| `server_rank_reveal_all_alt1`, `server_rank_reveal_all_alt2` | `client.dll` | Misc → Scoreboard → "Reveal ranks" |

`server_rank_reveal_all_alt1` and `_alt2` are the older and newer spellings of the same
rank-reveal handler; the feature tries all three and falls back to the
`CS_UM_ServerRankRevealAll` user message when none matches (see `docs/misc-runtime.md`).

`send_datagram` and `send_datagram_alt` are the two `CNetChan::SendDatagram`
signatures this repository's `main` branch carries, one per supported game build; the
feature tries them in order. `CNetChan::ProcessPacket` was already part of the ported
block, so ping spike reuses that entry instead of duplicating it.

`set_local_player_ready` is the signature published by CSGOSimple for the client's
`SetLocalPlayerReady(const char*)`; the prologue reads its single argument from
`[ebp+8]`, which is why it is called as a plain one-argument function
(`features/misc/other/accept.cpp`).

It is resolved through the same `Patterns::Init()` pass and published by
`offsets::Init()` under its own name. A missing entry is not fatal: the feature logs a
warning and stays off (see `docs/misc-runtime.md`).

## Checks

`tests/pattern_catalog_test.cpp` (portable, no Windows headers) verifies:

- all three catalogues have the expected entry counts (58 / 99 / 6) and globally unique
  names;
- every module name is one the scanner can load, every signature token is either two
  uppercase hex digits or `?`/`??`, offsets stay within the 4-slot operand chain, and
  `extra` stays small;
- `Resolve()` arithmetic for all four shapes, including a negative `E8` displacement
  and every failure path (no match, unreadable operand, uninitialised global, no
  module base).

## Reading the runtime report

`Patterns::Init` prints one line with the totals and, when a signature did not match, two
more that say what that actually cost:

- `matched through an alternative spelling: server_rank_reveal_all (via server_rank_reveal_all_alt2)`
  — the catalogue carries the spellings of the same function for different builds; one of
  them matching is the normal case, not a failure.
- `no match, owner decides: direct_present, direct_reset` — nothing covered these; the
  feature that needs one logs its own consequence (e.g. "rank reveal stays off").
- `offsets: pinned build table in use for: send_datagram` — the signature moved but this
  build's pinned offset still resolves the function, so the feature keeps working.
