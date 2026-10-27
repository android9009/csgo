# Готовые Win32-зависимости

В репозитории хранятся статические COFF-библиотеки x86. Во время сборки никакие зависимости не скачиваются.

| Файл | Содержимое | SHA-256 |
| --- | --- | --- |
| `freetype/win32/Debug/freetype.lib` | FreeType 2.11.1, Debug | `653061b306416363f46c8392f203405e5f3424cc4ebe5b6b36cfdfd3c29a27c4` |
| `freetype/win32/Release/freetype.lib` | FreeType 2.11.1, Release | `ec61f7691ab918cf7d7523f9eee94a390b4bf932dd786dd9bf3d04688e6b117a` |
| `lua/win32/Debug/lua51.lib` | LuaJIT 2.1.0-beta3 с JIT и FFI, Release | `9b578bd46e659725c8534421a8e57dfa1ce8d95acbff3d033643fdc03748cbbb` |

Источник комплекта: [`fuzyak3r/millionware-source`](https://github.com/fuzyak3r/millionware-source/tree/ca70d3fdcf69ca379b349fb2aa79a6e872ca5b6e/dep). Заголовки FreeType в этом репозитории побайтно совпадают с источником библиотек. LuaJIT используется через стабильный публичный Lua 5.1 C API; приватные заголовки и version symbol в SDK не используются.

Одна Release-сборка LuaJIT используется во всех конфигурациях, как и в исходном проекте. Это статическая библиотека, а не import library: `lua51.dll` не нужна. Экспортируются, в частности, `luaL_newstate`, `luaopen_jit`, `luaopen_ffi` и `luaJIT_setmode`.

Архивы содержат CRT-директивы `LIBCMT`/`LIBCMTD`. В `sdk.vcxproj` эти default libraries отключены, чтобы готовые C-объекты использовали CRT основного проекта (`/MD` или `/MDd`) и статическая CRT не добавлялась второй копией.

Лицензии находятся в `freetype/LICENSE.TXT` и `luajit/COPYRIGHT`.
