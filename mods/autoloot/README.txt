Risen AutoLoot 0.8.1
==================

Проще всего - установщиком: RisenModsInstaller.exe со страницы
https://github.com/LOTARLMEN/risen-mods/releases/latest - сам найдёт игру, поставит выбранные моды
вместе с меню настроек (F10 в игре - настройки всех модов) и будет их обновлять.
Ручная установка ниже - по желанию.

Быстрый лут без окна и без анимации, как AutoLoot от Hedin в Готике. Только для 64-битного Risen 2023 года (Steam).
Игровые файлы не изменяются, сохранения тоже. Бета-версия: делайте сохранение перед тестом.

  ПКМ  навестись на мёртвое тело, открытый сундук или предмет на земле (как для "использовать")
       и нажать правую кнопку мыши: всё сразу в инвентарь, без окна лута, квестовые предметы тоже.
       С достанным оружием не работает (в бою ПКМ - обычное действие игры).
       Запертый ничей сундук, ключ от которого у вас есть, ПКМ откроет ключом и сразу облутает.
       Если ПКМ держать зажатой, лут повторяется 4 раза в секунду: можно водить взглядом по предметам.
  F    включить / выключить автоподбор: предметы на земле в радиусе 3 м подбираются сами,
       в том числе в боевой стойке. Пока автоподбор включён, в левом верхнем углу горит рука.
       По желанию (в настройках) он же обыскивает трупы и ничьи открытые сундуки рядом.
       По умолчанию берёт только то, что впереди (конус по направлению камеры, 120°; под ногами - всегда),
       и только нужные категории: золото, стрелы, оружие, броня, зелья/еда, свитки/руны, записки,
       разное - галки в F10. Летящие стрелы (свои и чужие) не подбираются.
       Трупы обыскиваются и с оружием в руках, и без наведения на них: мясо, шкуры, когти зверей
       и монстров игра создаёт сама, как при обычном наведении.
       Квестовые предметы - только если включено "Подбирать и квестовые предметы".
       Сундук, который вы отперли сами (ключом, отмычкой), автоподбор не трогает - он ваш.
  F10  окно настроек всех моих модов прямо в игре (RU / EN - кнопка с глобусом). Пока оно открыто,
       игра стоит на паузе. Закрыть: Esc, F10 или кнопка.
  Что подобрано - видно слева в обычных игровых строчках "+ предмет".
  Там, где что-то залутано, вспыхивает маленькая искра (эффект самой игры, без звука), её цвет
  говорит, что взято: золотая - квестовый предмет, красная - оружие и стрелы, голубая - броня,
  фиолетовая - свитки и руны, зелёная - зелья, еда и растения, бело-голубая - всё остальное.
  У сундуков и тел искра чуть крупнее и цвета самой важной вещи из них.
  Размер, звук или отключение - в F10, раздел "Эффект".
  Пустой сундук или труп видно сразу при наведении: к имени добавляется "(Пусто)" (отключается в F10).

  Кража - как в самой игре. Чужое (вещь с владельцем или лежащая в чужом доме, лагере, охраняемом
  месте - игра решает это сама по зоне) по ПКМ взять можно, но это кража: кто это видел - отреагирует
  так же, как при обычном подборе. Если никто не видел - ничего не будет.
  Автоподбор (F) чужое не берёт никогда, и ничего не берёт сквозь стены, пол и закрытые двери.
  Человека, вырубленного в драке (не насмерть), ПКМ обыскивает так же, как труп - но только если игра сама
  даёт его обыскать (как по ЛКМ). Автоподбор (F) с Bodies=1 обыскивает и их (KnockedOut=0 - не трогать).
  Никогда не берётся: запертые сундуки без ключа,
  квестовые объекты, которые игра пока не даёт взять (черепа и т.п.). Это остаётся для обычного взаимодействия.
  Лут идёт через собственный скрипт игры, так что квесты на сбор предметов засчитываются как обычно.

УСТАНОВКА
  1. Закрыть игру.
  2. Открыть папку игры: Steam -> ПКМ по Risen -> Управление -> Просмотреть локальные файлы.
  3. Распаковать архив в эту папку (папка bin из архива ляжет поверх Risen\bin).
     Должно получиться:
         Risen\bin\version.dll         - загрузчик модов (общий для всех моих модов)
         Risen\bin\mods\autoloot.dll   - сам мод
         Меню настроек F10 - отдельный архив RisenMenu, распаковывается так же
         (Risen\bin\mods\menu.dll). Без него мод работает, но без окна F10 и значков,
         настройки - только в .ini.
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
      KnockedOut=1       вместе с трупами - и вырубленных в драке (0 = нет)
      Chests=0           опустошать ничьи открытые сундуки в радиусе (1 = да)
      Cone=1             только впереди (0 = весь круг вокруг)
      ConeAngle=120      ширина конуса, градусы
      QuestItems=0       брать и квестовые предметы (1 = да)
      [Filter]           что подбирать с земли (1 = да, 0 = нет):
      Gold=1  Ammo=1  Weapons=1  Armor=1  Consumables=1  Magic=1  Written=1  Misc=1  Other=1
      [Effect]
      Enabled=1          искра на месте лута
      Name=eff_magic_telekinesis_target_once_01   какой эффект игры показывать
      Height=0           подъём над предметом, см
      Size=0.15          размер (1 = как в игре)
      Sound=0            звук эффекта (1 = да)
      [Empty]
      Mark=1             метка "(Пусто)" у пустых сундуков и трупов
      Text=              свой текст метки (пусто = "(Пусто)" / "(Empty)" по языку меню)

