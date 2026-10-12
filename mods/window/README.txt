Risen Window 1.2.1
================

Проще всего - установщиком: RisenModsInstaller.exe со страницы
https://github.com/LOTARLMEN/risen-mods/releases/latest - сам найдёт игру, поставит выбранные моды
вместе с меню настроек (F10 в игре - настройки всех модов) и будет их обновлять.
Ручная установка ниже - по желанию.

Безрамочное окно на весь экран для Risen (64-битный Risen 2023 года, Steam).
Выглядит как полноэкранный режим, но Alt+Tab работает мгновенно и игра не зависает
(в настоящем полноэкранном режиме с ReShade игра может зависнуть при Alt+Tab).
Также прячет курсор Windows поверх игры (остаётся только игровой) и не даёт мыши
уходить за край окна, пока игра активна.

УСТАНОВКА
  1. Распаковать архив в папку игры Risen:
         Risen\bin\version.dll         - загрузчик модов (общий для всех моих модов)
         Risen\bin\mods\window.dll     - сам мод
         Меню настроек F10 - отдельный архив RisenMenu, распаковывается так же
         (Risen\bin\mods\menu.dll). Без него мод работает, но без окна F10 и значков,
         настройки - только в .ini.
  2. Включить в игре оконный режим. Или вручную: в файле Risen\data\ini\ConfigLocal.xml
     поставить FullScreen="false".

НАСТРОЙКИ
  F10 в игре, вкладка "Окно". Или Risen\bin\mods\window.ini (создаётся при первом запуске, меняется прямо во время игры):
      Borderless=1         безрамочное окно на весь монитор
      HideSystemCursor=1   прятать курсор Windows
      ClipCursor=1         держать мышь внутри окна
  Лог всех модов: Risen\bin\mods\RisenMods.log (после вылета и перезапуска - RisenMods.prev.log)

УДАЛЕНИЕ
  Удалить Risen\bin\mods\window.dll (и version.dll с папкой mods, если других модов нет).

Несовместим с другими модами, которые ставят СВОЙ version.dll в Risen\bin.
Антивирус может ругаться на version.dll - это ложное срабатывание. Windows должна быть на диске C:.

-------------------------------------------------------------------------------
Easiest: RisenModsInstaller.exe from https://github.com/LOTARLMEN/risen-mods/releases/latest - it finds the
game, installs the mods you pick with the F10 settings menu and keeps them updated. Manual install is optional.

English: borderless fullscreen window for Risen (64-bit 2023, Steam), hides the Windows cursor and keeps
the mouse inside the window. Set FullScreen="false" in data\ini\ConfigLocal.xml. Extract into the Risen folder.
Settings: bin\mods\window.ini (hot-reloaded).

Uses Dear ImGui (https://github.com/ocornut/imgui), MIT license, Copyright (c) 2014-2026 Omar Cornut.
