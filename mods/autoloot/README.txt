Risen AutoLoot 0.2
==================

Быстрый лут без окна и без анимации, как AutoLoot от Hedin в Готике. Только для 64-битного Risen 2023 года (Steam).
Игровые файлы не изменяются, сохранения тоже. Бета-версия: делайте сохранение перед тестом.

  ПКМ  навестись на мёртвое тело, открытый сундук или предмет на земле (как для "использовать")
       и нажать правую кнопку мыши: всё сразу в инвентарь, без окна лута.
       С достанным оружием не работает (в бою ПКМ - обычное действие игры).
  F    включить / выключить автоподбор: предметы на земле в радиусе 3 м подбираются сами,
       в том числе в боевой стойке.
  Что подобрано - видно слева в обычных игровых строчках "+ предмет".

  Никогда не берётся: чужие вещи (у которых есть владелец - иначе это кража), квестовые предметы,
  запертые сундуки, люди без сознания (их обыск - кража). Это остаётся для обычного взаимодействия.
  Лут идёт через собственный скрипт игры, так что квесты на сбор предметов засчитываются как обычно.

УСТАНОВКА
  1. Закрыть игру.
  2. Открыть папку игры: Steam -> ПКМ по Risen -> Управление -> Просмотреть локальные файлы.
  3. Распаковать архив в эту папку (папка bin из архива ляжет поверх Risen\bin).
     Должно получиться:
         Risen\bin\version.dll         - загрузчик модов (общий для всех моих модов)
         Risen\bin\mods\autoloot.dll   - сам мод
  4. Запустить игру. При первом запуске появится Risen\bin\mods\autoloot.ini с настройками.

УДАЛЕНИЕ (игра станет ровно как была)
  1. Закрыть игру.
  2. Удалить из папки Risen\bin:
         version.dll
         папку mods целиком
  Больше мод ничего не создаёт и не меняет. Если стоят и другие мои моды (Speed, Fast Start)
  и их хочется оставить - удалите только Risen\bin\mods\autoloot.dll и autoloot.ini.
  Проверка целостности файлов в Steam version.dll и папку mods НЕ удаляет - удаляйте руками.

НАСТРОЙКИ
  Risen\bin\mods\autoloot.ini (изменения подхватываются прямо во время игры):
      [QuickLoot]
      Enabled=1          лут на ПКМ включён
      Key=MOUSE2         кнопка: MOUSE2-5, F1-F24, A-Z, 0-9 и т.д.
      GroundItems=1      ПКМ подбирает и предметы на земле
      Chests=1           ПКМ лутает и сундуки
      [AutoLoot]
      Enabled=1          автоподбор доступен
      Active=0           включён сразу при запуске игры (1 = да)
      ToggleKey=F        кнопка включения
      Radius=300         радиус в сантиметрах (300 = 3 м)

ЕСЛИ ЧТО-ТО НЕ ТАК
  - Игра не запускается или вылетает - удалите version.dll (см. УДАЛЕНИЕ), игра станет как была,
    и пришлите мне файлы Risen\bin\mods\loader.log и autoloot.log.
  - Антивирус может ругаться на version.dll: это обычное ложное срабатывание на моды,
    которые загружаются через подмену системной DLL.
  - Несовместим с другими модами, которые ставят СВОЙ version.dll в Risen\bin.
  - Windows должна быть установлена на диск C:.

-------------------------------------------------------------------------------

Risen AutoLoot 0.2 (English, beta)

Quick looting for Risen (64-bit 2023 re-release, Steam), no loot window, no animation. No game files are changed.
  Right mouse  on the focused dead body / open chest / ground item: everything goes straight into the inventory
               (not with the weapon drawn).
  F            toggles AutoLoot: loose items within 3 m are picked up automatically.
Never taken: owned items (theft), quest items, locked chests, unconscious people. Uses the game's own loot
script, so quests count the items as usual.

Install:   close the game, extract into the Risen folder -> Risen\bin\version.dll (mod loader)
           + Risen\bin\mods\autoloot.dll.
Uninstall: close the game, delete Risen\bin\version.dll and the Risen\bin\mods folder. Nothing else is changed.
Settings:  Risen\bin\mods\autoloot.ini ([QuickLoot] Key/GroundItems/Chests, [AutoLoot] Active/ToggleKey/Radius).
Requires Windows on drive C:.
