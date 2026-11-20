# CS:GO clean base

Самостоятельная чистая база в ``. Старый проект из корня удалён; код базы написан заново, а из трёх ссылок использованы только архитектурные идеи и публичные данные.

## Дерево

```text

├── main.cpp                 DllMain, поток Entry и контролируемое завершение
├── context.hpp                единый runtime-контекст: local/weapon/view
├── CMakeLists.txt           x86 DLL для Visual Studio 2022
├── sdk/
│   ├── hooks/               VMT manager и игровые хуки
│   ├── third_party/         Fatality GUI/renderer, MinHook, JSON и лицензии
│   ├── interfaces/          Capture<T>, ключевые Source-интерфейсы и vfunc-обёртки
│   ├── entity/              CBaseEntity, CCSPlayer, CBaseWeapon и netvar accessors
│   ├── datatypes/           CUtlVector/CUtlString и совместимые типы
│   ├── clientclass.hpp        ClientClass
│   ├── recv.hpp               RecvTable/RecvProp
│   ├── datamap.hpp            Source 1 datamap/typedescription
│   ├── netvars.hpp/.cpp       поиск свойств и NETVAR-макрос
│   ├── schema.hpp             общий фасад для RecvTable, datamap и статических полей
│   └── structs.hpp            CUserCmd, glow/animation структуры
├── utils/                   memory scanner, XOR/hash, logging, config, input, events, render
│   ├── math/                Vector, QAngle, Color, VMatrix, математические функции
│   ├── offsets/             CS:GO snapshot-таблицы и runtime-адреса
│   └── patterns/            каталог сигнатур (данные в catalog.hpp)
├── menu/                    меню, табы, контролы, UI-превью и адаптеры
└── features/
    ├── visuals/              player ESP, chams, glow, removals (flash, visual punch),
    │                         misc/ (thirdperson, fov changer, viewmodel fov)
    ├── misc/                 папка на вкладку, файл на логику (см. docs/misc-runtime.md)
    ├── rage/                 ragebot/antiaim интерфейсы-точки расширения
    └── skins/                применение скинов и инвентарь
```

## Слои и ответственность

- **SDK** ничего не знает о функциях чита. Интерфейсы получаются по `CreateInterface`, RecvTables индексируются один раз, свойства сущностей читаются через `NETVAR`.
- **Patterns → offsets → interfaces → netvars → hooks** — порядок загрузки в `main.cpp`. Результаты сканирования live-процесса важнее fallback-констант.
- **`ctx`** — общее состояние для хуков/функций (вместо разрозненных глобальных переменных).
- **`features/`** — независимые модули. `CreateMove` вызывает movement/rage entry points; `PaintTraverse` — visuals/menu. Добавление функции не требует менять SDK или центральный контекст, если ей не нужно новое состояние.
- **Ошибки сигнатур** журналируются. Нет найденной optional-сигнатуры — её поле остаётся нулевым; известные поля могут использовать snapshot fallback. Для другого билда fallback нужно перепроверить.

## Что взято как ориентир

