# Interface verification against the supplied CS:GO Legacy binaries

Everything below comes from static analysis of the DLLs the user supplied
(`materialsystem.dll`, `studiorender.dll`, `client.dll`, plus their peers) with
`pefile` + `capstone`. No file offsets or RVAs are recorded here: a build's
addresses are worthless on any other build, so this document keeps only what is
portable — **interface version, vtable slot index, call signature and the shape of
the code that proves it**. The repository's slot constants and the checks in
`Interfaces::Setup` are what carry this into the build; nothing address-based is
compiled into the cheat.

## Method

1. `pe.get_memory_mapped_image()` for RVA-indexed reads (never a raw file offset).
2. Locate the interface: version string (`VMaterialSystem080`, `VStudioRender026`, …)
   → `InterfaceReg` chain → `createFn` (`mov eax, <singleton>; ret`).
3. Recover the vtable: the singleton's first dword is zero in the file, so the
   vtable comes from the `mov dword ptr [obj], imm32` in the object's init/ctor.
4. Confirm each slot by **call-site shape**, not by position: `call dword ptr
   [eax+0xNN]` means slot `0xNN/4`, and the other slots either store into the
   object (setters), reload a global (getters) or return a constant.
5. Strings act as anchors: e.g. `CMaterial::GetNumAnimationFrames` is referenced
   from exactly one function, and that function sits in `CMaterial`'s vtable at a
   known index.

## `client.dll` — `IClientMode` (ClientModeShared)

Vtable recovered from the constructor that does `mov [esi], <vtable>` and then
`mov [esi+4]`, `mov [esi+0x30]` (secondary bases); the same object is the one
reached through the client's `vfunc 10 → +5` double dereference.

| slot | method | evidence |
|---|---|---|
| 18 | `OverrideView(CViewSetup*)` | one `CViewSetup*` argument (`[ebp+8]`), writes origin `+0xB8/+0xBC/+0xC0` and angles `+0xC4/+0xC8/+0xCC`, self-calls `vfunc +0x544` |
| 24 | `CreateMove(float, CUserCmd*)` | sample-time float + command pointer, self-calls `vfunc +0x430` |
| 35 | `GetViewModelFOV()` | no arguments, returns a float |

`SlotCount` for the primary vtable is 44. `FrameStageNotify`, `DispatchUserMessage`
and `DoPostScreenEffects` were **not** disassembled individually; the repo keeps
using the numbers it already had for those, and `Interfaces::Setup` rejects a
`ClientMode` whose vtable is not inside `client.dll`.

### `CViewSetup` layout

Confirmed by `ClientModeShared::OverrideView`:

| offset | field |
|---|---|
| `+0xB8` | `vecOrigin` (`+0xBC`, `+0xC0` = y, z; 7 accesses each) |
| `+0xC4` | `angView` (pitch/yaw/roll, `+0xC8`, `+0xCC`) |

`fov` at `+0xB0` and `fovViewmodel` at `+0xB4` follow from the engine's field order
(`fov`, `fovViewmodel`, `origin`, `angles`) and are **inferred, not observed** —
`OverrideView` itself never touches them. Worth knowing if a fov problem is ever
blamed on this struct.

## `client.dll` — `C_CSGameRules`

`m_bFreezePeriod` is **`+0x20`** in this build. It is not guessed: the game builds
its receive tables at runtime, and the initializer for that property is

```
push 0 / push 0 / push 1 / push 0x20
mov  edx, offset "m_bFreezePeriod"
mov  ecx, offset <RecvProp>
call <RecvProp ctor>
```

so the `push 0x20` immediately before the name is the property offset. This is the
same number the buy bot already used (`is_freeze_time = game_rules + 0x20`), now
confirmed. The rules object itself still comes from the `A1 <global> … 80 B8 ??`
signature; note that the byte it tests is *not* the freeze flag (`+0xCA0`), which is
why the pattern is used only to find the object.

## `materialsystem.dll` — `IMaterialSystem` (080)

Vtable has ≥130 slots; the material-creation block (76–85) and the render-context
block (108–118) are contiguous.

| slot | method | evidence |
|---|---|---|
| 83 | `CreateMaterial(name, KeyValues*)` | SEH frame, self-call `[singleton]+0x1D8`, passes `[ebp+0xc]` (name) and a VMT buffer |
| 84 | `FindMaterial(name, group, complain, prefix)` | reads `[ebp+8]` as the name, self-call `[singleton]+0x34` |
| 115 | `GetRenderContext()` | returns `[object+0x2308]` with a lazy-create fallback |

