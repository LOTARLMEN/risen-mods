Risen Fast Start 1.0
====================

Ускоряет запуск Risen: окно игры появляется примерно через 10 секунд вместо ~30.
Игровые файлы не изменяются. Только для 64-битного Risen 2023 года (Steam).

УСТАНОВКА
  Скопировать version.dll в папку игры Risen\bin (рядом с Risen.exe).
  Путь в Steam: ПКМ по Risen -> Управление -> Просмотреть локальные файлы -> папка bin.

УДАЛЕНИЕ
  Удалить Risen\bin\version.dll.

ЧТО ДЕЛАЕТ
  1. При запуске игра читает свои архивы .pak примерно 8 миллионами крошечных
     обращений к диску (по 1-64 байта). Мод читает их через буфер в памяти:
     несколько тысяч обращений вместо миллионов. Данные те же самые.
  2. Движок запускает DxDiag (диагностику DirectX) только ради объёма видеопамяти,
     а это ~7 секунд. Мод отдаёт то же число напрямую из DirectX (DXGI).

ЕСЛИ ЧТО-ТО НЕ ТАК
  - Игра не запускается — удалите version.dll, игра станет как была.
  - Можно выключить любую часть: создайте рядом с version.dll файл
    risen_fast_start.ini с текстом:
        [Performance]
        FastPakReads=0
        SkipDxDiag=0
    (0 = выключено). Лог работы мода: Risen\bin\risen_fast_start.log
  - Антивирус может ругаться на version.dll: это обычное ложное срабатывание
    на моды, которые загружаются через подмену системной DLL.
  - Несовместим с другими модами, которые тоже ставят свой version.dll в Risen\bin.
  - Windows должна быть установлена на диск C:.

-------------------------------------------------------------------------------

Risen Fast Start 1.0 (English)

Makes Risen (64-bit 2023 re-release, Steam) start in ~10 s instead of ~30 s.
No game files are changed.

Install:   copy version.dll to Risen\bin (next to Risen.exe).
Uninstall: delete Risen\bin\version.dll.

What it does: buffers the game's ~8 million tiny reads of its .pak archives at startup,
and answers the engine's video memory query directly from DXGI instead of running DxDiag (~7 s).
Optional Risen\bin\risen_fast_start.ini: [Performance] FastPakReads=0 / SkipDxDiag=0 to turn parts off.
Log: Risen\bin\risen_fast_start.log. Requires Windows on drive C:.
