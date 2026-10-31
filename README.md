# Airflow Shell — Darkside GUI

Win32 DLL с оригинальным интерфейсом Darkside, адаптированным для Direct3D 9. В проект перенесены фактические `gui.cpp`, `gui.hpp`, страницы меню, контролы, анимации, модифицированный ImGui, конфиг-браузер, keybind-интерфейс и шрифты Darkside — не самодельная тема по скриншоту.

## Использование

1. Соберите `Release | x86` и загрузите `Bins/airflow_shell.dll` в D3D9-процесс CS:GO.
2. Нажмите **Insert**, чтобы открыть или закрыть меню.
3. Конфиги интерфейса сохраняются в `C:\airflow\configs`, бинды — в `C:\airflow\binds.json`.

Runtime содержит только hook'и `IDirect3DDevice9::Present` и `Reset`, обработку окна и рендер ImGui. Параметры Rage, ESP, Misc и Skins являются локальными UI-настройками: игровая логика Airflow, offsets, entity/weapon interfaces и gameplay-hook'и не восстановлены.

Базовый GUI использует оригинальные шрифты и rounded blur. Дополнительные D3D11/WIC/WinRT-сервисы анимированного shader-фона, GIF и live-данных Spotify в D3D9-backend не перенесены; соответствующие виджеты сохранены, но не получают текстуры или активную медиа-сессию.

## Visual Studio 2022

1. Откройте `airflow.sln`.
2. Установите toolset **v143** и Windows SDK.
3. Выберите `Release | x86` или `Debug | x86` и соберите проект.

Результат: `Bins/airflow_shell.dll`. В конфигурации `Debug | x86` решение также собирает `Bins/airflow_injector.exe`. Проект использует C++20, `/utf-8`, `/Zc:char8_t-` и статический MSVC runtime. Отдельный DirectX SDK и D3DX не требуются; используются системные `d3d9.lib` и библиотеки Windows SDK.

## Debug-инжектор

1. Соберите всё решение в `Debug | x86` и запустите CS:GO. После замены уже загруженной DLL перезапустите игру, чтобы в процессе не осталась старая копия.
2. Запустите `Bins\airflow_injector.exe`. Он найдёт `csgo.exe` и загрузит `airflow_shell.dll` из своей директории через `LoadLibraryW`.
3. При необходимости путь к DLL можно передать явно: `airflow_injector.exe "C:\full\path\airflow_shell.dll"`.
4. Откройте **Visuals**. После вылета нужны `C:\dev\csgo\Bins\debug.log` и `C:\dev\csgo\Bins\crash_last.dmp`.

Инжектор входит только в Debug-конфигурацию решения; Release-сборка DLL по-прежнему не получает консоль или crash-инструменты.

## Debug-диагностика

Только сборка `Debug | x86` открывает консоль при запуске runtime и регистрирует SEH/CRT/SIGABRT-диагностику. Этапы renderer, меню и отдельных вызовов agent preview, код исключения, адрес, модуль+offset и регистры пишутся в консоль и `C:\dev\csgo\Bins\debug.log`; стадия сохраняется отдельно для каждого потока. `C:\dev\csgo\Bins\crash_last.dmp` создаётся уже для первого зарегистрированного `FIRST-CHANCE`, а финальный `CRASH` перезаписывает его финальным исключением; дамп также создаётся для `abort()`. Строка `FIRST-CHANCE` описывает исключение, которое игра ещё может обработать; `CRASH` или `CRT report` важнее для итогового анализа. В `Release` консоль, файловые debug-логи, обработчики и DbgHelp полностью исключены препроцессором.

## CMake (Windows)

```bat
cmake -S . -B build -A Win32
cmake --build build --config Release
```

Результат CMake-сборки: `build/bin/Release/airflow_shell.dll`.
