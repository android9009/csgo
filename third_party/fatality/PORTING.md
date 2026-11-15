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

## Player model preview (incomplete)

The ESP and Chams preview panels have a shared orbit/zoom input state, but no
model renderer or game texture yet. The viewport explicitly reports that it is
unavailable. Experimental ABI declarations live in `sdk/model_preview.hpp`. The Source 1 merged-model, render-context and view bindings,
version checks, render-target lifetime and in-game validation still need to be
implemented before connecting that state to an actual model.
