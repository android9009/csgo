# Полная инвентаризация Misc

Источник списка: `menu/tabs/tab_misc.cpp`.
Статусы: **подключено** — есть runtime-код, не означает проверку в игре;
**частично / экспериментально** — есть ограничения ниже; **UI** — настройка
сохраняется, но потребителя в runtime сейчас нет. World не затронут.

## Misc / Various

| Пункт и вложенные настройки | Состояние |
|---|---|
| Scoreboard: Reveal ranks, Reveal weapon, Reveal unmatched ranks; Horizontal/Vertical/Weapon/Unmatched scale | UI |
| Logs: Hit, Miss, Buy, Hurt, Naded | Hit/Buy/Hurt/Naded подключены к событиям; Miss пока UI (нет shot acknowledgement/resolver history). Текст использует player index, не имя |
| Hit/Miss details: Damage dealt, Damage aimed, Hitchance, Shot, Backtrack, Safe point; дополнительно Miss reason | Пока не применяются к формату текста; нельзя выводить выдуманные shot/hitchance/backtrack данные |
| Buy log targets: Teammates, Enemy, Local, All | Подключены к item_purchase, All объединяет все стороны |
| Hit/Miss/Buy/Hurt/Naded colors | Цвета работающих категорий применяются к overlay и console |
| Console logs | ConColorMsg из tier0; при отсутствии export — fallback в диагностический DLL log |
| Hitsound; Hit sound enable; Kill sound enable и отдельный Kill sound | Подключено |
| Звуки: None, Bell, CoD, Fatality, Arena switch, Bepis, Phonk, Sparkle, Cock | Подключено |
| Indicators: Manual anti-aim, Freestanding, LC, Double tap, Hide shot, Damage override, Force safety, Headshot only, Bodyaim, Ping | Состояния settings/hotkeys подключены для всех, кроме LC. PING — только реально применённый sequence rewind. Нет выдуманного DT charge; нет визуального 1:1 font parity |
| Clantag | Анимация Pastaware, 30 фаз / 0.3s. Runtime fingerprint + проверка ClanTagChanged/tag/name. Нет бинарной проверки engine.dll локально |
| Reveal enemy chat | UI |
| Ragdoll thrust + Amount | Множитель cl_ragdoll_gravity 1–1000%; исходное значение восстанавливается при выключении/unload |
| Ping spike + Amount | История sequence/reliable-state + SendDatagram rewind/restore. Только pinned engine profile; loopback пропускается; ограничение sv_maxunlag |

## Other

| Пункт | Состояние |
|---|---|
| Bypass SV Pure | UI |
| Bypass SV Cheats | UI |
| Console filter | Подключён: con_filter_enable=1 и con_filter_text=[Pastaware], restore при выключении/unload; пользовательские изменения не перетираются. Требует проверяемый Source 1 x86 ConVar layout |
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
| Edge bugs | Экспериментальный поиск с общим бюджетом 64 трассы, после одной proximity sweep; применение только текущей команды |
| Pixel surf | Экспериментальный поиск: нужны 3 последовательных unsupported floor contacts с half-gravity signature; не просто сравнение velocity |
| Jump bug | Намеренно отсутствует в меню, cfg и runtime; пользователь подтвердил удаление |
| Long jump | Jump–duck assist после отрыва; не speed exploit. Отключается при активных edge/pixel/no-fall для исключения конфликтов |
| Ladder jump | Forward/upward ladder-top dismount: нижняя ladder trace hit + верхняя clear -> IN_JUMP. Сервер контролирует cooldown. Ladder physics solver по-прежнему не симулирует |

## Buybot

| Пункт | Состояние |
|---|---|
| Primary: None, AK47/M4A1, SG-556, AWP, SSG-08, SCAR-20/G3SG1, Negev | Подключено; для CT у SG используется AUG; успешность покупки решает сервер/loadout |
| Secondary: None, Desert Eagle, Dual Berettas, Tec-9/Five-SeveN, P250, CZ75-Auto, R8 Revolver | Раздельные команды deagle/cz75a/revolver; наличие в loadout решает сервер. Старое скрытое значение 5 теперь CZ75-Auto |
| Armor: None, Kevlar, Kevlar + Helmet | Подключено |
| Defuser, Taser, Grenades | Подключено; сторона, buy zone, round/spawn dedup |

