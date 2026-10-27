я зафиксил, меня зовут егор( yamaha );
удачи!

## Сборка

Требуются Visual Studio 2022 с набором C++ (v143), Windows SDK и Git, доступный в PATH. Глобально устанавливать vcpkg или выполнять `vcpkg integrate install` не нужно.

Откройте `millionware.sln` и выберите конфигурацию `beta release` или `stable release` с платформой `Win32`. Перед первой сборкой скрипт `scripts/install-dependencies.cmd` автоматически скачает официальный vcpkg в локальную папку `.vcpkg`, а затем установит FreeType и LuaJIT в `vcpkg_installed`.

Первая установка зависимостей может занять заметное время. Следующие сборки будут только проверять, что зависимости уже актуальны.
