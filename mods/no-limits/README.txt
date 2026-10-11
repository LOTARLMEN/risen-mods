Risen No Limits 0.1
===================

Снимает лимит учителей на силу и ловкость. Только для 64-битного Risen 2023 года (Steam).
Обычно каждый учитель учит силу / ловкость только до своего предела, дальше надо искать другого.
С модом любой учитель силы или ловкости учит до максимума самой игры - 200.
Стоимость в очках обучения прежняя. Остальные навыки (оружие, магия, воровские и т.д.) не меняются.

  Игровые файлы и сохранения не изменяются: мод правит проверку учителя только в памяти,
  пока игра запущена. Без мода у учителей снова их обычные лимиты, а уже выученное остаётся.
  Если версия игры другая (не та, под которую сделан мод), он просто выключится и ничего не тронет.

  F10  окно настроек всех моих модов прямо в игре, вкладка "Без лимитов" - включить / выключить.

УСТАНОВКА
  1. Закрыть игру.
  2. Открыть папку игры: Steam -> ПКМ по Risen -> Управление -> Просмотреть локальные файлы.
  3. Распаковать архив в эту папку (папка bin из архива ляжет поверх Risen\bin).
     Должно получиться:
         Risen\bin\version.dll          - загрузчик модов (общий для всех моих модов)
         Risen\bin\mods\no_limits.dll   - сам мод
         Risen\bin\mods\menu.dll        - меню настроек F10 (общее для всех моих модов)
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
      Enabled=1          сила и ловкость без лимитов учителей

ЕСЛИ ЧТО-ТО НЕ ТАК
  - Игра не запускается или вылетает - удалите version.dll (см. УДАЛЕНИЕ), игра станет как была,
    и пришлите мне файлы Risen\bin\mods\loader.log и no_limits.log.
  - Антивирус может ругаться на version.dll: это обычное ложное срабатывание на моды,
    которые загружаются через подмену системной DLL.
  - Несовместим с другими модами, которые ставят СВОЙ version.dll в Risen\bin.
  - Windows должна быть установлена на диск C:.

-------------------------------------------------------------------------------

Risen No Limits 0.1 (English)

Removes the teachers' limits on strength and dexterity in Risen (64-bit 2023 re-release, Steam): any strength or
dexterity teacher trains up to the game's own maximum of 200. Learning point costs and all other skills are
unchanged. Patched in memory only while the game runs: no game files or saves are changed; on a different game
version the mod switches itself off.
Install:   close the game, extract into the Risen folder -> Risen\bin\version.dll (mod loader)
           + Risen\bin\mods\no_limits.dll + Risen\bin\mods\menu.dll (F10 menu).
Uninstall: close the game, delete Risen\bin\version.dll and the Risen\bin\mods folder.
Settings:  F10 in game (tab "No Limits"), or Risen\bin\mods\no_limits.ini ([NoLimits] Enabled).
Requires Windows on drive C:.

Uses Dear ImGui (https://github.com/ocornut/imgui), MIT license, Copyright (c) 2014-2026 Omar Cornut.
