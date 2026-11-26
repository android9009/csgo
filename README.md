# airflow

Internal CS:GO cheat (Win32 / x86 DLL), built with MSVC.

## Requirements

* Visual Studio 2022 (v143 toolset) with the **Desktop development with C++** workload
* Windows SDK (no DirectX SDK needed — `d3d9`, `stb_image`, PolyHook and FreeType are vendored)
* A CS:GO (Legacy) installation for testing

## Building

Open `opaihook.sln` and build one of the Win32 configurations:

| Configuration | Output | Notes |
|---|---|---|
| `Live` | `Bins/csgo_release.dll` | main build |
| `Beta` | `Bins/csgo_release_beta.dll` | crash handler + extra diagnostics |
| `Alpha` | `Bins/csgo_release_alpha.dll` | crash handler + extra diagnostics |
| `Debug` | `Bins/csgo_debug.dll` | console log, `DELETE` unloads the DLL |

or from the command line:

```bat
msbuild opaihook.sln /m /p:Configuration=Live /p:Platform=x86
```

`injector/` contains a small GUI injector (build it the same way, Win32) that waits for
`csgo.exe`, injects the matching DLL and shows the log. The DLL itself also works with any
standard injector — it waits for the `Valve001` window and `serverbrowser.dll` before hooking.

CI (`.github/workflows/msbuild.yml`) rebuilds `Live|Win32` on every push and uploads `Bins/`
as an artifact.

## Layout

```
dllmain.cpp          entry point, crash handler (Alpha/Beta)
globals.cpp/.hpp     c_hacks: init sequence, interface list, precompiled-ish header
hacks.hpp            global state (interfaces, convars, modules) + c_hacks declaration
defines.hpp          vtable indices / flags for both Legacy and Live builds
features/            anti_aim, rage, visuals, skins, misc modules
sdk/                 entity/interface wrappers, netvars, hooks (VMT + detours)
utils/               math, render, memory, offset scanner, xor helpers, config paths
menu/                ImGui menu, custom widgets, config system, embedded fonts/icons
imgui/               ImGui + FreeType + DX9 backend (compiled into the DLL)
Libs/                vendored PolyHook/zydis, FreeType headers, tinyformat
injector/            standalone injector project
```

## Notes

* `LEGACY` selects the deprecated CS:GO branch (`Legacy*` configurations); without it the
  project targets the current client. Most SDK differences live in `defines.hpp`.
* All strings/pointers used by features are obfuscated (`XOR`, `XORN`, `CXOR`, `XORED_PTR`).
* Configs are JSON files, stored in an `airflow\` folder in the current working directory
  of the game (`airflow\sounds` holds custom hit sounds).
