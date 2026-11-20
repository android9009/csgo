# Build notes (MSVC x86, `/std:c++20`)

Constraints that are not style preferences but hard requirements of the toolchain or of
MSBuild. Everything below was hit by a real `Release|Win32` build of `csgo_base`.

## `__try` cannot live next to objects with destructors (C2712)

`error C2712: cannot use __try in functions that require object unwinding`

MSVC refuses `__try`/`__except` in a function that also has to run destructors on the way
out - any local `std::string`, `std::vector`, `std::function`, `CUtlString`, or a member
initialised by value counts. Notes:

- The handler must sit in a leaf helper whose locals are pointers and PODs. The object-owning
  function then calls that helper. See `Misc::Buybot::Detail::InGame`,
  `LocalAlive`, `ExecuteCommand` in `features/misc/buybot/buybot.cpp`.
- `noexcept` on the helper is fine and documents that the handler never propagates.
- `GetExceptionCode()` is only valid inside the handler; once the call is wrapped the helper
  has to report failure through its return value ("false = threw") and the caller logs once.
- Functions whose hit-by-a-wild-pointer risk is real and whose locals are PODs may keep
  `__try` inline (`sdk/hooks/createmove.cpp`, `sdk/hooks/framestage.cpp`,
  `features/misc/general/ping.cpp`).

## One definition per engine type (C2011)

`error C2011: 'CUserCmd': class type redefinition`

`sdk/structs.hpp` used to declare a flat `struct CUserCmd` next to the authoritative
`class CUserCmd` in `sdk/datatypes/c_user_cmd.hpp`. The game's command object has a virtual
destructor, so the class (vtable pointer from the virtual destructor at offset 0) is the only
correct layout; the duplicate is gone and `structs.hpp` includes `c_user_cmd.hpp` instead.
Forward declarations must use the same key: `class CUserCmd;` (`sdk/hooks/hooks.hpp`),
otherwise MSVC emits C4099.

## One source file name per project (MSB8027)

`warning MSB8027: Two or more files with the name of misc.cpp will produce outputs to the
same location`

MSBuild names objects after the source file, so `features/misc/misc.cpp` and
`features/visuals/misc/misc.cpp` both produced `misc.obj` in the same intermediate
directory and one of them could be linked into the wrong place. The visuals one is now
`features/visuals/misc/publish.cpp` (it publishes menu settings to the render/game thread).
`csgo_base.vcxproj` and `.filters` are updated; CMake globs the tree and needs no change.

## What the Linux checks can and cannot see

`g++ -std=c++20 -I.` on the host builds and runs the 22 headers-only tests in `tests/`, which
covers the layout and rule logic (`static_assert`s, `ModelTransparency` ledger, movement
writes). It cannot see MSVC-only diagnostics: C2712, C2011 between two headers that are only
included together in a Windows translation unit, and every MSB8xxx project warning. A
syntax pass that stubs `windows.h`/`d3d9.h` and rewrites `__try` into a plain block does
parse the file, but it silently accepts C2712 patterns - the leaf-helper rule above has to be
checked by reading the function, not by compiling it on Linux.
