# Airflow Recode

Основной проект репозитория — **Airflow**, Win32 DLL для CS:GO. Старый Skeet loader/overlay больше не входит в проект.

## Что изменено

- Новый тёмный интерфейс Airflow Recode с accent-подсветкой и обновлённой навигацией.
- Ragebot: выбор стратегии цели (`Crosshair`, `Distance`, `Health`, `Hybrid`) и ограничение FOV. Hybrid учитывает летальный урон, безопасность точки, дистанцию и положение относительно прицела.
- Полностью отдельный Legitbot (`legitbot.cpp/.hpp`):
  - отдельные настройки для каждого оружия;
  - head/chest/stomach/pelvis hitboxes;
  - видимость, smoke/flash checks и penetration damage;
  - current bones и до 12 валидных history records для backtrack;
  - target lock и четыре режима выбора цели;
  - tick-rate-independent smoothing, shot delay и quick stop;
  - standalone RCS;
  - Triggerbot с собственным bind и задержкой;
  - FOV circle и индикатор выбранной точки.
- Старые конфиги продолжают читаться; новые параметры сериализуются в тех же JSON-конфигах.
- Исправлен `c_float_color::base()`: больше не возвращается ссылка на временный объект.

## Сборка

1. Откройте `opaihook.sln` в Visual Studio 2022.
2. Нужен toolset **v143**, Windows SDK и DirectX SDK June 2010 (D3DX9).
3. Выберите `Debug | x86` для разработки или нужную release-конфигурацию проекта.

Главный build-файл — `opaihook.vcxproj`. Корневой `CMakeLists.txt` относится к bundled AsmJit и не собирает Airflow DLL.

GitHub Actions workflow `.github/workflows/msbuild.yml` устанавливает D3DX development files через NuGet, собирает `Debug|Win32` и прикладывает `csgo_debug.dll` как artifact.

## Управление

- `Insert` — открыть/закрыть меню.
- Aim и Trigger имеют независимые binds во вкладке `Legit`.
- Rage и Legit являются взаимоисключающими режимами; одновременное выполнение двух aim-контуров заблокировано.

Игровая проверка offsets/hooks должна выполняться на конкретной поддерживаемой сборке CS:GO отдельно от компиляции.
