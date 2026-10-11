Risen Speed 1.0
===============

Ускорение игры по кнопке, как Z-mod в Готике: F4 включает/выключает ускорение (по умолчанию x2).
Для 64-битного Risen 2023 года (Steam).

УСТАНОВКА
  Распаковать архив в папку игры Risen:
      Risen\bin\version.dll        - загрузчик модов (общий для всех моих модов)
      Risen\bin\mods\speed.dll     - сам мод

НАСТРОЙКИ
  Risen\bin\mods\speed.ini (создаётся при первом запуске, меняется прямо во время игры):
      Enabled=1          мод включён
      Multiplier=2.0     во сколько раз ускорять (1.0 = обычная скорость, максимум 10)
      ToggleKey=F4       кнопка: F1-F24, A-Z, 0-9, NUMPAD0-9, MOUSE3/4/5 и т.д.
  Не назначайте F3, F5-F9, F12 - их использует игра (F8 быстрое сохранение, F9 загрузка).
  Лог: Risen\bin\mods\speed.log

УДАЛЕНИЕ
  Удалить Risen\bin\mods\speed.dll (и version.dll с папкой mods, если других модов нет).

Несовместим с другими модами, которые ставят СВОЙ version.dll в Risen\bin.
Антивирус может ругаться на version.dll - это ложное срабатывание. Windows должна быть на диске C:.

-------------------------------------------------------------------------------
English: game speed toggle for Risen (64-bit 2023, Steam). F4 toggles x2 speed. Extract into the Risen folder.
Settings: bin\mods\speed.ini (Multiplier, ToggleKey; hot-reloaded).