The singleton is referenced literally (`ecx = <global>`), never as `this`, which is
why `Interfaces::MaterialSystem` may call `CreateMaterial`/`FindMaterial` without
worrying about which copy of the pointer it holds.

## `materialsystem.dll` — `IMaterial` / `CMaterial`

`CMaterial`'s vtable was found from the `CMaterial::GetNumAnimationFrames` string
(slot 61), 108 code slots.

| slot | method | evidence |
|---|---|---|
| 0 | `GetName()` | `add ecx,0x18` then tail-jump into the var container |
| 1 | `GetTextureGroupName()` | `add ecx,0x1a` variant of the same thunk |
| 4 | error/flag predicate | `movzx` of the flag byte at `+0x20` |
| 11 | `FindVar(name, found, complain)` | SEH frame; var-container lookup |
| 12 / 13 | `IncrementReferenceCount` / `DecrementReferenceCount` | `lock inc` / `lock dec` on `+0x1c` |
| 27 | `AlphaModulate(float)` | writes `$alpha`, guarded by the flag byte at `+0x20` |
| 28 | `ColorModulate(r,g,b)` | same guard, three floats |
| 29 / 30 | `SetMaterialVarFlag` / `GetMaterialVarFlag` | flag byte at `+0x20`, then `+0x24 → [0] → [8]` binder |
| 42 | `IsErrorMaterial()` | `cmp [global], ecx; sete al` — the comparison against the error material |
| 44 / 45 | `GetAlphaModulation` / `GetColorModulation` | same flag guard, read `+0x20` |
| 49 | `GetShaderName()` | `[ecx+0x14] → vtable[0]`, default object fallback |

All of `sdk/material.hpp`'s assertions hold for this build.

## `studiorender.dll` — `IStudioRender` (026)

Singleton from `VStudioRender026`; vtable of 53 code slots. Slots 33 and 34 were the
open question and are now settled:

| slot | method | evidence |
|---|---|---|
| **33** | `ForcedMaterialOverride(IMaterial*, int type, int overrides)` | `ret 0xc`; `type == 3` pushes onto a 4-deep stack (`[this+0x274]` depth, `[this+0x250][n]` material, `[this+0x264][n]` overrides, `[this+0x260]` type) and warns when full; any other type replaces entry 0 and stores `overrides = -1`, `depth = 0` |
| **34** | `IsForcedMaterialOverride()` | returns true when `[this+0x250] != 0` or the stored type is 2 or 4 |
| 20 / 21 | ambient-cube colour set / get | 3 floats each, `w = 1.0f` written per vector |
| 22 | local `LightDesc_t` array | copies an array of light descriptors |
| 28 | single-float context setter | `movss [this+0x24c], xmm0` |

So `features/visuals/material_renderer.hpp` calling slot 33 with
`(material, 0, -1)` is correct on this build: type 0 takes the replace path, and
`-1` means "every submodel". Passing `(nullptr, 0, -1)` at the end of a pass
restores the engine's own material, exactly as `Restore()` assumes.

Slot 28 writes one float at `+0x24c` next to the forced-material block; it looks
like a per-context modulation setter, but its exact meaning was not established, so
nothing in the cheat calls it.

## `engine.dll` — `IVEngineClient` (014) and `IVModelInfo`

Same method: `VEngineClient014` -> `InterfaceReg` -> `createFn` (`mov eax, <singleton>; ret`)
-> vtable from the `mov dword ptr [<singleton>], imm32` store in the object's init.

| slot | method | evidence |
|---|---|---|
| 5 | `GetScreenSize(int&, int&)` | two out-pointers, forwards to the surface |
| 8 | `GetPlayerInfo(int, player_info_t*)` | three arguments, indexes the player array |
| 12 | `GetLocalPlayer()` | reads the client state global, calls its vfunc `+0x14`, indexes the slot array |
| 18 / 19 | `GetViewAngles` / `SetViewAngles` | three floats through an out-pointer / an aligned copy in |
| 20 | `GetMaxClients()` | `mov eax, [clientstate+0x388]; ret` |
| 26 | `IsInGame()` | `cmp [clientstate+0x108], 6; sete al` — signon state `Full` |
| 27 | `IsConnected()` | `cmp [clientstate+0x108], 2; setge al` |
| 78 | `GetNetChannelInfo()` | `mov eax, [clientstate+0x9c]; ret` |
| 108 | `ClientCmd(const char*)` | one argument, `Cbuf_AddText` then `Cbuf_Execute` |
| **114** | `ClientCmd_Unrestricted(const char*, const char*)` | **two arguments (`ret 8`)**, ends in `Cbuf_AddText` with the flag taken from the second argument |

