Risen AutoLoot 0.6
==================

Быстрый лут без окна и без анимации, как AutoLoot от Hedin в Готике. Только для 64-битного Risen 2023 года (Steam).
Игровые файлы не изменяются, сохранения тоже. Бета-версия: делайте сохранение перед тестом.

  ПКМ  навестись на мёртвое тело, открытый сундук или предмет на земле (как для "использовать")
       и нажать правую кнопку мыши: всё сразу в инвентарь, без окна лута.
       С достанным оружием не работает (в бою ПКМ - обычное действие игры).
  F    включить / выключить автоподбор: предметы на земле в радиусе 3 м подбираются сами,
       в том числе в боевой стойке. Пока автоподбор включён, в левом верхнем углу горит рука.
       По желанию (в настройках) он же обыскивает трупы и ничьи открытые сундуки рядом.
       По умолчанию берёт только то, что впереди (конус по направлению камеры, 120°; под ногами - всегда),
       и только нужные категории: золото, стрелы, оружие, броня, зелья/еда, свитки/руны, записки,
       разное - галки в F10. Летящие стрелы (свои и чужие) не подбираются.
       Трупы обыскиваются и с оружием в руках, и без наведения на них: мясо, шкуры, когти зверей
       и монстров игра создаёт сама, как при обычном наведении.
  F10  окно настроек всех моих модов прямо в игре (RU / EN - кнопка с глобусом). Пока оно открыто,
       игра стоит на паузе. Закрыть: Esc, F10 или кнопка.
  Что подобрано - видно слева в обычных игровых строчках "+ предмет".
  Там, где что-то залутано, вспыхивает маленькая искра (эффект самой игры, без звука) -
  размер, звук или отключение - в F10, раздел "Эффект".

  Никогда не берётся: чужие вещи (у которых есть владелец и которые никто не выбросил - иначе это
  кража, как считает сама игра), квестовые предметы,
  запертые сундуки, люди без сознания (их обыск - кража). Это остаётся для обычного взаимодействия.
  Лут идёт через собственный скрипт игры, так что квесты на сбор предметов засчитываются как обычно.

УСТАНОВКА
  1. Закрыть игру.
  2. Открыть папку игры: Steam -> ПКМ по Risen -> Управление -> Просмотреть локальные файлы.
  3. Распаковать архив в эту папку (папка bin из архива ляжет поверх Risen\bin).
     Должно получиться:
         Risen\bin\version.dll         - загрузчик модов (общий для всех моих модов)
         Risen\bin\mods\autoloot.dll   - сам мод
         Risen\bin\mods\menu.dll       - меню настроек F10 (общее для всех моих модов)
  4. Запустить игру. При первом запуске появится Risen\bin\mods\autoloot.ini с настройками.

УДАЛЕНИЕ (игра станет ровно как была)
  1. Закрыть игру.
  2. Удалить из папки Risen\bin:
         version.dll
         папку mods целиком
  Больше мод ничего не создаёт и не меняет. Если стоят и другие мои моды (Speed, Fast Start)
  и их хочется оставить - удалите только Risen\bin\mods\autoloot.dll и autoloot.ini
  (menu.dll оставьте, он нужен остальным модам).
  Проверка целостности файлов в Steam version.dll и папку mods НЕ удаляет - удаляйте руками.

НАСТРОЙКИ
  Удобнее всего через F10 в игре, вкладка "Автолут". Или руками в Risen\bin\mods\autoloot.ini
  (изменения подхватываются прямо во время игры):
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
      Bodies=0           обыскивать трупы в радиусе (1 = да)
      Chests=0           опустошать ничьи открытые сундуки в радиусе (1 = да)
      Cone=1             только впереди (0 = весь круг вокруг)
      ConeAngle=120      ширина конуса, градусы
      [Filter]           что подбирать с земли (1 = да, 0 = нет):
      Gold=1  Ammo=1  Weapons=1  Armor=1  Consumables=1  Magic=1  Written=1  Misc=1  Other=1
      [Effect]
      Enabled=1          искра на месте лута
      Name=eff_magic_telekinesis_target_once_01   какой эффект игры показывать
      Height=0           подъём над предметом, см
      Size=0.15          размер (1 = как в игре)
      Sound=0            звук эффекта (1 = да)

ЕСЛИ ЧТО-ТО НЕ ТАК
  - Игра не запускается или вылетает - удалите version.dll (см. УДАЛЕНИЕ), игра станет как была,
    и пришлите мне файлы Risen\bin\mods\loader.log и autoloot.log.
  - Антивирус может ругаться на version.dll: это обычное ложное срабатывание на моды,
    которые загружаются через подмену системной DLL.
  - Несовместим с другими модами, которые ставят СВОЙ version.dll в Risen\bin.
  - Windows должна быть установлена на диск C:.

-------------------------------------------------------------------------------

Risen AutoLoot 0.6 (English, beta)

Quick looting for Risen (64-bit 2023 re-release, Steam), no loot window, no animation. No game files are changed.
  Right mouse  on the focused dead body / open chest / ground item: everything goes straight into the inventory
               (not with the weapon drawn).
  F            toggles AutoLoot: loose items within 3 m are picked up automatically (hand icon while on);
               optionally also nearby dead bodies and open chests nobody owns. By default only in front of
               you (120° cone around the camera view, things at your feet always), with per-category filters
               (gold, ammo, weapons, armour, consumables, magic, written, misc). Arrows in flight are ignored.
               Bodies are searched with the weapon drawn too, without focusing them: animal and monster
               plunder (meat, skins, claws) is created the way the game does on focus.
  F10          in-game settings window for all my mods (game paused while open).
Never taken: owned items (theft), quest items, locked chests, unconscious people. Uses the game's own loot
script, so quests count the items as usual. A small silent sparkle (the game's own effect) marks each looted
spot; size / sound / off in F10.

Install:   close the game, extract into the Risen folder -> Risen\bin\version.dll (mod loader)
           + Risen\bin\mods\autoloot.dll + Risen\bin\mods\menu.dll (F10 menu).
Uninstall: close the game, delete Risen\bin\version.dll and the Risen\bin\mods folder. Nothing else is changed.
Settings:  F10 in game, or Risen\bin\mods\autoloot.ini ([QuickLoot] Key/GroundItems/Chests,
           [AutoLoot] Active/ToggleKey/Radius/Bodies/Chests/Cone/ConeAngle, [Filter] Gold/Ammo/Weapons/
           Armor/Consumables/Magic/Written/Misc/Other, [Effect] Enabled/Name/Height/Size/Sound).
Requires Windows on drive C:.

Uses Dear ImGui (https://github.com/ocornut/imgui), MIT license, Copyright (c) 2014-2026 Omar Cornut.
