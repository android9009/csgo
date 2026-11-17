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
  misc/                   movement, prediction, events, buybot and other game logic
  visuals/                ESP data gathering, chams, glow and shot rendering logic
  skins/                  skin/equipment application and local-inventory logic
```

`features/visuals/preview_material.hpp` was a misleading name: it renders both
live-game chams and previews. It is now `features/visuals/material_renderer.hpp`;
its implementation and namespace have not changed. It belongs to game rendering,
not the UI-only model-preview implementation.

`menu/preview/model_preview.cpp` remains C++17 in the DLL target. The menu/tab
integration remains C++20 in `fatality_port_check`. CMake explicitly separates
these sources to avoid duplicate compilation/linking after moving out of the
previous third-party directory. The Visual Studio project and filters mirror the
new physical paths.

Third-party Fatality GUI controls, DX9 renderer, resources and upstream reference
files remain in `third_party/fatality/`: moving application integration does not
turn vendored dependencies into application-owned code. `reference_main.cpp` is
an archived upstream reference, is not compiled, and is intentionally unchanged.

SDK declarations remain in `sdk/`. In particular, `model_preview.hpp` and
`model_preview_render.hpp` contain engine/material ABI declarations also used by
live-game rendering. `visual_preview_state.hpp` contains shared settings/layout
transport types consumed by runtime and the editor; moving the actual preview
implementation does not duplicate those shared types or change their ABI.

Portable preview tests now include helpers from `menu/preview/`.