So `kClientCmdUnrestrictedSlot = 114` is right for this build, and the wrapper's
`(const char*, const char*)` signature matches the callee, not just the reference
header. The same walk confirms the `[clientstate+0x388]`/`[+0x108]`/`[+0x9c]` shape
the context code already assumes.

`IVModelInfo` could **not** be pinned this way: `VModelInfoClient004`'s create
function hands back an object whose vtable is one "not implemented" stub repeated for
nearly every slot (only the destructor and five high slots differ), so
`GetStudioModel` and `GetModelMaterials` cannot be proven from this file. The
repo's slot 32 for `GetStudioModel` is the index that already works in game, and it
matches the published `IVModelInfo` order that also places `GetModelMaterials` at
19; neither is used by the transparency path (model materials are resolved from the
studio header texture names), so nothing new depends on them.

Note: this `engine.dll` carries timestamp `0x64ee7e86`, while the supplied
`client.dll` carries `0x65272025`, i.e. they are not from the same build date. The
slot map above is self-consistent (all the classic client-state shapes agree), but
if a future `engine.dll` is supplied, re-running the same walk is cheap.

## `shaderapidx9.dll` — `CShaderAPIDx8`

`CShaderAPIDx8` implements ~367 virtuals (shader API + device + device manager +
shadow + …). Two things were needed to judge Fatality's prop-transparency hook:

| item | evidence |
|---|---|
| bound material at **`this + 0x2FA4`** | a one-instruction getter (`mov eax, [ecx+0x2FA4]; ret`) and a setter that stores into the same displacement; this is exactly the field Fatality's `shader_api_draw_mesh` reads |
| **`DrawMesh`**, 6 arguments (`ret 0x18`) | it loads `[this+0x2FA4]`, takes the material's shader name and compares it against `VertexLitGeneric`, `LightmappedGeneric` and `UnlitGeneric`, then forwards all six arguments to one of three shader-specific inner paths (`ecx = this - 0x25c`), skipping entirely when the device is in its 2D mode (`[device+0x4b]`). Slot 291 of the 367-slot vtable |

That is a genuine match for Fatality's hook signature, so the entry point is
available. It is **not** hooked here, for one reason: writing the per-instance alpha
the way Fatality does needs `MeshInstanceData_t`'s layout, and nothing in the module
proves it (the only float reachable from the instance pointer that the inner paths
touch is `+0x44`, which is compared against a constant rather than used as colour).
Modulating the material itself gets the same picture — see
`features/visuals/model_transparency.hpp` and `docs/chams-rendering.md`.

## What is still unverified

- `IVModelInfo` slots (see above) — not provable from the supplied `engine.dll`.
- `IClientMode` 37/38/44 (`FrameStageNotify`, `DispatchUserMessage`,
  `DoPostScreenEffects`) — untouched, the repo keeps the reference numbers.
- `CInput`: `client.dll` loads the interface pointer from one global and calls its
  vfuncs through it, which confirms the "global object, vtable at `+0`" model the
  third-person code relies on, but not the camera field offsets.
- Anything inside `shaderapidx9.dll` (e.g. `CShaderAPIDx8::DrawMesh`, which is where
  Fatality writes per-mesh alpha) — the module was not supplied.

## Consequences in the repo

- `kClientModeCreateMoveSlot = 24`, `kClientModeOverrideViewSlot = 18`,
  `kClientModeViewModelFovSlot = 35` — verified.
- `IMaterial`/`IMaterialVar` indices in `sdk/material.hpp` — verified.
- `IMaterialSystem` 83/84/115 — verified.
- `IStudioRender` 33 for `ForcedMaterialOverride` — verified; 34 is the query.
- New `features/visuals/model_transparency.hpp` uses only the verified material
  slots: it walks the model's materials through the studio header texture names
  (`FindMaterial(..., "Model textures")`), writes `$alpha` and the translucent
  flag, draws, and restores. No override, so chams keep precedence.
