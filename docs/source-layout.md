# Source layout

Application menu code is under `menu/`; game behavior stays under `features/`.
This is a source organization change, not a feature implementation or UI redesign.

```
menu/
  menu.cpp, menu.hpp       menu lifecycle, construction, input/render entry points
  config.hpp              GUI setting declarations
  bridge.cpp              GUI helper adapters
  tabs/                   Rage/Anti-aim, Misc, Visuals, Skins, Legit tab builders and declarations
  controls/               custom popups and control-building helpers
  preview/                model renderer, ESP editor, preview catalog/state/helpers
  bridges/                publish GUI settings to runtime snapshots
  overlays/               GUI-renderer drawing adapters for game overlays
features/
  visuals/                ESP data gathering, chams, glow, shot rendering and removals
                          (the Removals combo box lives in the Visuals tab); the misc/
                          module is removed entirely (docs/misc-runtime.md)
  skins/                  skin/equipment application and local-inventory logic
sdk/
  interfaces/             interface capture (client, engine, cvar, panorama, ...)
  panorama/               IPanoramaUIEngine/UIEngine wrappers and the arity-safe
                          RunScript call (docs/panorama.md)
  entity/                 player, weapon and player-resource netvar accessors
```

`features/visuals/preview_material.hpp` was a misleading name: it renders both
live-game chams and previews. It is now `features/visuals/chams/chams_renderer.hpp`;
its implementation and namespace have not changed. It belongs to game rendering,
not the UI-only model-preview implementation.

`menu/preview/model_preview.cpp` remains C++17 in the DLL target. The menu/tab
integration remains C++20 in `gui_port_check`. CMake explicitly separates
these sources to avoid duplicate compilation/linking after moving out of the
previous third-party directory. The Visual Studio project and filters mirror the
new physical paths.

Third-party Fatality GUI controls, DX9 renderer, resources and upstream reference
files remain in `sdk/third_party/gui/`: moving application integration does not
turn vendored dependencies into application-owned code. `reference_main.cpp` is
an archived upstream reference, is not compiled, and is intentionally unchanged.

SDK declarations remain in `sdk/`. In particular, `model_preview.hpp` and
`model_preview_render.hpp` contain engine/material ABI declarations also used by
live-game rendering. `visual_preview_state.hpp` contains shared settings/layout
transport types consumed by runtime and the editor; moving the actual preview
implementation does not duplicate those shared types or change their ABI.

Portable preview tests now include helpers from `menu/preview/`.

Shared C++ utilities are grouped under the existing `utils/` directory:
`utils/math/`, `utils/offsets/`, and `utils/patterns/`. Inside `utils/patterns/`,
`catalog.hpp` is data only (no Windows headers) so the portable tests can validate
every signature, while `patterns.cpp` performs the live scan and resolution. The old `tools/` analysis scripts were removed at the user's request. There are no forwarding copies at the former
repository-root locations; includes and Visual Studio filters use the new paths.

Hooks now live in `sdk/hooks/`, and vendored dependencies in
`sdk/third_party/`. CMake keeps the latter excluded from the recursive application
source list and adds only the intended dependency sources. Include roots and
Visual Studio filters follow these physical locations.

`tests/` is intentionally retained: the fourteen portable regression programs
check inventory identifiers, preview state, rendering plans and the signature
catalogue arithmetic. They are excluded from DLL compilation, take approximately 64 KiB, and
are useful for catching regressions after file-layout changes. The removed
`tools/` directory contained only the two standalone ABI inspection scripts;
none were required to build or run the DLL.
