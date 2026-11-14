# CS:GO clean base

Самостоятельная чистая база в ``. Старый проект из корня удалён; код базы написан заново, а из трёх ссылок использованы только архитектурные идеи и публичные данные.

## Дерево

```text

├── main.cpp                 DllMain, поток Entry и контролируемое завершение
├── context.hpp                единый runtime-контекст: local/weapon/view/prediction
├── CMakeLists.txt           x86 DLL для Visual Studio 2022
├── math/                    Vector, Vector2D, QAngle, Color, VMatrix, функции математики
├── sdk/
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
├── patterns/                58 Hazedumper-сигнатур
├── offsets/                 CS:GO snapshot-таблицы + runtime-результаты паттернов
├── hooks/                   VMT manager; отдельные CreateMove/PaintTraverse/EndScene/WndProc
└── features/
    ├── visuals/              простой player ESP; glow/world — точки расширения
    ├── misc/                 bunnyhop; сюда добавляются movement/misc функции
    ├── rage/                 ragebot/antiaim интерфейсы-точки расширения
    └── menu/                 лёгкое VGUI-меню, hotkeys и настройки
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
| [hazedumper](https://github.com/frk1/hazedumper) | Модель «IDA signature → operand chain → RVA»; перенесён каталог из 58 CS:GO сигнатур и таблицы `csgo.hpp` (snapshot `2023-09-05`, 94 netvar-константы и 58 signature-констант). В базе это собственный scanner/registry в `patterns/` и собственные типизированные fallback-таблицы в `offsets/`. |
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

В `patterns/patterns.cpp` находится каталог из 58 Hazedumper patterns. `offsets/offsets.hpp` хранит fallback значения из последнего известного Hazedumper snapshot — **5 сентября 2023 года**. Это известная зафиксированная база для CS:GO Legacy, не live-обновляемый источник и не обещание совместимости с любым исполняемым файлом. Перед использованием на конкретной сборке проверяйте успешность сканирования и адреса в отладчике; неподходящие fallback-данные надо обновить.

CS:GO Source 1 использует RecvTables и datamaps; отдельной Source 2 `SchemaSystem` в этой базе нет. `sdk/schema.hpp` даёт единый фасад к этим Source 1-метаданным, чтобы не путать их с CS2-схемой.

## Что уже работает и куда расширять

- Базовые entity/interface/netvar wrappers, IDA-pattern scanner, offset registry, VMT hooks, logging, JSON-настройки и VGUI-отрисовка.
- Пример player ESP в `features/visuals/player_esp.cpp`; bunnyhop как пример `CreateMove`-фичи в `features/misc/movement.cpp`.
- Меню открывается **Insert**; **F1** включает ESP, **F2** — подписи, **F3** — bunnyhop, **F5/F6** — сохранить/загрузить конфиг; **End** выгружает DLL.
- `ragebot.cpp`, `antiaim.cpp`, `glow.cpp`, `world.cpp` сейчас являются намеренными точками расширения, а не готовой реализацией этих функций.

Новая фича обычно состоит из `<feature>.h/.cpp` в подходящей папке и одного вызова из `hooks/client.cpp` или `hooks/paint.cpp`. Netvar-поля объявляйте рядом с соответствующим SDK-классом; runtime-адреса получайте через `offsets::Get()` / `Patterns::Get()`, а не хардкодьте в feature-коде.
