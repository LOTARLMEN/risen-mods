Risen Speed 1.5
===============

Ускорение игры по кнопке, как Z-mod в Готике. Только для 64-битного Risen 2023 года (Steam).
Игровые файлы не изменяются, сохранения тоже.

  Z    включить / выключить ускорение (по умолчанию x3).
       Пока ускорение включено, в левом верхнем углу горят песочные часы и множитель (x3).
  По желанию (галки в F10):
       - обычная скорость в диалогах: на время разговора ускорение замирает (часы темнеют),
         после диалога возвращается само;
       - клавиши шага (по умолчанию NumPad + и -): быстрее / медленнее на заданный шаг
         (по умолчанию 0.5), от x1 до x10. "Быстрее" заодно включает ускорение.
  F10  окно настроек всех моих модов прямо в игре (RU / EN - кнопка с глобусом). Пока оно открыто,
       игра стоит на паузе. Закрыть: Esc, F10 или кнопка.

УСТАНОВКА
  1. Закрыть игру.
  2. Открыть папку игры: Steam -> ПКМ по Risen -> Управление -> Просмотреть локальные файлы.
  3. Распаковать архив в эту папку (папка bin из архива ляжет поверх Risen\bin).
     Должно получиться:
         Risen\bin\version.dll        - загрузчик модов (общий для всех моих модов)
         Risen\bin\mods\speed.dll     - сам мод
         Risen\bin\mods\menu.dll       - меню настроек F10 (общее для всех моих модов)
  4. Запустить игру. При первом запуске появится Risen\bin\mods\speed.ini с настройками.

УДАЛЕНИЕ (игра станет ровно как была)
  1. Закрыть игру.
  2. Удалить из папки Risen\bin:
         version.dll
         папку mods целиком
  Больше мод ничего не создаёт и не меняет. Если у вас стоят и другие мои моды (Fast Start, Window)
  и их хочется оставить - удалите только Risen\bin\mods\speed.dll и speed.ini
  (menu.dll оставьте, он нужен остальным модам).
  Проверка целостности файлов в Steam version.dll и папку mods НЕ удаляет - удаляйте руками.

НАСТРОЙКИ
  Удобнее всего через F10 в игре, вкладка "Ускорение". Или руками в Risen\bin\mods\speed.ini
  (изменения подхватываются прямо во время игры):
      [Speed]
      Enabled=1          мод включён (0 = выключен)
      Multiplier=3.0     во сколько раз ускорять (максимум 10)
      ToggleKey=Z        кнопка: F1-F24, A-Z, 0-9, NUMPAD0-9, MOUSE3/4/5 и т.д.
      NormalInDialogs=0  обычная скорость в диалогах (1 = да)
      [Steps]
      Enabled=0          клавиши шага (1 = включены)
      FasterKey=NUMPAD+  быстрее
      SlowerKey=NUMPAD-  медленнее
      StepUp=0.5         шаг "быстрее"
      StepDown=0.5       шаг "медленнее" (ниже x1 не опускается)
      [Indicator]
      Show=1             показывать песочные часы (0 = не показывать)
  Кнопка окна F10, язык и размер значков - в Risen\bin\mods\menu.ini (или вкладка "Меню").
  Не назначайте F3, F5-F9, F12 - их использует игра (F8 быстрое сохранение, F9 загрузка).

ЕСЛИ ЧТО-ТО НЕ ТАК
  - Игра не запускается или вылетает - удалите version.dll (см. УДАЛЕНИЕ), игра станет как была,
    и пришлите мне файлы Risen\bin\mods\loader.log и speed.log.
  - Антивирус может ругаться на version.dll: это обычное ложное срабатывание на моды,
    которые загружаются через подмену системной DLL.
  - Несовместим с другими модами, которые ставят СВОЙ version.dll в Risen\bin.
  - Windows должна быть установлена на диск C:.
  - Сохраняться можно как обычно, в том числе с включённым ускорением.

-------------------------------------------------------------------------------

Risen Speed 1.5 (English)

Game speed toggle for Risen (64-bit 2023 re-release, Steam), like Gothic's Z-mod. No game files are changed.
  Z    toggles x3 speed; an hourglass + x3 indicator shows while it is on.
  Optional (F10): normal speed in dialogs (hourglass dimmed meanwhile); step keys (NumPad + / -) change the
  speed by a set step (default 0.5) between x1 and x10.
  F10  in-game settings window for all my mods (language RU/EN); the game is paused while it is open.

Install:   close the game, extract into the Risen folder -> Risen\bin\version.dll (mod loader)
           + Risen\bin\mods\speed.dll + Risen\bin\mods\menu.dll (F10 menu, hourglass).
Uninstall: close the game, delete Risen\bin\version.dll and the Risen\bin\mods folder. Nothing else is changed.
Settings:  F10 in game, or Risen\bin\mods\speed.ini ([Speed] Enabled/Multiplier/ToggleKey/NormalInDialogs,
           [Steps] Enabled/FasterKey/SlowerKey/StepUp/StepDown,
           [Indicator] Show; hot-reloaded); window key, language, icon size: menu.ini.
Requires Windows on drive C:.

Uses Dear ImGui (https://github.com/ocornut/imgui), MIT license, Copyright (c) 2014-2026 Omar Cornut.
