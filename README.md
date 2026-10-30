# Airflow Shell

Минимальная пустая Win32 DLL-оболочка. Эта ветка намеренно содержит только базу для дальнейшего развития: без меню, игровых функций, хуков, offsets, конфигов и сторонних библиотек.

`DllMain` возвращает `TRUE` при загрузке и выгрузке. При `DLL_PROCESS_ATTACH` вызывается только `DisableThreadLibraryCalls`; поток инициализации и обращения к CS:GO отсутствуют. DLL может быть загружена обычным `LoadLibrary`-инжектором.

## Visual Studio 2022

1. Откройте `opaihook.sln`.
2. Установите toolset **v143** и Windows SDK.
3. Выберите `Release | x86` или `Debug | x86` и соберите проект.

Результат: `Bins/airflow_shell.dll`. DirectX SDK и остальные внешние зависимости не требуются.

## CMake (Windows)

```bat
cmake -S . -B build -A Win32
cmake --build build --config Release
```

Результат CMake-сборки: `build/bin/Release/airflow_shell.dll`.
