Risen Quick Lockpick 0.1.1
========================

Проще всего - установщиком: RisenModsInstaller.exe со страницы
https://github.com/LOTARLMEN/risen-mods/releases/latest - сам найдёт игру, поставит выбранные моды
вместе с меню настроек (F10 в игре - настройки всех модов) и будет их обновлять.
Ручная установка ниже - по желанию.

Быстрый взлом замков без мини-игры. Только для 64-битного Risen 2023 года (Steam).
Смотришь на запертый сундук или дверь и жмёшь правую кнопку мыши - замок сразу открыт.

  - Можно ли его вскрыть, решает сама игра, как обычно: навык взлома против сложности замка,
    отмычка или ключ в инвентаре, замки только под ключ. Если нельзя - герой скажет свою обычную
    фразу ("Кто бы меня научил...", "Нет отмычки" и т.п.).
  - Есть ключ - замок просто открывается, отмычка не тратится.
  - Отмычкой замок открывается всегда, но отмычка может сломаться. Шанс зависит от навыка взлома:
    1 - 50%, 2 - 30%, 3 - 15% (меняется в настройках).
  - Вскрытый сундук сразу лутается (можно выключить).
  - Чужие замки (дома, сундуки с хозяином) мод не трогает - там обычная мини-игра, со всеми
    последствиями кражи, как в игре.
  - С обнажённым оружием клавиша работает как обычно в игре.

  F10  окно настроек всех моих модов прямо в игре, вкладка "Взлом" - клавиша, сундуки / двери,
       лут сразу, шансы поломки отмычки.

УСТАНОВКА
  1. Закрыть игру.
  2. Открыть папку игры: Steam -> ПКМ по Risen -> Управление -> Просмотреть локальные файлы.
  3. Распаковать архив в эту папку (папка bin из архива ляжет поверх Risen\bin).
     Должно получиться:
         Risen\bin\version.dll               - загрузчик модов (общий для всех моих модов)
         Risen\bin\mods\quick_lockpick.dll   - сам мод
         Меню настроек F10 - отдельный архив RisenMenu, распаковывается так же
         (Risen\bin\mods\menu.dll). Без него мод работает, но без окна F10,
         настройки - только в .ini.
  4. Запустить игру. При первом запуске появится Risen\bin\mods\quick_lockpick.ini с настройками.

УДАЛЕНИЕ (игра станет ровно как была)
  1. Закрыть игру.
  2. Удалить из папки Risen\bin:
         version.dll
         папку mods целиком
  Если стоят и другие мои моды и их хочется оставить - удалите только
  Risen\bin\mods\quick_lockpick.dll и quick_lockpick.ini (menu.dll оставьте, он нужен остальным модам).
  Проверка целостности файлов в Steam version.dll и папку mods НЕ удаляет - удаляйте руками.

НАСТРОЙКИ
  Через F10 в игре или в Risen\bin\mods\quick_lockpick.ini (подхватывается прямо во время игры):
      [QuickLockpick]
      Enabled=1          быстрый взлом включён
      Key=MOUSE2         клавиша (MOUSE2 = правая кнопка мыши, как у быстрого лута AutoLoot)
      Chests=1           сундуки
      Doors=1            двери
      LootAfter=1        сразу лутать вскрытый сундук
      BreakChance1=50    шанс (%) сломать отмычку при навыке взлома 1
      BreakChance2=30    ... при навыке 2
      BreakChance3=15    ... при навыке 3

ЕСЛИ ЧТО-ТО НЕ ТАК
  - Игра не запускается или вылетает - удалите version.dll (см. УДАЛЕНИЕ), игра станет как была,
    и пришлите мне файл Risen\bin\mods\RisenMods.log (а если игра вылетела и вы её уже перезапустили -
    RisenMods.prev.log). Папку с ним открывает кнопка "Папка с логом" внизу окна F10.
  - Антивирус может ругаться на version.dll: это обычное ложное срабатывание на моды,
    которые загружаются через подмену системной DLL.
  - Несовместим с другими модами, которые ставят СВОЙ version.dll в Risen\bin.
  - Windows должна быть установлена на диск C:.

-------------------------------------------------------------------------------

Risen Quick Lockpick 0.1.1 (English)

Easiest: RisenModsInstaller.exe from https://github.com/LOTARLMEN/risen-mods/releases/latest - it finds the
game, installs the mods you pick with the F10 settings menu and keeps them updated. Manual install is optional.

Quick lockpicking without the minigame in Risen (64-bit 2023 re-release, Steam): look at a locked chest or door
and press the right mouse button - the lock opens at once. The game itself decides whether you can (lockpick
skill against the lock's difficulty, a lockpick or the key, key-only locks) and the hero says his usual line when
not. With the key the lock just opens. With a lockpick it always opens, but the lockpick may break: 50% / 30% /
15% at lockpick skill 1 / 2 / 3 (configurable). The chest is looted right away (optional). Locks that belong to
someone are left to the game's minigame, theft consequences as usual. Not while the weapon is drawn.
Install:   close the game, extract into the Risen folder -> Risen\bin\version.dll (mod loader)
           + Risen\bin\mods\quick_lockpick.dll.
           F10 menu (optional, without it settings are in the .ini only): the RisenMenu zip,
           extracted the same way -> Risen\bin\mods\menu.dll.
Uninstall: close the game, delete Risen\bin\version.dll and the Risen\bin\mods folder.
Settings:  F10 in game (tab "Lockpick"), or Risen\bin\mods\quick_lockpick.ini ([QuickLockpick] Enabled, Key,
           Chests, Doors, LootAfter, BreakChance1-3).
Requires Windows on drive C:.

Uses Dear ImGui (https://github.com/ocornut/imgui), MIT license, Copyright (c) 2014-2026 Omar Cornut.
