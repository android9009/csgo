# Полная инвентаризация Misc

Источник списка: `third_party/fatality/extra/tab_misc.cpp`.
Статусы: **подключено** — есть runtime-код, не означает проверку в игре;
**частично / экспериментально** — есть ограничения ниже; **UI** — настройка
сохраняется, но потребителя в runtime сейчас нет. World не затронут.

## Misc / Various

| Пункт и вложенные настройки | Состояние |
|---|---|
| Scoreboard: Reveal ranks, Reveal weapon, Reveal unmatched ranks; Horizontal/Vertical/Weapon/Unmatched scale | UI |
| Logs: Hit, Miss, Buy, Hurt, Naded | Hit/Buy/Hurt/Naded подключены; Miss — UI |
| Hit/Miss details: Damage dealt, Damage aimed, Hitchance, Shot, Backtrack, Safe point; дополнительно Miss reason | Только Damage dealt в Hit; остальные UI, нет shot/resolver attribution |
| Buy log targets: Teammates, Enemy, Local, All | Подключено |
| Hit/Miss/Buy/Hurt/Naded colors | Цвета подключённых категорий работают; Miss пока нет |
| Console logs | Подключено, echo с очисткой строки |
| Hitsound; Hit sound enable; Kill sound enable и отдельный Kill sound | Подключено |
| Звуки: None, Bell, CoD, Fatality, Arena switch, Bepis, Phonk, Sparkle, Cock | Подключено |
| Indicators: Manual anti-aim, Freestanding, LC, Double tap, Hide shot, Damage override, Force safety, Headshot only, Bodyaim, Ping | UI; не рисуются фиктивные exploit/antiaim состояния |
| Clantag | UI |
| Reveal enemy chat | UI |
| Ragdoll thrust + Amount | UI |
| Ping spike + Amount | UI; нет истории netchannel sequence/reliable-state |

## Other

| Пункт | Состояние |
|---|---|
| Bypass SV Pure | UI |
| Bypass SV Cheats | UI |
| Console filter | UI (не путать с работающим Console logs) |
| Auto-accept | Подключено экспериментально: проверенный callback client.dll, Panorama ABI из референса; match-found не тестировался |
| Radar | Подключено, запись spotted; восстановление при выключении |
| DPI scale: Automatic, 50%, 75%, 100%, 125%, 150%, 200% | Подключено к меню |

## Movement

| Пункт | Состояние |
|---|---|
| Air strafe: Viewangles, Movement keys, Easy strafe, Jump release; Smoothing, Avoid walls | Подключено частично; не весь native prediction/weapon solver Skeet |
| Bhop | Подключено, пропуск ladder/noclip/water, учёт server autobhop |
| No fall damage | Экспериментальный landing assist: duck/jump timing; не гарантия нулевого серверного урона |
| Instant duck | Подключено через IN_BULLRUSH, зависит от сервера |
| Edge jump | Экспериментально, теперь использует многoэтапную изолированную симуляцию одного тика вместо linear velocity hull check |
| Edge stop | Экспериментальный counterstrafe перед прогнозируемым сходом с края |
| Quick stop | Подключён контрстрейф; live friction/acceleration, пока без полного weapon/surface slowdown |

## Movement extra

| Пункт | Состояние |
|---|---|
| Edge bugs | Экспериментальный поиск 8 вариантов × до 8 тиков; применение только текущей команды |
| Pixel surf | Экспериментальный поиск: нужны 3 последовательных unsupported floor contacts с half-gravity signature; не просто сравнение velocity |
| Jump bug | UI; duck/unduck origin transitions и reprediction ещё не доведены |
| Long jump | UI |
| Ladder jump | UI; ladder physics в новом solver не симулируется |

## Buybot

| Пункт | Состояние |
|---|---|
| Primary: None, AK47/M4A1, SG-556, AWP, SSG-08, SCAR-20/G3SG1, Negev | Подключено; для CT у SG используется AUG; успешность покупки решает сервер/loadout |
| Secondary: None, Deagle/Revolver, Dual Berettas, Tec-9/Five-SeveN, P250 | Подключено, команда buy deagle использует доступный loadout slot |
| Armor: None, Kevlar, Kevlar + Helmet | Подключено |
| Defuser, Taser, Grenades | Подключено; сторона, buy zone, round/spawn dedup |

