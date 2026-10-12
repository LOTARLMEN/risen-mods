Risen No Limits 0.2.1
===================

Проще всего - установщиком: RisenModsInstaller.exe со страницы
https://github.com/LOTARLMEN/risen-mods/releases/latest - сам найдёт игру, поставит выбранные моды
вместе с меню настроек (F10 в игре - настройки всех модов) и будет их обновлять.
Ручная установка ниже - по желанию.

Снимает ограничения прокачки силы и ловкости. Только для 64-битного Risen 2023 года (Steam).
Обычно сила и ловкость не поднимаются выше 200, а каждый учитель учит их только до своего предела.
С модом максимум - 1000 (меняется в настройках), и любой учитель силы или ловкости учит до него.
Стоимость в очках обучения прежняя. Остальные навыки (оружие, магия, воровские и т.д.) не меняются.

  Игровые файлы не изменяются: мод правит максимум и проверку учителя только в памяти, пока игра
  запущена. Вкачанное выше 200 хранится в сохранении как обычно. Без мода игра снова покажет
  не больше 200, но вкачанное в сохранении не пропадёт: с модом вернётся.
  Если версия игры другая (не та, под которую сделан мод), он просто выключится и ничего не тронет.

  F10  окно настроек всех моих модов прямо в игре, вкладка "Без лимитов" - включить / выключить,
       максимум.

УСТАНОВКА
  1. Закрыть игру.
  2. Открыть папку игры: Steam -> ПКМ по Risen -> Управление -> Просмотреть локальные файлы.
  3. Распаковать архив в эту папку (папка bin из архива ляжет поверх Risen\bin).
     Должно получиться:
         Risen\bin\version.dll          - загрузчик модов (общий для всех моих модов)
         Risen\bin\mods\no_limits.dll   - сам мод
         Меню настроек F10 - отдельный архив RisenMenu, распаковывается так же
         (Risen\bin\mods\menu.dll). Без него мод работает, но без окна F10 и значков,
         настройки - только в .ini.
  4. Запустить игру. При первом запуске появится Risen\bin\mods\no_limits.ini с настройками.

УДАЛЕНИЕ (игра станет ровно как была)
  1. Закрыть игру.
  2. Удалить из папки Risen\bin:
         version.dll
         папку mods целиком
  Если стоят и другие мои моды и их хочется оставить - удалите только
  Risen\bin\mods\no_limits.dll и no_limits.ini (menu.dll оставьте, он нужен остальным модам).
  Проверка целостности файлов в Steam version.dll и папку mods НЕ удаляет - удаляйте руками.

НАСТРОЙКИ
  Через F10 в игре или в Risen\bin\mods\no_limits.ini (подхватывается прямо во время игры):
      [NoLimits]
      Enabled=1          сила и ловкость без лимитов
      MaxValue=1000      их максимум (в игре 200; от 200 до 9999)

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

Risen No Limits 0.2.1 (English)

Easiest: RisenModsInstaller.exe from https://github.com/LOTARLMEN/risen-mods/releases/latest - it finds the
game, installs the mods you pick with the F10 settings menu and keeps them updated. Manual install is optional.

Removes the training limits of strength and dexterity in Risen (64-bit 2023 re-release, Steam): their maximum is
1000 instead of 200 (configurable), and any strength or dexterity teacher trains up to it instead of their own
limit. Learning point costs and all other skills are unchanged. Patched in memory only while the game runs, no game
files are changed; without the mod a save shows at most 200 again, the trained points stay in it. On a different
game version the mod switches itself off.
Install:   close the game, extract into the Risen folder -> Risen\bin\version.dll (mod loader)
           + Risen\bin\mods\no_limits.dll.
           F10 menu (optional, without it settings are in the .ini only): the RisenMenu zip,
           extracted the same way -> Risen\bin\mods\menu.dll.
Uninstall: close the game, delete Risen\bin\version.dll and the Risen\bin\mods folder.
Settings:  F10 in game (tab "No Limits"), or Risen\bin\mods\no_limits.ini ([NoLimits] Enabled, MaxValue).
Requires Windows on drive C:.

Uses Dear ImGui (https://github.com/ocornut/imgui), MIT license, Copyright (c) 2014-2026 Omar Cornut.

Bugs and crashes: open an issue at https://github.com/LOTARLMEN/risen-mods/issues and attach Risen\bin\mods\RisenMods.log
(after a crash and a restart: RisenMods.prev.log; F10 > "Log folder" opens the folder).