| Источник | Применённые идеи/данные |
|---|---|
| [hazedumper](https://github.com/frk1/hazedumper) | Модель «IDA signature → operand chain → RVA»; перенесён каталог из 58 CS:GO сигнатур и таблицы `csgo.hpp` (snapshot `2023-09-05`, 94 netvar-константы и 58 signature-констант). В базе это собственный scanner/registry в `utils/patterns/` и собственные типизированные fallback-таблицы в `utils/offsets/`. |
| ветка `main` этого репозитория (`opaihook`) | 99 недостающих сигнатур из общего блока `offsets.cpp`; подробности переноса, исключённые и уже имевшиеся имена — в `docs/pattern-catalog.md`. Числовых fallback у них нет. |
| [fatality_source](https://github.com/reversed2603/fatality_source) | Группировка SDK/interfaces/features, разбиение хуков и feature-модулей по файлам, компактные typed-accessors и структура CUserCmd. Реализация здесь новая, без копирования исходников. |
| [VeryElusive/game-hack (Havoc)](https://github.com/VeryElusive/game-hack/tree/main/base) | Слои `context` → `sdk/utils/hooks/features`, единый контекст вместо множества singleton-глобалок, отдельный memory/pattern helper, `Capture<T>` интерфейсов и staged Entry-инициализация. |

## Сборка

Требуется Windows + Visual Studio 2022 (MSVC) и Windows SDK. DLL рассчитана на **Win32/x86**; внешние библиотеки не нужны (меню рисуется через VGUI surface).

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release
```

В Visual Studio также можно открыть `CMakeLists.txt` как CMake-проект. Проверка сборки требует Windows/MSVC; текущая рабочая среда Linux не располагает ими.

## Паттерны и актуальность

В `utils/patterns/catalog.hpp` находятся два каталога: 58 Hazedumper patterns и 99 сигнатур, перенесённых из ветки `main` (общий блок `offsets.cpp`, без `#ifdef LEGACY` и без live-блока). Сканер и разрешение адресов — в `utils/patterns/patterns.cpp`, правила переноса — в `docs/pattern-catalog.md`. `utils/offsets/offsets.hpp` хранит fallback значения из последнего известного Hazedumper snapshot — **5 сентября 2023 года**. Это известная зафиксированная база для CS:GO Legacy, не live-обновляемый источник и не обещание совместимости с любым исполняемым файлом. Перед использованием на конкретной сборке проверяйте успешность сканирования и адреса в отладчике; неподходящие fallback-данные надо обновить.

CS:GO Source 1 использует RecvTables и datamaps; отдельной Source 2 `SchemaSystem` в этой базе нет. `sdk/schema.hpp` даёт единый фасад к этим Source 1-метаданным, чтобы не путать их с CS2-схемой.

## Что уже работает и куда расширять

- Базовые entity/interface/netvar wrappers, IDA-pattern scanner, offset registry, VMT hooks, logging, JSON-настройки и VGUI-отрисовка.
- Player ESP: `features/visuals/player_esp.cpp`.
- Misc: `features/misc/` — папка на группу меню (`general/`, `other/`, `movement/`, `buybot/`), в папке **один `.hpp` на всю папку** и по `.cpp` на каждую логику. Работают radar-reveal, clan tag, reveal enemy chat в формате самой игры (`DispatchUserMessage` + `SayText2`), ping spike (хуки `SendDatagram`/`ProcessPacket`, порт из `main`); в группе OTHER — auto-accept (`EmitSound1` + `SetLocalPlayerReady`) и console filter (экспорты `tier0!ConMsg`/`ConColorMsg`); remove flash и visual punch живут в `features/visuals/removals/`, потому что их чекбоксы — в Removals вкладки Visuals. Scoreboard (ranks/re-weapon/re-money и panorama-скрипт) удалён целиком. В MOVEMENT работают air strafe, bhop и quick stop (порты из `main`), edge jump и edge stop (свой трейс-прогноз) — всё через хук `ClientMode::CreateMove` (слот 24); «No fall damage» и «Instant duck» убраны из меню, потому что клиентских способов их сделать нет. BUY BOT — реальная закупка: одна строка `buy X; buy Y; ...` в порядке `main` (primary → pistol → armor → остальное) через `IEngineClient::ClientCmd_Unrestricted` (слот 114), окно покупки — `m_bFreezePeriod` из `c_game_rules` (байт `+0x20`, сигнатура `game_rules` из `main`), до трёх попыток за раунд. Во вкладке Visuals группа MISC теперь тоже с логикой: thirdperson (камера в `ClientMode::OverrideView`, слот 18, трейс как в `main`, своя модель скрывается в `DrawModelExecute`), fov changer (поля `m_iDefaultFOV`/`m_iFOV`/`m_iFOVStart` на `FRAME_RENDER_START`) и viewmodel fov (хук `GetViewModelFOV`, слот 35). Точка входа — `FrameStageNotify` (и `CreateMove` для движения и buy bot), границы охвата и список нереализованного — `docs/misc-runtime.md`.
- Меню открывается **Insert**; **F1** включает ESP, **F2** — подписи, **F3** — bunnyhop, **F5/F6** — сохранить/загрузить конфиг; **End** выгружает DLL.
- `ragebot.cpp`, `antiaim.cpp`, `glow.cpp`, `world.cpp` сейчас являются намеренными точками расширения, а не готовой реализацией этих функций.

Новая фича обычно состоит из `<feature>.h/.cpp` в подходящей папке и одного вызова из `sdk/hooks/client.cpp` или `sdk/hooks/paint.cpp`. Netvar-поля объявляйте рядом с соответствующим SDK-классом; runtime-адреса получайте через `offsets::Get()` / `Patterns::Get()`, а не хардкодьте в feature-коде.

## Injector (Win32)

Откройте `csgo_base.sln` в Visual Studio 2022 и соберите `Debug|Win32` или `Release|Win32`, либо выполните:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Debug
```

В `build/Debug` находятся `injector.exe` и `csgo_debug.dll`, в `build/Release` — `injector.exe` и `csgo_release.dll`. Инжектор каждой конфигурации ищет соответствующую DLL рядом с собой. Запустите инжектор, затем CS:GO Legacy; окно остаётся открытым до закрытия пользователем. Игра и инжектор должны работать в одной пользовательской сессии и с совместимыми правами. Требуется доступ на запись в эту папку: инжектор дописывает `pastavar_debug.log`, а DLL — `csgo_base.log` рядом с собой; журналы записываются в UTF-8. Debug-сборка дополнительно показывает историю событий в окне.

DLL экспортирует `PastavarGetStatus` и `PastavarCopyStartupError`. Для передачи между процессами используется именованная локальная общая память по PID: инжектор не вызывает адреса экспортов в другом процессе. `Ready` означает, что штатная инициализация и обязательные хуки завершились, но не гарантирует работу всех необязательных функций. Неуспех обязательной инициализации оставляет DLL загруженной для чтения ошибки; после исправления причины перезапустите игру. Ошибки C++ и SEH в потоке инициализации содержат этап и текст или код исключения. При неожиданном завершении игры показывается код выхода, но он не доказывает, что причиной стала DLL; ошибки других потоков и перехваченные внутри хуков исключения не всегда можно диагностировать таким способом. При `LoadLibraryW` с нулевым результатом удалённый `GetLastError` недоступен через один вызов `CreateRemoteThread`, поэтому показывается ограниченное сообщение, а не выдуманный код ошибки.