Peek assist находится **в Rage → General**, а не во вкладке Misc. Его runtime общий
с Misc: grounded anchor, возврат после weapon_fire / отпускания movement keys,
торможение, distance limit и простая collision steering. Полная acknowledged
shot history / native 32-command history / визуальные режимы не готовы.

## Что именно добавлено в prediction

`features/misc/prediction.hpp` — отдельная Source-style **read-only симуляция**,
а не wrapper над IPrediction::SetupMove / IGameMovement::ProcessMovement.
У каждого кандидата собственные origin/velocity/ground/duck, общий лимит 512 трасс.
Есть half-gravity до/после движения, friction/acceleration, air speed cap 30,
до четырёх collision bumps, до пяти clipping planes, движение по crease,
ground categorization, проверка stand-up clearance и ограниченный duck blend.
Используются live gravity/airaccelerate/jump impulse и интервал тика.
Ни игрок, ни Globals, random seed, MoveHelper host или prediction history
не переписываются для перебора. На исчерпании бюджета/invalid/solid — отказ
от кандидата. Следующий реальный тик всегда начинается с нового snapshot.

**Это ещё НЕ полноценная engine prediction и НЕ обещание «нормально 1:1».**
Не воспроизведены stepping/лестницы/вода, moving platforms/base velocity,
точные duck origin transitions, stamina, weapon slowdown, surface physics,
динамическое движение других entities, command prediction history/reprediction.
Поэтому Edge bug / Pixel surf обозначены experimental и в tooltip, и здесь.
Настоящий повторяемый engine replay требует проверенного save/restore всего
prediction state, а не только origin/velocity. Вызвать ProcessMovement многократно
без такого восстановления означало бы двигать реального игрока, повторять
side effects и портить prediction; такой опасный shortcut не добавлен.

## Референсы, реально просмотренные

1. [Valve movement source](https://github.com/perilouswithadollarsign/cstrike15_src/blob/f82112a2388b841d72cb62ca48ab1846dfcc11c8/game/shared/gamemovement.cpp)
   — StartGravity, FinishGravity, AirAccelerate, TryPlayerMove, ClipVelocity,
   CategorizePosition. Проверен commit `f82112a2388b841d72cb62ca48ab1846dfcc11c8`.
   Это опубликованное стороннее зеркало, не подтверждение лицензии на копирование;
   в проект добавлена собственная компактная адаптация алгоритмов, не файл целиком.
2. [Skeet-reference simulation](https://github.com/makavtoJJ1/Skeet-hook-source/tree/1bab8213bd866f4c366a2071a09b27d6ff76820e/src/movement)
   — simulation_tick.cpp, simulation_collision.cpp, simulation_step.cpp,
   simulation_velocity.cpp, edge_jump.cpp. Подтверждает необходимость нескольких
   стадий симуляции, planes/crease clipping и post-prediction ground transition.
   Подлинность как оригинального Skeet не установлена; LICENSE не найден.
3. [Fatality autostrafer](https://github.com/reversed2603/fatality_source/blob/dee7327e3b55c03ee93e9eaa0c4dcada64e49110/internal_hvh/features/autostrafer.cpp)
   — входные режимы и зависимость movement от prediction. LICENSE не найден.
4. [Пример engine wrapper, просмотрен, НЕ импортирован](https://github.com/asu29/cumhacck_csgo/blob/ba74797bbb81c7ece4ae2ca6f079bd61cc9ebdd8/core/features/misc/engine_prediction.cpp)
   — SetupMove/ProcessMovement/FinishMove и Globals/seed/host lifecycle. Не подходит
   как готовый multi-candidate replay: показанного восстановления Globals/seed
   недостаточно для rollback entity state. Непроверенные сигнатуры/обфускация оттуда
   не переносились.

## Проверка в игре — ещё требуется

Local server, 64/128 tick: ровный пол, край, узкий lip, surf map, сход со ступеней,
duck/unduck под потолком, смерть/respawn/teleport, ladder/water exclusions;
проверить отсутствие изменения aim и prediction drift при выключенных assists.
Сравнивать предсказанные маршруты с фактическим следующим тиком. До этого статуса
«проверено в игре» ни у нового solver, ни у новых assists нет.
