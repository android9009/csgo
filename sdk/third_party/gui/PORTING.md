# Fatality GUI integration

The GUI controls, resources, configuration declarations and standalone demo
layout originate from `fix-master.rar`. `reference_main.cpp` retains the
standalone program for comparison; `port.cpp` adapts its window construction,
initialization, input and rendering for the game's D3D9 device.

The archive's `renderer.lib` is x64 and is not used. The Win32 renderer is
built from source; its shader compilation uses Windows SDK `D3DCompile` rather
than legacy `D3DXCompileShader`. The D3D9 Reset hook releases and recreates
resources. Runtime behavior and appearance in CS:GO still require testing.

The Rage page is based on the fix-master demo and has a reorganized Aimbot layout. Its newly introduced controls store GUI values and hotkeys only; they do not implement rage features. Legit, Visuals, Misc and Skins
page layouts come from the subsequently supplied Fatality menu sources. The Lua
page is empty in those sources. Menu values are not connected to this project's feature or config system.

## Player model preview (experimental)

The ESP and Chams panels share a fixed-distance, horizontal-orbit preview.
Local uses the live agent; Team/Enemy resolve their side from the local team.
The skinchanger has separate full-body framing. Chams appearance, sample ESP
and draggable labels/bars are now connected to the preview; world item labels
use copied game-thread snapshots. See `docs/visual-preview-status.md` for exact
coverage, performance strategy, remaining UI-only controls and runtime checks.
Compilation is not proof of game ABI compatibility or frame-time behavior.
