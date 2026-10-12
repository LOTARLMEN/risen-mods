Risen Menu 1.1.1
==============

Проще всего ставить моды установщиком: RisenModsInstaller.exe со страницы релизов
https://github.com/LOTARLMEN/risen-mods/releases/latest - он сам найдёт игру, поставит выбранные моды
и это меню, а потом будет их обновлять. Ручная установка ниже - по желанию.

Окно настроек всех моих модов прямо в игре (F10) и значки в левом верхнем углу (песочные часы ускорения,
рука автоподбора). Только для 64-битного Risen 2023 года (Steam). Само по себе ничего не делает -
нужен хотя бы один мод (AutoLoot, Speed, No Limits, Fast Start, Window). Моды работают и без меню,
тогда настройки - только в их .ini, а значков на экране нет.

  F10  открыть / закрыть окно (RU / EN - кнопка с глобусом). Пока оно открыто, игра стоит на паузе.
       Закрыть: Esc, F10 или кнопка.

УСТАНОВКА
  1. Закрыть игру.
  2. Распаковать архив в папку игры Risen (Steam -> ПКМ по Risen -> Управление -> Просмотреть локальные
     файлы). Должно получиться:
         Risen\bin\mods\menu.dll       - меню
  Загрузчик (Risen\bin\version.dll) приходит в архиве любого мода.

УДАЛЕНИЕ
  Удалить Risen\bin\mods\menu.dll и menu.ini. Моды продолжат работать.

НАСТРОЙКИ
  F10 в игре, вкладка "Меню". Или Risen\bin\mods\menu.ini (меняется прямо во время игры):
      [Menu]
      Key=F10            кнопка окна: F1-F24, A-Z, 0-9, NUMPAD0-9, MOUSE3/4/5 и т.д.
      Language=          EN или RU (пусто = язык Windows)
      [Icons]
      Show=1             значки в левом верхнем углу (0 = не показывать)
      Scale=1.0          их размер
  Лог всех модов: Risen\bin\mods\RisenMods.log (после вылета и перезапуска - RisenMods.prev.log)

-------------------------------------------------------------------------------

Risen Menu 1.1.1 (English)

Easiest: RisenModsInstaller.exe from https://github.com/LOTARLMEN/risen-mods/releases/latest - it finds the
game, installs the mods you pick together with this menu and keeps them updated. Manual install is optional.

In-game settings window (F10) for all my mods plus the status icons in the top-left corner (Speed's hourglass,
AutoLoot's hand). Risen 64-bit 2023 re-release (Steam). Does nothing alone - install at least one mod.
The mods also work without it (settings in their .ini files only, no icons).

Install:   close the game, extract into the Risen folder -> Risen\bin\mods\menu.dll
           (the loader bin\version.dll comes with every mod zip).
Uninstall: delete Risen\bin\mods\menu.dll and menu.ini; the mods keep working.
Settings:  F10 -> "Menu" page, or bin\mods\menu.ini ([Menu] Key, Language EN/RU; [Icons] Show, Scale).

Uses Dear ImGui (https://github.com/ocornut/imgui), MIT license, Copyright (c) 2014-2026 Omar Cornut.
