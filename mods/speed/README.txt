Risen Speed 1.2
===============

Ускорение игры по кнопке, как Z-mod в Готике. Только для 64-битного Risen 2023 года (Steam).
Игровые файлы не изменяются, сохранения тоже.

  Z    включить / выключить ускорение (по умолчанию x3).
       Пока ускорение включено, в левом верхнем углу горят песочные часы и множитель (x3).
  F10  окно настроек прямо в игре: скорость, клавиша, часы, язык (RU / EN - кнопка с глобусом
       справа вверху). Пока окно открыто, игра стоит на паузе. Закрыть: Esc, F10 или кнопка.

УСТАНОВКА
  1. Закрыть игру.
  2. Открыть папку игры: Steam -> ПКМ по Risen -> Управление -> Просмотреть локальные файлы.
  3. Распаковать архив в эту папку (папка bin из архива ляжет поверх Risen\bin).
     Должно получиться:
         Risen\bin\version.dll        - загрузчик модов (общий для всех моих модов)
         Risen\bin\mods\speed.dll     - сам мод
  4. Запустить игру. При первом запуске появится Risen\bin\mods\speed.ini с настройками.

УДАЛЕНИЕ (игра станет ровно как была)
  1. Закрыть игру.
  2. Удалить из папки Risen\bin:
         version.dll
         папку mods целиком
  Больше мод ничего не создаёт и не меняет. Если у вас стоят и другие мои моды (Fast Start, Window)
  и их хочется оставить - удалите только Risen\bin\mods\speed.dll и speed.ini.
  Проверка целостности файлов в Steam version.dll и папку mods НЕ удаляет - удаляйте руками.

НАСТРОЙКИ
  Удобнее всего через F10 в игре. Или руками в Risen\bin\mods\speed.ini
  (изменения подхватываются прямо во время игры):
      [Speed]
      Enabled=1          мод включён (0 = выключен)
      Multiplier=3.0     во сколько раз ускорять (максимум 10)
      ToggleKey=Z        кнопка: F1-F24, A-Z, 0-9, NUMPAD0-9, MOUSE3/4/5 и т.д.
      [Indicator]
      Show=1             показывать песочные часы (0 = не показывать)
      Scale=1.0          размер часов
      [Panel]
      Key=F10            кнопка окна настроек
      Language=RU        язык окна: RU или EN
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

Risen Speed 1.2 (English)

Game speed toggle for Risen (64-bit 2023 re-release, Steam), like Gothic's Z-mod. No game files are changed.
  Z    toggles x3 speed; an hourglass + x3 indicator shows while it is on.
  F10  in-game settings window (speed, key, indicator, language RU/EN); the game is paused while it is open.

Install:   close the game, extract into the Risen folder -> Risen\bin\version.dll (mod loader)
           + Risen\bin\mods\speed.dll.
Uninstall: close the game, delete Risen\bin\version.dll and the Risen\bin\mods folder. Nothing else is changed.
Settings:  F10 in game, or Risen\bin\mods\speed.ini ([Speed] Enabled/Multiplier/ToggleKey,
           [Indicator] Show/Scale, [Panel] Key/Language; hot-reloaded).
Requires Windows on drive C:.

Uses Dear ImGui (https://github.com/ocornut/imgui), MIT license, Copyright (c) 2014-2026 Omar Cornut.
