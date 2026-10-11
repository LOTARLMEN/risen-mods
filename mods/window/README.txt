Risen Window 1.0
================

Безрамочное окно на весь экран для Risen (64-битный Risen 2023 года, Steam).
Выглядит как полноэкранный режим, но Alt+Tab работает мгновенно и игра не зависает
(в настоящем полноэкранном режиме с ReShade игра может зависнуть при Alt+Tab).
Также прячет курсор Windows поверх игры (остаётся только игровой) и не даёт мыши
уходить за край окна, пока игра активна.

УСТАНОВКА
  1. Распаковать архив в папку игры Risen:
         Risen\bin\version.dll         - загрузчик модов (общий для всех моих модов)
         Risen\bin\mods\window.dll     - сам мод
  2. Включить в игре оконный режим. Или вручную: в файле Risen\data\ini\ConfigLocal.xml
     поставить FullScreen="false".

НАСТРОЙКИ
  Risen\bin\mods\window.ini (создаётся при первом запуске, меняется прямо во время игры):
      Borderless=1         безрамочное окно на весь монитор
      HideSystemCursor=1   прятать курсор Windows
      ClipCursor=1         держать мышь внутри окна
  Лог: Risen\bin\mods\window.log

УДАЛЕНИЕ
  Удалить Risen\bin\mods\window.dll (и version.dll с папкой mods, если других модов нет).

Несовместим с другими модами, которые ставят СВОЙ version.dll в Risen\bin.
Антивирус может ругаться на version.dll - это ложное срабатывание. Windows должна быть на диске C:.

-------------------------------------------------------------------------------
English: borderless fullscreen window for Risen (64-bit 2023, Steam), hides the Windows cursor and keeps
the mouse inside the window. Set FullScreen="false" in data\ini\ConfigLocal.xml. Extract into the Risen folder.
Settings: bin\mods\window.ini (hot-reloaded).
