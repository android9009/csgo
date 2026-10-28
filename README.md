# Airflow 1.5

База проекта — исходный код **Airflow 1.5** для CS:GO.

- Источник: [`cacamelio/airflow-sources-fixed`](https://github.com/cacamelio/airflow-sources-fixed)
- Каталог в источнике: `v1.5`
- Коммит источника: `120fd1f3a39cbd50fcb70572a1e9286eddefc57d`
- Решение: `opaihook.sln`
- Платформа: `Win32`
- Toolset: `v142`

Сторонние исходники и предсобранные библиотеки находятся в `Libs`. Отдельный vcpkg/bootstrap/online-setup шаг не используется.

## Debug Win32

Для `Debug` и `Legacy Debug` добавлен проект `Libs/PolyHook/PolyHook_2.Debug.vcxproj`. Он без CMake и абсолютных путей собирает из уже имеющихся исходников одну библиотеку `Libs/PolyHook/build32/Debug/PolyHook_2.lib` с `/MTd`: необходимые Win32-хуки PolyHook, Zydis и Zycore. Отдельная Debug-библиотека `zydis.lib` больше не требуется. Release-подобные конфигурации продолжают использовать прежние предсобранные библиотеки.

Откройте `opaihook.sln`, выберите `Debug|x86` и выполните Build Solution. Новый проект подключён через ProjectReference, поэтому зависимость должна собраться перед основным DLL-проектом.

## Изменения логики

- При активном defensive DT и peek существующий fakelag (его enable/limit) учитывается в пределах оставшегося command budget; recharge, обычный DT, fake-duck и ручной desync не переработаны.
- Ragebot не экстраполирует shifting/invalid front record и продолжает поиск только среди valid records.
- Исправлено невозможное условие определения on-shot record.
- Однотиковый видимый yaw excursion с возвратом помечается как measured flick record; для него последовательно используется уже построенная measured-center matrix. Скрытые defensive углы не угадываются, firing `tick_count` не изменён.

## Проверка

В Linux-окружении выполнены только статические проверки файлов и конфигурации. Фактическая сборка MSVC, линковка, инжект и игровая эффективность здесь не проверялись; их нужно подтвердить в Visual Studio и затем в игре.