Peek assist находится **в Rage → General**, а не во вкладке Misc. Его runtime общий
с Misc: grounded anchor, возврат после weapon_fire (включая выстрел в воздухе) / отпускания movement keys,
торможение с оценкой остановочного пути, distance limit и простая collision steering.
При телепорте >512 units / смене высоты >64 anchor сбрасывается. Добавлены
наземные Glow/Filled режимы и два цвета (ожидание/возврат), renderer получает
отдельный snapshot через try_lock. Полная acknowledged shot history / native
32-command history не готовы. Маркер — DX9 overlay на плоскости anchor, не world material;
на неровном полу нет отдельного mesh с прилеганием к поверхности.

## Что именно добавлено в prediction

`features/misc/prediction.hpp` — отдельная Source-style **read-only симуляция**,
а не wrapper над IPrediction::SetupMove / IGameMovement::ProcessMovement.
У каждого кандидата собственные origin/velocity/ground/duck, в runtime общий лимит 64 трассы после proximity sweep (у тестового API максимум 512).
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


## Следующий запрос: пользовательские Lua-референсы

Просмотрены загруженные `Enemy team chat revealer.lua.txt` и
`Scoreboard Equipment _18.lua (1).txt`.

- Enemy chat: player_say, enemy/team check, cl_mute_enemy_team,
  cl_mute_all_but_friends_and_party, индивидуальный mute через GameStateAPI,
  отложенный вывод 0.2s и подавление дублей player_chat в окне 0.4s,
  локализация T/CT + alive/dead + location. Нужны native usermessage/chat
  receiver и Panorama mute/localization adapters. Не подключено: нельзя честно
  объявить соответствие этому Lua, игнорируя индивидуальный mute и дубли.
  Не может получить сообщения, которые сервер вообще не посылает клиенту.
- Scoreboard Equipment: Panorama CSGOHud -> ScoreboardContainer ->
  id-sb-name__nameicons, привязка к XUID; SVG ItemImage; сортировка оружия,
  выделение active, armor/helmet/defuser/grenades; cleanup при disable;
  update 0.1s. В исходнике cache сравнивает только playerArray: при переносе
  нужно включить scale/color/filters, иначе настройки не обновятся.
  Нужны native script host/panel lifecycle и inventory/event collector.
  Native scoreboard пока не подключён; отдельный DX9-список рядом не выдавался
  за перенос панелей из предоставленного Lua.

Дополнительные просмотренные референсы текущего прохода (Skeet pinned commit
из списка выше): misc/clan_tag.cpp, misc/ping_spike.cpp,
engine/build_misc_engine_bindings.cpp, hud/feature_indicators.cpp,
esp/indicators.cpp. Network ABI сверено также с Fatality sdk/CNetChannel.h
(SendDatagram46, incoming sequence1C/reliable28, IEngineClient channel78).

Ping использует историю вместо простого вычитания ticks из sequence.
Выбирается запись не новее now-delay, обе пары полей восстанавливаются через
__finally после отправки. Пустая/короткая история не подделывается. Лимит
requested <= max(0,sv_maxunlag-current latency). На другом engine timestamp /
SizeOfImage (не 64EE7E86 / 039C0000) сетевой hook не устанавливается.
Смена канала сбрасывает историю. В игре и при reconnect/unload НЕ проверялось.

Индикаторы берут текущие effective values после обработки GUI hotkeys.
Текущий Rage General использует profile key 0; reference-only map 512 не читается.
DT/OSAA отражают включение настройки, НЕ наличие работающего exploit/заряда.
FS/Manual требуют соответствующих состояний AA. LC остаётся без реального
поставщика состояния. Шрифт/метрики оригинального Skeet в этот проход не перенесены.

