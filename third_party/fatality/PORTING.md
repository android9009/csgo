# Fatality GUI port (work in progress)

Sources and resources originate from the repository's `fix-master.rar`. The
standalone program is retained as `reference_main.cpp` for its original menu
layout and initialization; it is not compiled into the DLL. The archive's
`renderer.lib` is x64 and is deliberately not used in this Win32 project.

The DX9 adapter has been changed to use the Windows SDK's `D3DCompile` in
place of the legacy DirectX SDK's `D3DXCompileShader`. The optional CMake
`BUILD_FATALITY_PORT_CHECK` target compiles the original GUI and DX9 renderer
separately; it does not replace or link into the current menu. A successful
compile is not a runtime validation.

Remaining work: adapt reference_main's window construction and initialization
to the game's device, integrate WndProc and device reset/lifetime handling,
connect configuration, and test in-game before removing the old menu.
