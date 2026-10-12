Risen Fast Start 1.2.1
====================

Проще всего - установщиком: RisenModsInstaller.exe со страницы
https://github.com/LOTARLMEN/risen-mods/releases/latest - сам найдёт игру, поставит выбранные моды
вместе с меню настроек (F10 в игре - настройки всех модов) и будет их обновлять.
Ручная установка ниже - по желанию.

Ускоряет запуск Risen: окно игры появляется примерно через 10 секунд вместо ~30.
Игровые файлы не изменяются. Только для 64-битного Risen 2023 года (Steam).

УСТАНОВКА
  Распаковать архив в папку игры Risen (папка bin из архива ляжет поверх Risen\bin).
  Должно получиться:
      Risen\bin\version.dll            - загрузчик модов (общий для всех моих модов)
      Risen\bin\mods\fast_start.dll    - сам мод
  Папка игры в Steam: ПКМ по Risen -> Управление -> Просмотреть локальные файлы.

УДАЛЕНИЕ
  Удалить Risen\bin\mods\fast_start.dll.
  Если других модов из папки mods нет - удалить и Risen\bin\version.dll с папкой mods.

ЧТО ДЕЛАЕТ
  1. При запуске игра читает свои архивы .pak примерно 8 миллионами крошечных
     обращений к диску (по 1-64 байта). Мод читает их через буфер в памяти:
     несколько тысяч обращений вместо миллионов. Данные те же самые.
  2. Движок запускает DxDiag (диагностику DirectX) только ради объёма видеопамяти,
     а это ~7 секунд. Мод отдаёт то же число напрямую из DirectX (DXGI).

ЕСЛИ ЧТО-ТО НЕ ТАК
  - Игра не запускается - удалите version.dll, игра станет как была.
  - Можно выключить любую часть: создайте файл Risen\bin\mods\fast_start.ini с текстом
        [Performance]
        FastPakReads=0
        SkipDxDiag=0
    (0 = выключено). Лог всех модов: Risen\bin\mods\RisenMods.log (после вылета и перезапуска - RisenMods.prev.log)
    Баги и вылеты: создайте issue на https://github.com/LOTARLMEN/risen-mods/issues и приложите туда лог.
  - Антивирус может ругаться на version.dll: это обычное ложное срабатывание
    на моды, которые загружаются через подмену системной DLL.
  - Несовместим с другими модами, которые ставят СВОЙ version.dll в Risen\bin.
    Мои моды (Window, Speed) используют тот же загрузчик и ставятся вместе.
  - Windows должна быть установлена на диск C:.

-------------------------------------------------------------------------------

Risen Fast Start 1.2.1 (English)

Easiest: RisenModsInstaller.exe from https://github.com/LOTARLMEN/risen-mods/releases/latest - it finds the
game, installs the mods you pick with the F10 settings menu and keeps them updated. Manual install is optional.

Makes Risen (64-bit 2023 re-release, Steam) start in ~10 s instead of ~30 s. No game files are changed.

Install:   extract into the Risen folder -> Risen\bin\version.dll (mod loader) + Risen\bin\mods\fast_start.dll.
Uninstall: delete Risen\bin\mods\fast_start.dll (and version.dll + mods\ if no other mods are left).

What it does: buffers the game's ~8 million tiny reads of its .pak archives at startup,
and answers the engine's video memory query directly from DXGI instead of running DxDiag (~7 s).
Optional Risen\bin\mods\fast_start.ini: [Performance] FastPakReads=0 / SkipDxDiag=0 to turn parts off.
Requires Windows on drive C:.

F10 in game: settings window of all my mods (bin\mods\menu.dll), tab "Fast Start".
Uses Dear ImGui (https://github.com/ocornut/imgui), MIT license, Copyright (c) 2014-2026 Omar Cornut.

Bugs and crashes: open an issue at https://github.com/LOTARLMEN/risen-mods/issues and attach Risen\bin\mods\RisenMods.log
(after a crash and a restart: RisenMods.prev.log; F10 > "Log folder" opens the folder).