Clantag восстанавливает сохранённый m_szClan через player resource; при disconnect
не пытается отправить старый tag в отсутствующий channel. Fingerprint не совпал —
callback не вызывается. Clock упрощён до wall-clock 0.3s, без native latency correction.

Остались без подключения: native scoreboard (включая ranks/unmatched/scales),
enemy chat, Console filter, SV Pure/Cheats bypass, LC и полная engine prediction.
Все меню-логи намеренно отложены. Обещания «всё работает в игре» нет.


## Аудит 2026-11-14 — выполненная часть и открытые блокеры

Сверены origin/main `154ded7`, Skeet prediction command/player_simulation/entity_update
и Fatality SDK. В main есть SetupMove/ProcessMovement/FinishMove, но также
build-specific current-command offsets 0x3348/0x3298. Они не перенесены вслепую.

Исправлено в этом проходе:
- отсутствовавшая публикация log types/console/buy targets, регистрация item_purchase;
- событийные hit/hurt/grenade/purchase сообщения (8 последних, 5 секунд),
  Fatality overlay остаётся видимым после смерти, цвет AARRGGBB декодируется правильно;
- кэш ConVar pointers отдельно для каждого вызывающего потока, значения остаются live;
- Settings snapshots с try_lock и предыдущей полной копией при contention,
  atomics для alive/weapon/ping вместо общего mutex; publisher при contention
  повторяет попытку на следующем UI frame. Message mutex отдельный,
  renderer использует try_lock + предыдущий snapshot, сетевой rewind тоже try_lock;
- вместо unconditional 512-sweep route search: active-setting gate, proximity sweep,
  shared 64-sweep budget, отказ от неполностью оценённого поиска;
- CZ75-Auto и R8 отдельными вариантами buybot, тесты mapping/budget/snapshot consistency.

Не завершено (не считать выполнением полного запроса): настоящий IPrediction wrapper,
MoveHelper/MD5/random seed/prediction_player/current cmd lifecycle, prediction compression
RunCommand/PostNetworkDataReceived, pre/predict/post pipeline,
полная WASD/wall/Peek переработка, miss logs/shot details, native scoreboard/enemy chat,
LC, SV bypass. Ping timestamp gate **сохранён**: нет проверенного
универсального channel layout; просто убрать gate означало бы писать в неизвестный объект.
Прочие UI-only пункты отмечены в таблицах. Поэтому «каждый toggle работает» пока неверно.

Локально нет Windows SDK/MSVC и игрового процесса. Portable-тесты не валидируют ABI,
вызовы событий, in-game console/renderer, серверную физику или отсутствие prediction drift.


## Продолжение после уточнения пользователя

Jump bug не возвращать. Логи в этом проходе не менялись и дальнейшей доработки
по последней просьбе не требуют.

Дополнительно исправлены:
- Quick stop: вместо фиктивного 0.001 counter-movement рассчитывается сила
  по оставшейся после friction скорости и live accelerate/interval;
- WASD-strafe: fallback на IN_FORWARD/BACK/MOVELEFT/MOVERIGHT при нулевых analog
  значениях, Easy strafe учитывает выбранное направление WASD, не только view;
- Peek: выстрел в воздухе latch до проверки ground, ранний сброс старого anchor,
  физически мотивированный braking gate перед пересечением точки;
- наземный Peek overlay: 48 сегментов, Filled/Glow, цвета ожидания/возврата,
  independent nonblocking snapshot с view matrix; near-plane сегменты пропускаются;
- Console filter: сохранение исходных enable/text, восстановление только своих
  значений; callbacks/flags ConVar не патчатся. GetString не является virtual:
  используется проверяемый x86 ConVar parent/string layout, не неверный vfunc 11.

Полный engine prediction/compression pipeline и оставшиеся UI-only адаптеры из
таблицы по-прежнему не реализованы. Из этого прохода не следует, что весь Misc
готов или проверен в игре. Portable-тесты дополнены для airborne shot, braking,
teleport/vertical reset, WASD Easy strafe, low-speed quick stop и marker snapshot.
