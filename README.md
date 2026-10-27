я зафиксил, меня зовут егор( yamaha );
удачи!

## Сборка

Требуются Visual Studio 2022 с набором C++ (v143), Windows SDK и vcpkg с интеграцией MSBuild.

Если vcpkg ещё не установлен, один раз выполните в командной строке:

```bat
git clone https://github.com/microsoft/vcpkg
cd vcpkg
.\bootstrap-vcpkg.bat
.\vcpkg integrate install
```

После этого откройте `millionware.sln` и выберите конфигурацию `beta release` или `stable release` с платформой `Win32`. FreeType и LuaJIT нужных конфигураций будут автоматически установлены из `vcpkg.json` при первой сборке. Первая установка зависимостей может занять некоторое время.