ЕСЛИ ЧТО-ТО НЕ ТАК
  - Игра не запускается или вылетает - удалите version.dll (см. УДАЛЕНИЕ), игра станет как была,
    и пришлите мне файл Risen\bin\mods\RisenMods.log (а если игра вылетела и вы её уже перезапустили -
    RisenMods.prev.log). Папку с ним открывает кнопка "Папка с логом" внизу окна F10.
  Баги и вылеты: создайте issue на https://github.com/LOTARLMEN/risen-mods/issues и приложите туда лог.
  - Антивирус может ругаться на version.dll: это обычное ложное срабатывание на моды,
    которые загружаются через подмену системной DLL.
  - Несовместим с другими модами, которые ставят СВОЙ version.dll в Risen\bin.
  - Windows должна быть установлена на диск C:.

-------------------------------------------------------------------------------

Risen AutoLoot 0.8.1 (English, beta)

Easiest: RisenModsInstaller.exe from https://github.com/LOTARLMEN/risen-mods/releases/latest - it finds the
game, installs the mods you pick with the F10 settings menu and keeps them updated. Manual install is optional.

Quick looting for Risen (64-bit 2023 re-release, Steam), no loot window, no animation. No game files are changed.
  Right mouse  on the focused dead body / open chest / ground item: everything goes straight into the inventory,
               quest items too (not with the weapon drawn). A locked chest nobody owns whose key you carry
               is opened with the key and looted.
  F            toggles AutoLoot: loose items within 3 m are picked up automatically (hand icon while on);
               optionally also nearby dead bodies and open chests nobody owns. By default only in front of
               you (120° cone around the camera view, things at your feet always), with per-category filters
               (gold, ammo, weapons, armour, consumables, magic, written, misc). Arrows in flight are ignored.
               Bodies are searched with the weapon drawn too, without focusing them: animal and monster
               plunder (meat, skins, claws) is created the way the game does on focus. Quest items only
               when "Also quest items" is on. A chest you unlocked yourself (key, lockpick) is left to you.
  F10          in-game settings window for all my mods (game paused while open).
Theft works as in the game itself: RMB may take what isn't yours (owned, or lying in someone's house, camp or a
guarded place - the game decides by the zone), but NPCs who see it react as to a normal theft; unseen, nothing
happens. AutoLoot (F) never takes what would be theft, and nothing through walls, floors or closed doors.
Someone knocked out in a non-lethal fight is searched like a body, only when the game itself lets you (as with
LMB) - by RMB, and by AutoLoot (F) with dead bodies on ([AutoLoot] KnockedOut=0 leaves them alone).
Holding RMB repeats QuickLoot 4 times a second, so you can sweep your view over items.
Never taken: locked chests, quest objects the game won't let you take
yet (skulls and the like). Uses the game's own loot script, so quests count the items as usual.
A small silent sparkle (the game's own effect) marks each looted spot, coloured by what was taken: gold = quest
item, red = weapons and ammo, light blue = armour, violet = scrolls and runes, green = potions, food and plants,
white-blue = anything else (chests and bodies: a bigger flash in the colour of the most important thing taken);
size / sound / off in F10. Empty chests and bodies show "(Empty)" next to their name on focus.

Install:   close the game, extract into the Risen folder -> Risen\bin\version.dll (mod loader)
           + Risen\bin\mods\autoloot.dll.
           F10 menu (optional, without it settings are in the .ini only): the RisenMenu zip,
           extracted the same way -> Risen\bin\mods\menu.dll.
Uninstall: close the game, delete Risen\bin\version.dll and the Risen\bin\mods folder. Nothing else is changed.
Settings:  F10 in game, or Risen\bin\mods\autoloot.ini ([QuickLoot] Key/GroundItems/Chests,
           [AutoLoot] Active/ToggleKey/Radius/Bodies/Chests/Cone/ConeAngle/QuestItems, [Filter] Gold/Ammo/
           Weapons/Armor/Consumables/Magic/Written/Misc/Other, [Effect] Enabled/Name/Height/Size/Sound,
           [Empty] Mark/Text).
Requires Windows on drive C:.

Uses Dear ImGui (https://github.com/ocornut/imgui), MIT license, Copyright (c) 2014-2026 Omar Cornut.

Bugs and crashes: open an issue at https://github.com/LOTARLMEN/risen-mods/issues and attach Risen\bin\mods\RisenMods.log
(after a crash and a restart: RisenMods.prev.log; F10 > "Log folder" opens the folder).
