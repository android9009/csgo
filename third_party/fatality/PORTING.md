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

The ESP and Chams preview panels share orbit and zoom input. The experimental
Source 1 renderer in `features/visuals/model_preview.cpp` follows Fatality's
merged-model and render-target approach, using candidate addresses resolved once
at startup. It uses a fixed FBI agent and has not been validated in-game; Type,
ESP overlay and Chams preview are not yet connected to the model rendering.
The game-specific ABI, reset/unload handling, correctness and performance still
require runtime testing.
