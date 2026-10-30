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

Результат: `Bins/airflow_shell.dll`. Проект использует C++20, `/utf-8`, `/Zc:char8_t-` и статический MSVC runtime. Отдельный DirectX SDK и D3DX не требуются; используются системные `d3d9.lib` и библиотеки Windows SDK.

## Debug-диагностика

Только сборка `Debug | x86` открывает консоль при запуске runtime и устанавливает обработчик необработанных исключений. Этапы Panorama/D3D9, код исключения, адрес, модуль+offset и регистры пишутся в консоль и `C:\airflow\debug.log`; также создаётся `C:\airflow\crash_last.dmp` для анализа в Visual Studio. В `Release` консоль, файловые debug-логи, exception filter и DbgHelp полностью исключены препроцессором.

## CMake (Windows)

```bat
cmake -S . -B build -A Win32
cmake --build build --config Release
```

Результат CMake-сборки: `build/bin/Release/airflow_shell.dll`.
